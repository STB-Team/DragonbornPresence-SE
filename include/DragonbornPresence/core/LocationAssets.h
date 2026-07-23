#pragma once

#include <string>
#include <string_view>

namespace DragonbornPresence::core
{

    struct LocationImageRule
    {
        std::string worldspace;
        std::string location;
        std::string cell;
        std::string match;
        std::string image;
        std::string text;
    };

    struct LargeAssetSelection
    {
        std::string_view image;
        std::string_view text;
    };

} // namespace DragonbornPresence::core