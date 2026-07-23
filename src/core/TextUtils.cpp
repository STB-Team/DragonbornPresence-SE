#include "DragonbornPresence/core/TextUtils.h"
#include <cstdint>

namespace DragonbornPresence::core
{
    bool IsValidUtf8(std::string_view sv)
    {
        auto *bytes = reinterpret_cast<const unsigned char *>(sv.data());
        auto *end = bytes + sv.size();

        while (bytes < end)
        {
            std::uint32_t cp;
            int num;

            if ((*bytes & 0x80) == 0x00)
            {
                cp = *bytes & 0x7F;
                num = 1;
            }
            else if ((*bytes & 0xE0) == 0xC0)
            {
                cp = *bytes & 0x1F;
                num = 2;
            }
            else if ((*bytes & 0xF0) == 0xE0)
            {
                cp = *bytes & 0x0F;
                num = 3;
            }
            else if ((*bytes & 0xF8) == 0xF0)
            {
                cp = *bytes & 0x07;
                num = 4;
            }
            else
                return false;

            ++bytes;
            for (int i = 1; i < num; ++i)
            {
                if (bytes >= end || (*bytes & 0xC0) != 0x80)
                    return false;
                cp = (cp << 6) | (*bytes & 0x3F);
                ++bytes;
            }

            if (cp > 0x10FFFF ||
                (cp >= 0xD800 && cp <= 0xDFFF) ||
                (cp <= 0x7F && num != 1) ||
                (cp >= 0x80 && cp <= 0x7FF && num != 2) ||
                (cp >= 0x800 && cp <= 0xFFFF && num != 3) ||
                (cp >= 0x10000 && num != 4))
                return false;
        }
        return true;
    }

    /// Truncates text at a UTF-8 code-point boundary.
    std::string LimitUtf8Bytes(std::string_view value, std::size_t maxBytes)
    {
        if (value.size() <= maxBytes)
        {
            return std::string(value);
        }

        std::size_t validSize = maxBytes;
        while (validSize > 0 &&
               (static_cast<unsigned char>(value[validSize]) & 0xC0) == 0x80)
        {
            --validSize;
        }

        return std::string(value.substr(0, validSize));
    }

    /// Tests whether an ASCII pattern occurs in text using case-insensitive comparison.
    [[nodiscard]] bool ContainsAsciiInsensitive(
        std::string_view value,
        std::string_view pattern) noexcept
    {
        if (pattern.empty())
            return true;
        if (pattern.size() > value.size())
            return false;

        const auto foldAscii = [](unsigned char character)
        {
            return character >= 'A' && character <= 'Z'
                       ? static_cast<unsigned char>(character + ('a' - 'A'))
                       : character;
        };

        for (std::size_t offset = 0; offset + pattern.size() <= value.size(); ++offset)
        {
            bool matches = true;
            for (std::size_t index = 0; index < pattern.size(); ++index)
            {
                if (foldAscii(static_cast<unsigned char>(value[offset + index])) !=
                    foldAscii(static_cast<unsigned char>(pattern[index])))
                {
                    matches = false;
                    break;
                }
            }
            if (matches)
                return true;
        }
        return false;
    }
}
