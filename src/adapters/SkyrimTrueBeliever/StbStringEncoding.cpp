#include "DragonbornPresence/adapters/SkyrimTrueBeliever/StbStringEncoding.h"

#include <Windows.h>

#include <string>

namespace DragonbornPresence::adapters::SkyrimTrueBeliever
{

    std::string Cp1251ToUtf8(
        const char *value)
    {
        if (!value || *value == '\0')
            return {};

        const int wideLength = MultiByteToWideChar(
            1251,
            0,
            value,
            -1,
            nullptr,
            0);

        if (wideLength <= 0)
            return {};

        std::wstring wide(
            static_cast<std::size_t>(wideLength),
            L'\0');

        if (!MultiByteToWideChar(
                1251,
                0,
                value,
                -1,
                wide.data(),
                wideLength))
        {
            return {};
        }

        // Exclude the null terminator from the UTF-8 result.
        const int utf8Length = WideCharToMultiByte(
            CP_UTF8,
            0,
            wide.data(),
            wideLength - 1,
            nullptr,
            0,
            nullptr,
            nullptr);

        if (utf8Length <= 0)
            return {};

        std::string result(
            static_cast<std::size_t>(utf8Length),
            '\0');

        if (!WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.data(),
                wideLength - 1,
                result.data(),
                utf8Length,
                nullptr,
                nullptr))
        {
            return {};
        }

        return result;
    }

} // namespace DragonbornPresence::adapters::SkyrimTrueBeliever