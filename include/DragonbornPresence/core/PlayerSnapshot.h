#pragma once

#include "DragonbornPresence/core/LocationContext.h"
#include <optional>
#include <string>

namespace DragonbornPresence::core
{

    struct PlayerSnapshot
    {
        int level = 0;
        std::optional<int> deaths;
        std::string stone;
        std::string difficulty;
        LocationContext location;
        std::string combatText;
        std::string PlayerName;
        bool inCombat = false;
    };

} // namespace DragonbornPresence::core