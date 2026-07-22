#include "DragonbornPresence/core/TextUtils.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ASCII text is valid UTF-8")
{
    CHECK(DragonbornPresence::core::IsValidUtf8(""));
    CHECK(DragonbornPresence::core::IsValidUtf8("DragonbornPresence"));
}

TEST_CASE("Malformed UTF-8 is rejected")
{
    CHECK_FALSE(DragonbornPresence::core::IsValidUtf8("\x80"));
    CHECK_FALSE(DragonbornPresence::core::IsValidUtf8("\xC2"));
}