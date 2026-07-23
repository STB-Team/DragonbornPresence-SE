#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace DragonbornPresence::core
{
    [[nodiscard]] bool IsValidUtf8(std::string_view value);
    [[nodiscard]] std::string LimitUtf8Bytes(std::string_view value, std::size_t maxBytes);
    [[nodiscard]] bool ContainsAsciiInsensitive(std::string_view value, std::string_view pattern) noexcept;
} // namespace DragonbornPresence::core
