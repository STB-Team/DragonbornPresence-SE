#pragma once

#include "DragonbornPresence/application/ports/IConfigProvider.h"
#include "DragonbornPresence/application/ports/IGameDataSource.h"
#include "DragonbornPresence/application/ports/ILogger.h"
#include "DragonbornPresence/application/ports/IPresenceClient.h"
#include "DragonbornPresence/core/Config.h"
#include "DragonbornPresence/core/PlayerSnapshot.h"
#include "DragonbornPresence/core/RefreshReason.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace DragonbornPresence::application
{

    /// Coordinates configuration, game snapshots, Presence, and state transitions.
    ///
    /// The coordinator depends only on core models and application ports. Skyrim
    /// events, SKSE services, background threads, and Discord SDK types remain in
    /// outer adapters.
    class PresenceCoordinator final
    {
    public:
        /// Stores required dependencies as non-owning references.
        PresenceCoordinator(
            ports::IConfigProvider &configProvider,
            ports::IGameDataSource &gameDataSource,
            ports::IPresenceClient &presenceClient,
            ports::ILogger &logger) noexcept;

        /// Permanently stops application work after an external callback error.
        void HandleException(
            std::string_view context,
            const char *details) noexcept;

        /// Handles an unknown non-standard C++ exception.
        void HandleUnknownException(
            std::string_view context) noexcept;

        /// Replaces the active configuration with provider output.
        void LoadConfig();

        /// Initializes game data and starts runtime processing.
        ///
        /// A disabled or unavailable Presence transport does not prevent Skyrim
        /// event registration; an explicit configuration change may enable it later.
        [[nodiscard]] bool Start();

        /// Stops future application work without controlling runtime threads.
        void Stop() noexcept;

        /// Publishes the first complete player state.
        void OnGameLoaded();

        /// Marks the current playable game as unavailable.
        void OnMainMenuOpened();

        /// Applies an engine-independent loading transition.
        void OnLoadingChanged(bool isLoading);

        /// Coalesces an engine-independent combat signal into the next tick.
        void RequestCombatRefresh(
            bool involvesPlayer,
            bool combatEnded);

        /// Checks runtime configuration, processes transport callbacks, and
        /// publishes the next Presence state.
        ///
        /// The runtime adapter must invoke this method on Skyrim's main thread.
        [[nodiscard]] bool Tick();

    private:
        /// Applies a changed runtime configuration and transport transition.
        void ApplyReloadedConfig(core::Config config);

        /// Starts one transport session without stopping runtime on failure.
        [[nodiscard]] bool StartTransport();

        /// Formats one user-configured Presence line from a player snapshot.
        [[nodiscard]] static std::string RenderPresenceTemplate(
            std::string_view presenceTemplate,
            const core::PlayerSnapshot &snapshot,
            std::string_view locationText = {});

        /// Sends the stable loading activity.
        void SendLoadingPresence();

        /// Builds and submits one activity from the latest player snapshot.
        void RefreshPresence(
            core::RefreshReason reason);

        /// Updates loading state and publishes the corresponding activity.
        void SetLoading(bool isLoading);

        ports::IConfigProvider &configProvider_;
        ports::IGameDataSource &gameDataSource_;
        ports::IPresenceClient &presenceClient_;
        ports::ILogger &logger_;

        core::Config config_;
        std::atomic<bool> combatRefreshRequested_{false};
        std::atomic<bool> active_{false};
        std::atomic<bool> permanentlyStopped_{false};
        std::uint8_t presencePollTicks_ = 0;
        bool loading_ = true;
        bool gameLoaded_ = false;
        bool lastCombatState_ = false;
    };

} // namespace DragonbornPresence::application