#pragma once

#include "DragonbornPresence/core/Config.h"

namespace DragonbornPresence::application::ports
{

    /// Application port for obtaining the plugin configuration.
    ///
    /// The application layer depends on this abstraction instead of filesystem,
    /// JSON, SKSE, or a particular configuration adapter. Implementations may load
    /// the configuration from JSON, memory, tests, or another external source.
    class IConfigProvider
    {
    public:
        /// Enables destruction through an interface pointer or reference.
        virtual ~IConfigProvider() = default;

        /// Returns a complete core configuration for the current plugin instance.
        ///
        /// The returned value owns all strings and location rules. The application
        /// can therefore store it without depending on the provider's lifetime for
        /// the returned data.
        [[nodiscard]] virtual core::Config Load() = 0;
    };

} // namespace DragonbornPresence::application::ports