#include "DragonbornPresence/adapters/SkyrimTrueBeliever/SkseLogger.h"

#include <SKSE/SKSE.h>

namespace DragonbornPresence::adapters::SkyrimTrueBeliever
{

    void SkseLogger::Info(
        std::string_view message) noexcept
    {
        try
        {
            SKSE::log::info("{}", message);
        }
        catch (...)
        {
        }
    }

    void SkseLogger::Warning(
        std::string_view message) noexcept
    {
        try
        {
            SKSE::log::warn("{}", message);
        }
        catch (...)
        {
        }
    }

    void SkseLogger::Error(
        std::string_view message) noexcept
    {
        try
        {
            SKSE::log::error("{}", message);
        }
        catch (...)
        {
        }
    }

    void SkseLogger::Critical(
        std::string_view message) noexcept
    {
        try
        {
            SKSE::log::critical("{}", message);
        }
        catch (...)
        {
        }
    }

} // namespace DragonbornPresence::adapters::SkyrimTrueBeliever