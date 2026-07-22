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

