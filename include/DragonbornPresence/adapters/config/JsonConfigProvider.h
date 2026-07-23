#pragma once

#include "DragonbornPresence/application/ports/IConfigProvider.h"

namespace DragonbornPresence::adapters::config
{

    /// Infrastructure adapter that loads the runtime JSON configuration.
    ///
    /// Filesystem access, JSON parsing, and SKSE diagnostics belong to this
    /// adapter. Callers receive only the core-owned Config model and therefore
    /// do not need to depend on nlohmann::json or know the configuration path.
    class JsonConfigProvider final
        : public ::DragonbornPresence::application::ports::IConfigProvider
    {
    public:
        /// Reads DragonbornPresence.json and returns a complete configuration.
        ///
        /// Missing and invalid optional fields preserve the defaults declared by
        /// core::Config. A malformed document produces a default configuration.
        [[nodiscard]] core::Config Load() override;
    };

} // namespace DragonbornPresence::adapters::config