#pragma once

#include <string_view>

namespace DragonbornPresence::core
{

    enum class RefreshReason
    {
        kGameLoaded,
        kLoadingFinished,
        kCombat,
        kPoll,
    };

    [[nodiscard]] constexpr std::string_view ToLogLabel(
        RefreshReason reason) noexcept
    {
        switch (reason)
        {
        case RefreshReason::kGameLoaded:
            return "game-loaded";

        case RefreshReason::kLoadingFinished:
            return "loading-finished";

        case RefreshReason::kCombat:
            return "combat";

        case RefreshReason::kPoll:
            return "poll";
        }

        return "unknown";
    }

} // namespace DragonbornPresence::core