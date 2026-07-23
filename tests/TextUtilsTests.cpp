#include "DragonbornPresence/core/TextUtils.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace core = DragonbornPresence::core;

TEST_CASE("UTF-8 validation accepts every valid sequence width")
{
    CHECK(core::IsValidUtf8(""));
    CHECK(core::IsValidUtf8("DragonbornPresence"));
    CHECK(core::IsValidUtf8("\xC2\xA2"));
    CHECK(core::IsValidUtf8("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"));
    CHECK(core::IsValidUtf8("\xF0\x9F\x92\x80"));
    CHECK(core::IsValidUtf8("\xF4\x8F\xBF\xBF"));
}

TEST_CASE("UTF-8 validation rejects malformed and non-scalar sequences")
{
    CHECK_FALSE(core::IsValidUtf8("\x80"));
    CHECK_FALSE(core::IsValidUtf8("\xC2"));
    CHECK_FALSE(core::IsValidUtf8("\xE2\x28\xA1"));
    CHECK_FALSE(core::IsValidUtf8("\xF0\x9F\x92"));
    CHECK_FALSE(core::IsValidUtf8("\xC0\x80"));
    CHECK_FALSE(core::IsValidUtf8("\xE0\x80\x80"));
    CHECK_FALSE(core::IsValidUtf8("\xF0\x80\x80\x80"));
    CHECK_FALSE(core::IsValidUtf8("\xED\xA0\x80"));
    CHECK_FALSE(core::IsValidUtf8("\xF4\x90\x80\x80"));
    CHECK_FALSE(core::IsValidUtf8("\xF8\x88\x80\x80\x80"));
}

TEST_CASE("UTF-8 byte limiting preserves complete code points")
{
    const std::string text = "A\xF0\x9F\x92\x80"
                             "B";

    CHECK(core::LimitUtf8Bytes(text, 0).empty());
    CHECK(core::LimitUtf8Bytes(text, 1) == "A");
    CHECK(core::LimitUtf8Bytes(text, 2) == "A");
    CHECK(core::LimitUtf8Bytes(text, 4) == "A");
    CHECK(core::LimitUtf8Bytes(text, 5) == "A\xF0\x9F\x92\x80");
    CHECK(core::LimitUtf8Bytes(text, 6) == text);
    CHECK(core::LimitUtf8Bytes("abcdef", 3) == "abc");
}

TEST_CASE("ASCII-insensitive search handles boundaries and casing")
{
    CHECK(core::ContainsAsciiInsensitive("Whiterun Hold", "whiterun"));
    CHECK(core::ContainsAsciiInsensitive("Whiterun Hold", "HOLD"));
    CHECK(core::ContainsAsciiInsensitive("abc", ""));
    CHECK(core::ContainsAsciiInsensitive("abc", "abc"));
    CHECK_FALSE(core::ContainsAsciiInsensitive("abc", "abcd"));
    CHECK_FALSE(core::ContainsAsciiInsensitive("Whiterun", "Riften"));

    const std::string utf8Text = "\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82-ABC";
    CHECK(core::ContainsAsciiInsensitive(utf8Text, "-abc"));
}