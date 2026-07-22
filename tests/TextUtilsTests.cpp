#include "DragonbornPresence/core/TextUtils.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Valid UTF-8 is accepted")
{
    CHECK(DragonbornPresence::core::IsValidUtf8(""));
    CHECK(DragonbornPresence::core::IsValidUtf8("DragonbornPresence"));
    // "Привет"
    CHECK(DragonbornPresence::core::IsValidUtf8(
        "\xD0\x9F\xD1\x80\xD0\xB8"
        "\xD0\xB2\xD0\xB5\xD1\x82"));

    // U+1F480 SKULL
    CHECK(DragonbornPresence::core::IsValidUtf8("\xF0\x9F\x92\x80"));
}

TEST_CASE("Malformed UTF-8 is rejected")
{
    CHECK_FALSE(DragonbornPresence::core::IsValidUtf8("\x80"));
    CHECK_FALSE(DragonbornPresence::core::IsValidUtf8("\xC2"));
}