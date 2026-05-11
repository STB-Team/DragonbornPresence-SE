#pragma once
#include <string>
#include <string_view>

std::string Cp1251ToUtf8(const char* str);
bool IsValidUtf8(std::string_view str);
