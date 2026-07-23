#pragma once

#include "DragonbornPresence/core/LocationAssets.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace DragonbornPresence::core
{

    inline constexpr std::int64_t kSupportedConfigSchemaVersion = 1;

    inline constexpr std::string_view kDefaultDetailsTemplate = "{difficulty}";
    inline constexpr std::string_view kDefaultStateTemplate =
        "lvl-{lvl} {deaths} {stone}";
    inline constexpr std::string_view kLegacyDefaultStateTemplate =
        "lvl-{lvl} 💀-{deaths} {stone}";
    inline constexpr std::string_view kDefaultLargeTextTemplate = "{player}";
    inline constexpr std::string_view kDefaultCombatTextTemplate = "{combat}";

    struct Config
    {
        bool enabled = true;

        std::string largeImage = "stb_logo";
        std::string largeText = "Skyrim True Believer";

        std::string loadingImage = "loading";
        std::string combatImage = "combat";

        std::vector<LocationImageRule> locationImageRules;

        std::string detailsTemplate{kDefaultDetailsTemplate};
        std::string stateTemplate{kDefaultStateTemplate};
        std::string largeTextTemplate{kDefaultLargeTextTemplate};
        std::string combatTextTemplate{kDefaultCombatTextTemplate};
    };

} // namespace DragonbornPresence::core