#include "DragonbornPresence/adapters/SkyrimTrueBeliever/StbRuntimeAdapter.h"

#include "DragonbornPresence/application/PresenceCoordinator.h"
#include "DragonbornPresence/application/ports/ILogger.h"

#include <chrono>
#include <exception>
#include <format>
#include <string_view>

namespace DragonbornPresence::adapters::SkyrimTrueBeliever
{

    namespace
    {

        constexpr std::chrono::milliseconds
            kDiscordCallbackInterval{500};

        constexpr std::uint32_t
            kPendingTaskWarningTicks = 1;

        constexpr std::uint32_t
            kPendingTaskWarningRepeatTicks = 10;

        constexpr std::string_view
            kMainMenuName = "Main Menu";

        constexpr std::string_view
            kLoadingMenuName = "Loading Menu";

    } // namespace

    StbRuntimeAdapter::StbRuntimeAdapter(
        application::PresenceCoordinator &coordinator,
        application::ports::ILogger &logger) noexcept
        : coordinator_(coordinator),
          logger_(logger),
          menuEventSink_(*this),
          combatEventSink_(*this)
    {
    }

    /// Initializes required Skyrim services and starts application processing.
    void StbRuntimeAdapter::RegisterGameEventHandlers()
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
    void StbRuntimeAdapter::OnGameLoaded()
    {
        coordinator_.OnGameLoaded();
    }

    /// Translates a Skyrim menu event into engine-independent signals.
    void StbRuntimeAdapter::HandleMenuEvent(
        const RE::MenuOpenCloseEvent &event)
    {
        if (event.menuName ==
                kMainMenuName &&
            event.opening)
        {
            coordinator_.OnMainMenuOpened();
        }
        else if (
            event.menuName ==
            kLoadingMenuName)
        {
            coordinator_.OnLoadingChanged(
                event.opening);
        }
    }

    /// Translates a Skyrim combat event into plain booleans.
    void StbRuntimeAdapter::HandleCombatEvent(
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
    void StbRuntimeAdapter::HandleException(
        std::string_view context,
        const char *details) noexcept
    {
        callbackThread_.request_stop();
        coordinator_.HandleException(
            context,
            details);
    }

    void StbRuntimeAdapter::HandleUnknownException(
        std::string_view context) noexcept
    {
        HandleException(
            context,
            "unknown non-standard C++ exception");
    }

    void StbRuntimeAdapter::StartCallbackThread()
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
                        std::this_thread::sleep_for(kDiscordCallbackInterval);

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
                                kPendingTaskWarningTicks;

                            const bool repeatedWarning =
                                pendingTicks >
                                    kPendingTaskWarningTicks &&
                                (pendingTicks -
                                 kPendingTaskWarningTicks) %
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
    void StbRuntimeAdapter::HandleBackgroundException(
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

    RE::BSEventNotifyControl
    StbRuntimeAdapter::MenuEventSink::ProcessEvent(
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
            runtimeAdapter_.HandleException(
                "menu event handler",
                error.what());
        }
        catch (...)
        {
            runtimeAdapter_.HandleUnknownException(
                "menu event handler");
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl
    StbRuntimeAdapter::CombatEventSink::ProcessEvent(
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
            runtimeAdapter_.HandleException(
                "combat event handler",
                error.what());
        }
        catch (...)
        {
            runtimeAdapter_.HandleUnknownException(
                "combat event handler");
        }

        return RE::BSEventNotifyControl::kContinue;
    }
} // namespace DragonbornPresence::adapters::SkyrimTrueBeliever