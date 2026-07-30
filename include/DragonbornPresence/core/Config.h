#pragma once

#include "DragonbornPresence/core/LocationAssets.h"

#include <cstdint>
#include <string>
#include <vector>

namespace DragonbornPresence::core
{

    using ApplicationId = std::int64_t;

    inline constexpr ApplicationId kDefaultApplicationId = 1527543892151373937;
    inline constexpr std::int64_t kSupportedConfigSchemaVersion = 1;

    struct Config
    {
        bool enabled = true;
        ApplicationId applicationId = kDefaultApplicationId;

        std::string largeImage = "stb_logo";
        std::string largeText = "Skyrim True Believer";

        std::string loadingImage = "loading";
        std::string combatImage = "combat";

        std::vector<LocationImageRule> locationImageRules;
    };

} // namespace DragonbornPresence::core