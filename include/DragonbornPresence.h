#pragma once

namespace DragonbornPresence
{

    /// Loads plugin configuration.
    void LoadConfig() noexcept;

    /// Initializes game event handlers and runtime processing.
    void RegisterGameEventHandlers() noexcept;

    /// Publishes the first complete game state.
    void OnGameLoaded() noexcept;

} // namespace DragonbornPresence