#pragma once

#include <string>

namespace DragonbornPresence::adapters::discord
{

    /// Verifies that Discord Desktop is installed and currently running.
    ///
    /// The function does not install, repair, or launch Discord. A diagnostic
    /// explanation is copied into failureReason when the check fails.
    [[nodiscard]] bool IsDiscordRunning(
        std::string *failureReason = nullptr) noexcept;

    /// Loads the bundled Discord Game SDK from the plugin directory.
    ///
    /// The function never downloads or installs the SDK. A diagnostic
    /// explanation is copied into failureReason when loading fails.
    [[nodiscard]] bool LoadDiscordSdk(
        std::string *failureReason = nullptr) noexcept;

} // namespace DragonbornPresence::adapters::discord