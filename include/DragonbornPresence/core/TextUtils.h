#pragma once 

#include <string_view>

namespace DragonbornPresence::core {
    [[nodiscard]] bool IsValidUtf8(std::string_view value);
} // namespace DragonbornPresence::core
