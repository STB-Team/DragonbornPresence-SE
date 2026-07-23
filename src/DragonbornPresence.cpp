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
#include "DragonbornPresence/application/ports/ILogger.h"
#include "DragonbornPresence/adapters/SkyrimTrueBeliever/SkseLogger.h"

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

        namespace application_constants
        {

            constexpr std::uint8_t kPresencePollIntervalInCallbackTicks = 1;
            constexpr std::string_view kLoadingText = "Загрузка";
            constexpr std::string_view kUnknownDeathsText = "—";

        } // namespace application_constants

        namespace runtime_constants
        {

            constexpr std::chrono::milliseconds kDiscordCallbackInterval{500};
            constexpr std::uint32_t kPendingTaskWarningTicks = 1;
            constexpr std::uint32_t kPendingTaskWarningRepeatTicks = 10;
            constexpr std::string_view kMainMenuName = "Main Menu";
            constexpr std::string_view kLoadingMenuName = "Loading Menu";

        } // namespace runtime_constants

        namespace runtime
        {

            class StbRuntimeAdapter;

            class MenuEventSink final
                : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
            {
            public:
                explicit MenuEventSink(
                    StbRuntimeAdapter &runtimeAdapter) noexcept
                    : runtimeAdapter_(runtimeAdapter)
                {
                }

                RE::BSEventNotifyControl ProcessEvent(
                    const RE::MenuOpenCloseEvent *event,
                    RE::BSTEventSource<RE::MenuOpenCloseEvent> *) override;

            private:
                StbRuntimeAdapter &runtimeAdapter_;
            };

            class CombatEventSink final
                : public RE::BSTEventSink<RE::TESCombatEvent>
            {
            public:
                explicit CombatEventSink(
                    StbRuntimeAdapter &runtimeAdapter) noexcept
                    : runtimeAdapter_(runtimeAdapter)
                {
                }

                RE::BSEventNotifyControl ProcessEvent(
                    const RE::TESCombatEvent *event,
                    RE::BSTEventSource<RE::TESCombatEvent> *) override;

            private:
                StbRuntimeAdapter &runtimeAdapter_;
            };

            /// Coordinates configuration, game snapshots, Presence, and state transitions.
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
                    ::DragonbornPresence::application::ports::IPresenceClient &presenceClient,
                    ::DragonbornPresence::application::ports::ILogger &logger) noexcept
                    : configProvider_(configProvider),
                      gameDataSource_(gameDataSource),
                      presenceClient_(presenceClient),
                      logger_(logger)
                {
                }

                /// Stops all plugin work after an exception reaches an external game callback.
                void HandleException(
                    std::string_view context,
                    const char *details) noexcept
                {
                    active_ = false;
                    permanentlyStopped_ = true;

                    try
                    {
                        logger_.Critical(std::format(
                            "DragonbornPresence exception in '{}': {} The integration was "
                            "stopped; Skyrim can continue normally.",
                            context,
                            details
                                ? details
                                : "unknown exception"));
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

                /// Initializes game data and the Presence transport.
                ///
                /// Returns true only when the application is ready to receive game signals
                /// and periodic ticks.
                [[nodiscard]] bool Start()
                {
                    if (permanentlyStopped_)
                    {
                        logger_.Error(
                            "DragonbornPresence registration was skipped because an earlier "
                            "fatal error permanently stopped this plugin instance.");

                        return false;
                    }

                    if (!presenceClient_.Initialize(config_))
                        return false;

                    active_ = true;
                    gameDataSource_.Initialize();
                    SendLoadingPresence();

                    return active_;
                }

                /// Stops future application work without directly controlling runtime threads.
                void Stop() noexcept
                {
                    active_ = false;
                    permanentlyStopped_ = true;
                }

                /// Marks the game ready and immediately publishes the complete player state.
                void OnGameLoaded()
                {
                    if (!active_)
                        return;

                    logger_.Info(
                        "Game loaded — refreshing STB presence data.");

                    gameLoaded_ = true;
                    loading_ = false;
                    RefreshPresence(core::RefreshReason::kGameLoaded);
                }

                /// Marks the current playable game as unavailable.
                void OnMainMenuOpened()
                {
                    if (!active_)
                        return;

                    gameLoaded_ = false;
                    SetLoading(true);
                }

                /// Applies a loading-menu transition without depending on Skyrim event types.
                void OnLoadingChanged(bool isLoading)
                {
                    if (!active_)
                        return;

                    if (isLoading)
                    {
                        SetLoading(true);
                    }
                    else if (gameLoaded_)
                    {
                        SetLoading(false);
                    }
                }

                /// Coalesces an engine-independent combat signal into the next tick.
                void RequestCombatRefresh(
                    bool involvesPlayer,
                    bool combatEnded)
                {
                    if (!active_ || !gameLoaded_ || loading_)
                        return;

                    const bool mayEndCombat =
                        combatEnded && lastCombatState_;

                    if (involvesPlayer || mayEndCombat)
                    {
                        combatRefreshRequested_ = true;
                    }
                }

                /// Processes transport callbacks and publishes the next Presence state.
                ///
                /// Must be invoked on Skyrim's main thread. Returns false after the
                /// application permanently stops and no further ticks should be scheduled.
                [[nodiscard]] bool Tick()
                {
                    if (!active_)
                        return false;

                    if (!presenceClient_.RunCallbacks())
                    {
                        Stop();
                        return false;
                    }

                    if (++presencePollTicks_ >=
                        application_constants::
                            kPresencePollIntervalInCallbackTicks)
                    {
                        presencePollTicks_ = 0;

                        if (gameLoaded_ && !loading_)
                        {
                            const auto reason =
                                combatRefreshRequested_.exchange(false)
                                    ? core::RefreshReason::kCombat
                                    : core::RefreshReason::kPoll;

                            RefreshPresence(reason);
                        }
                        else
                        {
                            // Retry the loading activity after an earlier in-flight update
                            // completes without queuing multiple callbacks.
                            SendLoadingPresence();
                        }
                    }

                    return active_;
                }

            private:
                /// Sends the stable loading activity used before a playable save is ready.
                void SendLoadingPresence()
                {
                    presenceClient_.UpdateActivity({
                        {},
                        application_constants::kLoadingText,
                        config_.largeImage,
                        config_.largeText,
                        config_.loadingImage,
                        application_constants::kLoadingText,
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
                                                       : std::string(application_constants::kUnknownDeathsText);
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

                    logger_.Info(std::format(
                        "[{}] level={} deaths={} stone='{}' difficulty='{}' location='{}' "
                        "large='{}' combat='{}'.",
                        core::ToLogLabel(reason),
                        snapshot.level,
                        deathsText,
                        snapshot.stone,
                        snapshot.difficulty,
                        snapshot.location.displayName,
                        largeAsset.image,
                        snapshot.combatText));
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

                /// Required application logger owned by the composition root.
                ///
                /// The coordinator reports diagnostics without depending on SKSE or another
                /// concrete logging backend.
                ::DragonbornPresence::application::ports::ILogger &logger_;

                core::Config config_;
                std::atomic<bool> combatRefreshRequested_{false};
                std::atomic<bool> active_{false};
                std::atomic<bool> permanentlyStopped_{false};
                std::uint8_t presencePollTicks_ = 0;
                bool loading_ = true;
                bool gameLoaded_ = false;
                bool lastCombatState_ = false;
            };

            /// Adapts Skyrim services, events, and scheduling to application operations.
            class StbRuntimeAdapter final
            {
            public:
                StbRuntimeAdapter(
                    PresenceCoordinator &coordinator,
                    ::DragonbornPresence::application::ports::ILogger &logger) noexcept
                    : coordinator_(coordinator),
                      logger_(logger),
                      menuEventSink_(*this),
                      combatEventSink_(*this)
                {
                }

                /// Initializes required Skyrim services and starts application processing.
                void RegisterGameEventHandlers()
                {
                    logger_.Info(
                        "Registering game event handlers...");

                    auto *ui = RE::UI::GetSingleton();
                    auto *eventSource =
                        RE::ScriptEventSourceHolder::GetSingleton();

                    taskInterface_ = SKSE::GetTaskInterface();

                    if (!ui || !eventSource || !taskInterface_)
                    {
                        logger_.Critical(std::format(
                            "DragonbornPresence cannot start: UI={}, "
                            "ScriptEventSourceHolder={}, SKSE TaskInterface={}. "
                            "At least one required Skyrim/SKSE service is unavailable. "
                            "No event sinks or background tasks were registered.",
                            ui != nullptr,
                            eventSource != nullptr,
                            taskInterface_ != nullptr));

                        return;
                    }

                    if (!coordinator_.Start())
                        return;

                    ui->AddEventSink<RE::MenuOpenCloseEvent>(
                        &menuEventSink_);

                    eventSource->AddEventSink<RE::TESCombatEvent>(
                        &combatEventSink_);

                    StartCallbackThread();
                }

                /// Forwards the game-loaded lifecycle signal to the application.
                void OnGameLoaded()
                {
                    coordinator_.OnGameLoaded();
                }

                /// Translates a Skyrim menu event into engine-independent signals.
                void HandleMenuEvent(
                    const RE::MenuOpenCloseEvent &event)
                {
                    if (event.menuName ==
                            runtime_constants::kMainMenuName &&
                        event.opening)
                    {
                        coordinator_.OnMainMenuOpened();
                    }
                    else if (
                        event.menuName ==
                        runtime_constants::kLoadingMenuName)
                    {
                        coordinator_.OnLoadingChanged(
                            event.opening);
                    }
                }

                /// Translates a Skyrim combat event into plain booleans.
                void HandleCombatEvent(
                    const RE::TESCombatEvent &event)
                {
                    const bool involvesPlayer =
                        (event.actor &&
                         event.actor->IsPlayerRef()) ||
                        (event.targetActor &&
                         event.targetActor->IsPlayerRef());

                    const bool combatEnded =
                        event.newState.get() ==
                        RE::ACTOR_COMBAT_STATE::kNone;

                    coordinator_.RequestCombatRefresh(
                        involvesPlayer,
                        combatEnded);
                }

                /// Stops runtime production and application work after callback failure.
                void HandleException(
                    std::string_view context,
                    const char *details) noexcept
                {
                    callbackThread_.request_stop();
                    coordinator_.HandleException(
                        context,
                        details);
                }

                void HandleUnknownException(
                    std::string_view context) noexcept
                {
                    HandleException(
                        context,
                        "unknown non-standard C++ exception");
                }

            private:
                void StartCallbackThread()
                {
                    if (callbackThread_.joinable())
                        return;

                    callbackThread_ = std::jthread(
                        [this](std::stop_token stopToken) noexcept
                        {
                            try
                            {
                                while (!stopToken.stop_requested())
                                {
                                    std::this_thread::sleep_for(
                                        runtime_constants::
                                            kDiscordCallbackInterval);

                                    if (stopToken.stop_requested())
                                        break;

                                    if (callbackTaskPending_.exchange(true))
                                    {
                                        const auto pendingTicks =
                                            callbackTaskPendingTicks_
                                                .fetch_add(1) +
                                            1;

                                        const bool firstWarning =
                                            pendingTicks ==
                                            runtime_constants::
                                                kPendingTaskWarningTicks;

                                        const bool repeatedWarning =
                                            pendingTicks >
                                                runtime_constants::
                                                    kPendingTaskWarningTicks &&
                                            (pendingTicks -
                                             runtime_constants::
                                                 kPendingTaskWarningTicks) %
                                                    runtime_constants::
                                                        kPendingTaskWarningRepeatTicks ==
                                                0;

                                        if (firstWarning ||
                                            repeatedWarning)
                                        {
                                            logger_.Warning(std::format(
                                                "Discord task "
                                                "'RunCallbacks/RefreshPresence' has "
                                                "been waiting on Skyrim's main thread "
                                                "for about {} ms; {} callback request{} "
                                                "coalesced. Only one task is queued, so "
                                                "memory use remains bounded.",
                                                pendingTicks *
                                                    runtime_constants::
                                                        kDiscordCallbackInterval
                                                            .count(),
                                                pendingTicks,
                                                pendingTicks == 1
                                                    ? ""
                                                    : "s"));
                                        }

                                        continue;
                                    }

                                    callbackTaskPendingTicks_ = 0;

                                    if (!taskInterface_)
                                    {
                                        HandleBackgroundException(
                                            "SKSE TaskInterface became unavailable");
                                        return;
                                    }

                                    taskInterface_->AddTask(
                                        [this]() noexcept
                                        {
                                            callbackTaskPendingTicks_ = 0;
                                            callbackTaskPending_ = false;

                                            try
                                            {
                                                if (!coordinator_.Tick())
                                                {
                                                    callbackThread_.request_stop();
                                                }
                                            }
                                            catch (
                                                const std::exception &error)
                                            {
                                                HandleException(
                                                    "Discord main-thread task",
                                                    error.what());
                                            }
                                            catch (...)
                                            {
                                                HandleUnknownException(
                                                    "Discord main-thread task");
                                            }
                                        });
                                }
                            }
                            catch (const std::exception &error)
                            {
                                HandleBackgroundException(
                                    error.what());
                            }
                            catch (...)
                            {
                                HandleBackgroundException(
                                    "unknown exception in the scheduler thread");
                            }
                        });
                }

                /// Stops producer work without destroying Discord core on this thread.
                void HandleBackgroundException(
                    const char *details) noexcept
                {
                    callbackThread_.request_stop();
                    coordinator_.Stop();

                    try
                    {
                        logger_.Critical(std::format(
                            "DragonbornPresence scheduler stopped: {} "
                            "The Discord SDK core will remain idle and be released "
                            "after the scheduler thread joins; Skyrim can continue "
                            "normally.",
                            details
                                ? details
                                : "unknown scheduler error"));
                    }
                    catch (...)
                    {
                    }
                }

                PresenceCoordinator &coordinator_;
                ::DragonbornPresence::application::ports::ILogger &logger_;

                MenuEventSink menuEventSink_;
                CombatEventSink combatEventSink_;

                const SKSE::TaskInterface *taskInterface_ = nullptr;
                std::atomic<bool> callbackTaskPending_{false};
                std::atomic<std::uint32_t>
                    callbackTaskPendingTicks_{0};
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
                        runtimeAdapter_.HandleMenuEvent(*event);
                }
                catch (const std::exception &error)
                {
                    runtimeAdapter_.HandleException("menu event handler", error.what());
                }
                catch (...)
                {
                    runtimeAdapter_.HandleUnknownException("menu event handler");
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
                        runtimeAdapter_.HandleCombatEvent(*event);
                }
                catch (const std::exception &error)
                {
                    runtimeAdapter_.HandleException("combat event handler", error.what());
                }
                catch (...)
                {
                    runtimeAdapter_.HandleUnknownException("combat event handler");
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        } // namespace runtime

        /// Selects and owns concrete infrastructure adapters for the plugin lifetime.
        ///
        /// The runtime adapter is declared last so its scheduler thread is stopped and
        /// joined before the coordinator and the coordinator's dependencies are destroyed.
        adapters::config::JsonConfigProvider g_configProvider;
        adapters::SkyrimTrueBeliever::StbGameDataSource g_gameDataSource;
        adapters::discord::DiscordPresenceClient g_presenceClient;
        adapters::SkyrimTrueBeliever::SkseLogger g_logger;

        runtime::PresenceCoordinator g_presenceCoordinator(
            g_configProvider,
            g_gameDataSource,
            g_presenceClient,
            g_logger);

        runtime::StbRuntimeAdapter g_runtimeAdapter(
            g_presenceCoordinator,
            g_logger);

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
            g_runtimeAdapter.RegisterGameEventHandlers();
        }
        catch (const std::exception &error)
        {
            g_runtimeAdapter.HandleException(
                "game event registration",
                error.what());
        }
        catch (...)
        {
            g_runtimeAdapter.HandleUnknownException("game event registration");
        }
    }

    /// Publishes the first state without allowing errors to escape into SKSE.
    void OnGameLoaded() noexcept
    {
        try
        {
            g_runtimeAdapter.OnGameLoaded();
        }
        catch (const std::exception &error)
        {
            g_runtimeAdapter.HandleException("game-load handler", error.what());
        }
        catch (...)
        {
            g_runtimeAdapter.HandleUnknownException("game-load handler");
        }
    }

} // namespace DragonbornPresence
