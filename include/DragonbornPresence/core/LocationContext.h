#pragma once

#include <string>
#include <vector>

namespace DragonbornPresence::core
{

    struct LocationContext
    {
        std::string worldspaceEditorId;
        std::string cellEditorId;
        std::vector<std::string> locationEditorIds;
        std::string displayName;
    };

} // namespace DragonbornPresence::core