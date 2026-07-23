#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <atomic>
#include <cstdint>
#include <string_view>
#include <thread>

namespace DragonbornPresence::application
{

    class PresenceCoordinator;

    namespace ports
    {

        class ILogger;

    } // namespace ports

} // namespace DragonbornPresence::application

namespace DragonbornPresence::adapters::SkyrimTrueBeliever
{

    /// Adapts Skyrim services, events, and scheduling to application operations.
    ///
    /// The adapter owns Skyrim event sinks and the scheduler thread. Skyrim
    /// objects are translated into engine-independent calls before crossing the
    /// application boundary.
    class StbRuntimeAdapter final
    {
    public:
        StbRuntimeAdapter(
            application::PresenceCoordinator &coordinator,
            application::ports::ILogger &logger) noexcept;

        /// Initializes required Skyrim services and starts application processing.
        void RegisterGameEventHandlers();

        /// Forwards the game-loaded lifecycle signal to the application.
        void OnGameLoaded();

        /// Stops runtime production and application work after callback failure.
        void HandleException(
            std::string_view context,
            const char *details) noexcept;

        /// Handles an unknown exception without crossing the Skyrim callback boundary.
        void HandleUnknownException(
            std::string_view context) noexcept;

    private:
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

        /// Translates a Skyrim menu event into engine-independent signals.
        void HandleMenuEvent(
            const RE::MenuOpenCloseEvent &event);

        /// Translates a Skyrim combat event into plain booleans.
        void HandleCombatEvent(
            const RE::TESCombatEvent &event);

        /// Starts the bounded scheduler that dispatches work to Skyrim's main thread.
        void StartCallbackThread();

        /// Stops producer work without destroying Discord resources on this thread.
        void HandleBackgroundException(
            const char *details) noexcept;

        application::PresenceCoordinator &coordinator_;
        application::ports::ILogger &logger_;

        MenuEventSink menuEventSink_;
        CombatEventSink combatEventSink_;

        const SKSE::TaskInterface *taskInterface_ = nullptr;
        std::atomic<bool> callbackTaskPending_{false};
        std::atomic<std::uint32_t> callbackTaskPendingTicks_{0};
        std::jthread callbackThread_;
    };


} // namespace DragonbornPresence::adapters::SkyrimTrueBeliever