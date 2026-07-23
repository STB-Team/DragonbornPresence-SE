#include "DragonbornPresence/adapters/SkyrimTrueBeliever/StbStringEncoding.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace stb = DragonbornPresence::adapters::SkyrimTrueBeliever;

TEST_CASE("CP1251 conversion handles empty input")
{
    CHECK(stb::Cp1251ToUtf8(nullptr).empty());
    CHECK(stb::Cp1251ToUtf8("").empty());
}

TEST_CASE("CP1251 conversion preserves ASCII")
{
    CHECK(stb::Cp1251ToUtf8("DragonbornPresence") == "DragonbornPresence");
}

TEST_CASE("CP1251 conversion produces UTF-8 Russian text")
{
    const std::string cp1251 = "\xCF\xF0\xE8\xE2\xE5\xF2";
    const std::string expectedUtf8 =
        "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";

    CHECK(stb::Cp1251ToUtf8(cp1251.c_str()) == expectedUtf8);
}
