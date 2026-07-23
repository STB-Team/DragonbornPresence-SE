#pragma once

#include "DragonbornPresence/application/ports/ILogger.h"

#include <string_view>

namespace DragonbornPresence::adapters::SkyrimTrueBeliever
{

    /// SKSE implementation of the application logging output port.
    ///
    /// Every operation suppresses backend exceptions so logging failures cannot
    /// escape into application code or external Skyrim callbacks.
    class SkseLogger final
        : public ::DragonbornPresence::application::ports::ILogger
    {
    public:
        /// Writes an informational message through the SKSE logger.
        void Info(
            std::string_view message) noexcept override;

        /// Writes a warning through the SKSE logger.
        void Warning(
            std::string_view message) noexcept override;

        /// Writes an error through the SKSE logger.
        void Error(
            std::string_view message) noexcept override;

        /// Writes a critical error through the SKSE logger.
        void Critical(
            std::string_view message) noexcept override;
    };

} // namespace DragonbornPresence::adapters::SkyrimTrueBeliever