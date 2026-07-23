#include "DragonbornPresence/core/Difficulty.h"

namespace DragonbornPresence::core
{

    std::string_view DifficultyName(
        std::optional<int> rawDifficulty) noexcept
    {
        if (!rawDifficulty)
            return "не определена";

        switch (static_cast<Difficulty>(*rawDifficulty))
        {
        case Difficulty::kAdventure:
            return "🟢Приключение";

        case Difficulty::kTactics:
            return "🟡Тактика";

        case Difficulty::kHeroic:
            return "🔴Героический";

        case Difficulty::kTrialOfTheGods:
            return "⚫Испытание богов";

        case Difficulty::kCustom:
            return "⚪Свой уровень сложности";

        default:
            return "не определена";
        }
    }

} // namespace DragonbornPresence::core