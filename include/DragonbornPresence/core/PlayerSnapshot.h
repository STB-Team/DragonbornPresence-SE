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
        std::string playerName;
        std::string god;
        std::string vampire;
        std::string werewolf;
        LocationContext location;
        std::string combatText;
        bool inCombat = false;
    };

} // namespace DragonbornPresence::core