#include "DragonbornPresence.h"
#include "DragonbornPresence/core/Difficulty.h"
#include "DragonbornPresence/core/RefreshReason.h"
#include "DragonbornPresence/core/PlayerSnapshot.h"
#include "DragonbornPresence/core/LocationAssets.h"
#include "DragonbornPresence/core/Config.h"
#include "DragonbornPresence/core/LocationContext.h"
#include "DragonbornPresence/core/LocationAssetResolver.h"
#include "DragonbornPresence/adapters/config/JsonConfigProvider.h"
#include "DragonbornPresence/application/ports/IConfigProvider.h"
#include "DragonbornPresence/application/ports/IGameDataSource.h"
#include "DragonbornPresence/adapters/SkyrimTrueBeliever/StbGameDataSource.h"
#include "DragonbornPresence/application/ports/IPresenceClient.h"
#include "DragonbornPresence/adapters/discord/DiscordPresenceClient.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace DragonbornPresence
{

    namespace
    {

        namespace constants
        {

            constexpr std::chrono::milliseconds kDiscordCallbackInterval{500};
            constexpr std::uint8_t kPresencePollIntervalInCallbackTicks = 1;
            constexpr std::uint32_t kPendingTaskWarningTicks = 1;
            constexpr std::uint32_t kPendingTaskWarningRepeatTicks = 10;
            constexpr std::string_view kMainMenuName = "Main Menu";
            constexpr std::string_view kLoadingMenuName = "Loading Menu";
            constexpr std::string_view kLoadingText = "Загрузка";
            constexpr std::string_view kUnknownDeathsText = "—";

        } // namespace constants

        namespace runtime
        {

            class PresenceCoordinator;

            /// Adapts Skyrim menu events to the application-level presence coordinator.
            class MenuEventSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
            {
            public:
                /// Binds the sink to the coordinator that owns its lifecycle.
                explicit MenuEventSink(PresenceCoordinator &coordinator) noexcept
                    : coordinator_(coordinator)
                {
                }

                /// Forwards valid menu events and always allows later sinks to run.
                RE::BSEventNotifyControl ProcessEvent(
                    const RE::MenuOpenCloseEvent *event,
                    RE::BSTEventSource<RE::MenuOpenCloseEvent> *) override;

            private:
                PresenceCoordinator &coordinator_;
            };

            /// Adapts Skyrim combat events to the application-level presence coordinator.
            class CombatEventSink final : public RE::BSTEventSink<RE::TESCombatEvent>
            {
            public:
                /// Binds the sink to the coordinator that owns its lifecycle.
                explicit CombatEventSink(PresenceCoordinator &coordinator) noexcept
                    : coordinator_(coordinator)
                {
                }

                /// Forwards valid combat events and always allows later sinks to run.
                RE::BSEventNotifyControl ProcessEvent(
                    const RE::TESCombatEvent *event,
                    RE::BSTEventSource<RE::TESCombatEvent> *) override;

            private:
                PresenceCoordinator &coordinator_;
            };

            /// Coordinates configuration, game data, Discord transport, and Skyrim events.
            class PresenceCoordinator final
            {
            public:
                /// Constructs the coordinator with all external dependencies.
                ///
                /// All dependencies are required and stored as non-owning references. The
                /// composition root must keep their concrete implementations alive for at
                /// least as long as this coordinator.
                PresenceCoordinator(
                    ::DragonbornPresence::application::ports::IConfigProvider &configProvider,
                    ::DragonbornPresence::application::ports::IGameDataSource &gameDataSource,
                    ::DragonbornPresence::application::ports::IPresenceClient &presenceClient) noexcept
                    : configProvider_(configProvider),
                      gameDataSource_(gameDataSource),
                      presenceClient_(presenceClient),
                      menuEventSink_(*this),
                      combatEventSink_(*this)
                {
                }

                /// Stops all plugin work after an exception reaches an external game callback.
                void HandleException(std::string_view context, const char *details) noexcept
                {
                    active_ = false;
                    permanentlyStopped_ = true;
                    callbackThread_.request_stop();
                    try
                    {
                        SKSE::log::critical(
                            "DragonbornPresence exception in '{}': {} The integration was "
                            "stopped; Skyrim can continue normally.",
                            context,
                            details ? details : "unknown exception");
                    }
                    catch (...)
                    {
                    }
                    presenceClient_.Shutdown(
                        "an internal DragonbornPresence exception occurred; see the previous "
                        "critical log entry");
                }

                void HandleUnknownException(std::string_view context) noexcept
                {
                    HandleException(context, "unknown non-standard C++ exception");
                }

                /// Replaces the active configuration with validated file contents or defaults.
                void LoadConfig()
                {
                    config_ = configProvider_.Load();
                }

                /// Initializes integrations, publishes loading state, and registers event sinks.
                void RegisterGameEventHandlers()
                {
                    SKSE::log::info("Registering game event handlers...");
                    if (permanentlyStopped_)
                    {
                        SKSE::log::error(
                            "DragonbornPresence registration was skipped because an earlier "
                            "fatal error permanently stopped this plugin instance.");
                        return;
                    }

                    auto *ui = RE::UI::GetSingleton();
                    auto *eventSource = RE::ScriptEventSourceHolder::GetSingleton();
                    taskInterface_ = SKSE::GetTaskInterface();
                    if (!ui || !eventSource || !taskInterface_)
                    {
                        SKSE::log::critical(
                            "DragonbornPresence cannot start: UI={}, ScriptEventSourceHolder={}, "
                            "SKSE TaskInterface={}. At least one required Skyrim/SKSE service is "
                            "unavailable. No event sinks or background tasks were registered.",
                            ui != nullptr,
                            eventSource != nullptr,
                            taskInterface_ != nullptr);
                        return;
                    }

                    if (!presenceClient_.Initialize(config_))
                        return;

                    active_ = true;
                    gameDataSource_.Initialize();
                    SendLoadingPresence();
                    if (!active_)
                        return;

                    ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuEventSink_);
                    eventSource->AddEventSink<RE::TESCombatEvent>(&combatEventSink_);
                    StartCallbackThread();
                }

                /// Marks the game ready and immediately publishes the complete player state.
                void OnGameLoaded()
                {
                    if (!active_)
                        return;
                    SKSE::log::info("Game loaded — refreshing STB presence data.");
                    gameLoaded_ = true;
                    loading_ = false;
                    RefreshPresence(core::RefreshReason::kGameLoaded);
                }

                /// Applies menu transitions to the loading and game-ready state machine.
                void HandleMenuEvent(const RE::MenuOpenCloseEvent &event)
                {
                    if (!active_)
                        return;
                    if (event.menuName == constants::kMainMenuName && event.opening)
                    {
                        gameLoaded_ = false;
                        SetLoading(true);
                    }
                    else if (event.menuName == constants::kLoadingMenuName)
                    {
                        if (event.opening)
                        {
                            SetLoading(true);
                        }
                        else if (gameLoaded_)
                        {
                            SetLoading(false);
                        }
                    }
                }

                /// Coalesces combat changes into the next 500-millisecond polling task.
                void HandleCombatEvent(const RE::TESCombatEvent &event)
                {
                    if (!active_ || !gameLoaded_ || loading_)
                        return;

                    const bool involvesPlayer =
                        (event.actor && event.actor->IsPlayerRef()) ||
                        (event.targetActor && event.targetActor->IsPlayerRef());
                    const bool mayEndCombat =
                        event.newState.get() == RE::ACTOR_COMBAT_STATE::kNone && lastCombatState_;
                    if (involvesPlayer || mayEndCombat)
                    {
                        combatRefreshRequested_ = true;
                    }
                }

            private:
                /// Stops all future Presence work after a Discord failure.
                void Stop() noexcept
                {
                    active_ = false;
                    permanentlyStopped_ = true;
                    callbackThread_.request_stop();
                }

                /// Sends the stable loading activity used before a playable save is ready.
                void SendLoadingPresence()
                {
                    presenceClient_.UpdateActivity({
                        {},
                        constants::kLoadingText,
                        config_.largeImage,
                        config_.largeText,
                        config_.loadingImage,
                        constants::kLoadingText,
                    });
                    if (!presenceClient_.IsActive())
                        Stop();
                }

                /// Builds and submits one complete activity from the latest player snapshot.
                void RefreshPresence(core::RefreshReason reason)
                {
                    if (!active_)
                        return;
                    if (loading_ || !gameLoaded_)
                    {
                        SendLoadingPresence();
                        return;
                    }

                    const core::PlayerSnapshot snapshot = gameDataSource_.ReadPlayerSnapshot();
                    const std::string deathsText = snapshot.deaths
                                                       ? std::to_string(*snapshot.deaths)
                                                       : std::string(constants::kUnknownDeathsText);
                    const std::string detailsText = snapshot.difficulty;
                    const std::string stateText = std::format(
                        "lvl-{} 💀-{} {}",
                        snapshot.level,
                        deathsText,
                        snapshot.stone);
                    const std::string_view smallImage = snapshot.inCombat
                                                            ? std::string_view(config_.combatImage)
                                                            : std::string_view{};
                    const std::string_view smallText = snapshot.inCombat
                                                           ? std::string_view(snapshot.combatText)
                                                           : std::string_view{};
                    const core::LocationAssetResolver assetResolver(config_);
                    const auto largeAsset = assetResolver.Resolve(snapshot.location);

                    lastCombatState_ = snapshot.inCombat;
                    const bool activityChanged = presenceClient_.UpdateActivity({
                        detailsText,
                        stateText,
                        largeAsset.image,
                        largeAsset.text,
                        smallImage,
                        smallText,
                    });
                    if (!presenceClient_.IsActive())
                    {
                        Stop();
                        return;
                    }
                    if (!activityChanged)
                        return;

                    SKSE::log::info(
                        "[{}] level={} deaths={} stone='{}' difficulty='{}' location='{}' "
                        "large='{}' combat='{}'.",
                        core::ToLogLabel(reason),
                        snapshot.level,
                        deathsText,
                        snapshot.stone,
                        snapshot.difficulty,
                        snapshot.location.displayName,
                        largeAsset.image,
                        snapshot.combatText);
                }

                /// Updates the loading state and publishes the corresponding presence.
                void SetLoading(bool isLoading)
                {
                    loading_ = isLoading;
                    if (isLoading)
                    {
                        SendLoadingPresence();
                    }
                    else if (gameLoaded_)
                    {
                        RefreshPresence(core::RefreshReason::kLoadingFinished);
                    }
                }

                /// Starts the detached Discord callback and 500-millisecond polling loop once.
                void StartCallbackThread()
                {
                    if (callbackThread_.joinable())
                        return;

                    callbackThread_ = std::jthread([this](std::stop_token stopToken) noexcept
                                                   {
            try {
                while (!stopToken.stop_requested()) {
                    std::this_thread::sleep_for(constants::kDiscordCallbackInterval);
                    if (stopToken.stop_requested()) break;

                    if (callbackTaskPending_.exchange(true)) {
                        const auto pendingTicks =
                            callbackTaskPendingTicks_.fetch_add(1) + 1;
                        const bool firstWarning =
                            pendingTicks == constants::kPendingTaskWarningTicks;
                        const bool repeatedWarning =
                            pendingTicks > constants::kPendingTaskWarningTicks &&
                            (pendingTicks - constants::kPendingTaskWarningTicks) %
                                    constants::kPendingTaskWarningRepeatTicks ==
                                0;
                        if (firstWarning || repeatedWarning) {
                            SKSE::log::warn(
                                "Discord task 'RunCallbacks/RefreshPresence' has "
                                "been waiting on Skyrim's main thread for about "
                                "{} ms; {} callback request{} coalesced. Only one "
                                "task is queued, so memory use remains bounded.",
                                pendingTicks *
                                    constants::kDiscordCallbackInterval.count(),
                                pendingTicks,
                                pendingTicks == 1 ? "" : "s");
                        }
                        continue;
                    }
                    callbackTaskPendingTicks_ = 0;

                    if (!taskInterface_) {
                        HandleBackgroundException(
                            "SKSE TaskInterface became unavailable");
                        return;
                    }
                    taskInterface_->AddTask([this]() noexcept {
                        callbackTaskPendingTicks_ = 0;
                        callbackTaskPending_ = false;
                        if (!active_) return;

                        try {
                            if (!presenceClient_.RunCallbacks())
                            {
                                Stop();
                                return;
                            }
                            if (++presencePollTicks_ >=
                                constants::kPresencePollIntervalInCallbackTicks) {
                                presencePollTicks_ = 0;
                                if (gameLoaded_ && !loading_) {
                                    const auto reason =
                                        combatRefreshRequested_.exchange(false)
                                        ? core::RefreshReason::kCombat
                                        : core::RefreshReason::kPoll;
                                    RefreshPresence(reason);
                                } else {
                                    // Retry the loading activity after an earlier
                                    // in-flight update completes, without queuing
                                    // more than one Discord callback.
                                    SendLoadingPresence();
                                }
                            }
                        } catch (const std::exception& error) {
                            HandleException(
                                "Discord main-thread task",
                                error.what());
                        } catch (...) {
                            HandleUnknownException("Discord main-thread task");
                        }
                    });
                }
            } catch (const std::exception& error) {
                HandleBackgroundException(error.what());
            } catch (...) {
                HandleBackgroundException(
                    "unknown exception in the scheduler thread");
            } });
                }

                /// Stops producer work without destroying the SDK from the background thread.
                void HandleBackgroundException(const char *details) noexcept
                {
                    active_ = false;
                    callbackThread_.request_stop();
                    permanentlyStopped_ = true;
                    try
                    {
                        SKSE::log::critical(
                            "DragonbornPresence scheduler stopped: {} The Discord SDK core "
                            "will remain idle and be released after the scheduler thread joins; "
                            "Skyrim can continue normally.",
                            details ? details : "unknown scheduler error");
                    }
                    catch (...)
                    {
                    }
                }
                /// Required configuration dependency owned by the composition root.
                ::DragonbornPresence::application::ports::IConfigProvider &configProvider_;

                /// Required game-data dependency owned by the composition root.
                ///
                /// The interface prevents coordinator code from accessing RE::* objects and
                /// exposes only core-owned snapshots.
                ::DragonbornPresence::application::ports::IGameDataSource &gameDataSource_;

                /// Required Presence transport owned by the composition root.
                ///
                /// The application coordinator publishes core payloads without depending on
                /// Discord SDK types or the concrete transport implementation.
                ::DragonbornPresence::application::ports::IPresenceClient &presenceClient_;
                core::Config config_;
                MenuEventSink menuEventSink_;
                CombatEventSink combatEventSink_;
                const SKSE::TaskInterface *taskInterface_ = nullptr;
                std::atomic<bool> callbackTaskPending_{false};
                std::atomic<std::uint32_t> callbackTaskPendingTicks_{0};
                std::atomic<bool> combatRefreshRequested_{false};
                std::atomic<bool> active_{false};
                std::atomic<bool> permanentlyStopped_{false};
                std::uint8_t presencePollTicks_ = 0;
                bool loading_ = true;
                bool gameLoaded_ = false;
                bool lastCombatState_ = false;
                std::jthread callbackThread_;
            };

            /// Forwards menu events without allowing C++ exceptions to escape into Skyrim.
            RE::BSEventNotifyControl MenuEventSink::ProcessEvent(
                const RE::MenuOpenCloseEvent *event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent> *)
            {
                try
                {
                    if (event)
                        coordinator_.HandleMenuEvent(*event);
                }
                catch (const std::exception &error)
                {
                    coordinator_.HandleException("menu event handler", error.what());
                }
                catch (...)
                {
                    coordinator_.HandleUnknownException("menu event handler");
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            /// Forwards combat events without allowing C++ exceptions to escape into Skyrim.
            RE::BSEventNotifyControl CombatEventSink::ProcessEvent(
                const RE::TESCombatEvent *event,
                RE::BSTEventSource<RE::TESCombatEvent> *)
            {
                try
                {
                    if (event)
                        coordinator_.HandleCombatEvent(*event);
                }
                catch (const std::exception &error)
                {
                    coordinator_.HandleException("combat event handler", error.what());
                }
                catch (...)
                {
                    coordinator_.HandleUnknownException("combat event handler");
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        } // namespace runtime

        /// Selects and owns concrete infrastructure adapters for the plugin lifetime.
        ///
        /// Dependencies are declared before PresenceCoordinator because it stores
        /// non-owning references to them. Destruction occurs in reverse order, so the
        /// coordinator is destroyed before every adapter it references.
        adapters::config::JsonConfigProvider g_configProvider;
        adapters::SkyrimTrueBeliever::StbGameDataSource g_gameDataSource;
        adapters::discord::DiscordPresenceClient g_presenceClient;

        runtime::PresenceCoordinator g_presenceCoordinator(
            g_configProvider,
            g_gameDataSource,
            g_presenceClient);

    } // namespace

    /// Loads configuration without allowing errors to escape into SKSE.
    void LoadConfig() noexcept
    {
        try
        {
            g_presenceCoordinator.LoadConfig();
        }
        catch (const std::exception &error)
        {
            g_presenceCoordinator.HandleException("configuration loading", error.what());
        }
        catch (...)
        {
            g_presenceCoordinator.HandleUnknownException("configuration loading");
        }
    }

    /// Initializes integrations without allowing errors to escape into SKSE.
    void RegisterGameEventHandlers() noexcept
    {
        try
        {
            g_presenceCoordinator.RegisterGameEventHandlers();
        }
        catch (const std::exception &error)
        {
            g_presenceCoordinator.HandleException(
                "game event registration",
                error.what());
        }
        catch (...)
        {
            g_presenceCoordinator.HandleUnknownException("game event registration");
        }
    }

    /// Publishes the first state without allowing errors to escape into SKSE.
    void OnGameLoaded() noexcept
    {
        try
        {
            g_presenceCoordinator.OnGameLoaded();
        }
        catch (const std::exception &error)
        {
            g_presenceCoordinator.HandleException("game-load handler", error.what());
        }
        catch (...)
        {
            g_presenceCoordinator.HandleUnknownException("game-load handler");
        }
    }

} // namespace DragonbornPresence
