#include "DragonbornPresence.h"
#include "DragonbornPresence/adapters/config/JsonConfigProvider.h"
#include "DragonbornPresence/application/ports/IConfigProvider.h"
#include "DragonbornPresence/application/ports/IGameDataSource.h"
#include "DragonbornPresence/adapters/SkyrimTrueBeliever/StbGameDataSource.h"
#include "DragonbornPresence/application/ports/IPresenceClient.h"
#include "DragonbornPresence/adapters/discord/DiscordPresenceClient.h"
#include "DragonbornPresence/application/ports/ILogger.h"
#include "DragonbornPresence/adapters/SkyrimTrueBeliever/SkseLogger.h"
#include "DragonbornPresence/application/PresenceCoordinator.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <format>
#include <string_view>
#include <thread>

namespace DragonbornPresence
{

    namespace
    {

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

            /// Adapts Skyrim services, events, and scheduling to application operations.
            class StbRuntimeAdapter final
            {
            public:
                StbRuntimeAdapter(
                    ::DragonbornPresence::application::PresenceCoordinator &coordinator,
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

                ::DragonbornPresence::application::PresenceCoordinator &coordinator_;
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

        application::PresenceCoordinator g_presenceCoordinator(
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
