#pragma once

#include <optional>
#include <string_view>

namespace DragonbornPresence::core
{
    enum class Difficulty : int
    {
        kAdventure = 0,
        kTactics = 1,
        kHeroic = 2,
        kTrialOfTheGods = 3,
        kCustom = 4,
    };

    [[nodiscard]] std::string_view DifficultyName(std::optional<int> rawDifficulty) noexcept;
}

