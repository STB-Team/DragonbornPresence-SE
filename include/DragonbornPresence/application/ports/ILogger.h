#pragma once

#include <string_view>

namespace DragonbornPresence::application::ports
{

    /// Application output port for diagnostic logging.
    ///
    /// The application layer depends on this abstraction instead of SKSE,
    /// spdlog, a console, a file, or another concrete logging backend.
    /// Implementations must not allow logging failures to escape into the
    /// application or external game callbacks.
    class ILogger
    {
    public:
        /// Enables destruction through an interface pointer or reference.
        virtual ~ILogger() = default;

        /// Records an informational application event.
        virtual void Info(
            std::string_view message) noexcept = 0;

        /// Records a recoverable condition requiring attention.
        virtual void Warning(
            std::string_view message) noexcept = 0;

        /// Records an operation failure that disables part of the integration.
        virtual void Error(
            std::string_view message) noexcept = 0;

        /// Records a fatal integration failure while allowing Skyrim to continue.
        virtual void Critical(
            std::string_view message) noexcept = 0;
    };

} // namespace DragonbornPresence::application::ports