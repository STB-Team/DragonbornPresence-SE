#include "AdditionalFunctions.h"
#include <windows.h>

std::string Cp1251ToUtf8(const char* str) {
    if (!str || !*str) return {};

    int wlen = MultiByteToWideChar(1251, 0, str, -1, nullptr, 0);
    if (wlen <= 0) return {};

    std::wstring wide(wlen, L'\0');
    if (!MultiByteToWideChar(1251, 0, str, -1, wide.data(), wlen)) return {};

    // wlen - 1: exclude the null terminator so clen reflects only UTF-8 content bytes
    int clen = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen - 1, nullptr, 0, nullptr, nullptr);
    if (clen <= 0) return {};

    std::string result(clen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen - 1, result.data(), clen, nullptr, nullptr);
    return result;
}

bool IsValidUtf8(std::string_view sv) {
    auto* bytes = reinterpret_cast<const unsigned char*>(sv.data());
    auto* end   = bytes + sv.size();

    while (bytes < end) {
        uint32_t cp;
        int num;

        if ((*bytes & 0x80) == 0x00)      { cp = *bytes & 0x7F; num = 1; }
        else if ((*bytes & 0xE0) == 0xC0) { cp = *bytes & 0x1F; num = 2; }
        else if ((*bytes & 0xF0) == 0xE0) { cp = *bytes & 0x0F; num = 3; }
        else if ((*bytes & 0xF8) == 0xF0) { cp = *bytes & 0x07; num = 4; }
        else return false;

        ++bytes;
        for (int i = 1; i < num; ++i) {
            if (bytes >= end || (*bytes & 0xC0) != 0x80) return false;
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
