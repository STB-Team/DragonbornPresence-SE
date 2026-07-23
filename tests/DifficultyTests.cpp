#include "DragonbornPresence/core/Difficulty.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace core = DragonbornPresence::core;

TEST_CASE("Difficulty values map to stable STB labels")
{
    CHECK(core::DifficultyName(0) == "🟢Приключение");
    CHECK(core::DifficultyName(1) == "🟡Тактика");
    CHECK(core::DifficultyName(2) == "🔴Героический");
    CHECK(core::DifficultyName(3) == "⚫Испытание богов");
    CHECK(core::DifficultyName(4) == "⚪Свой уровень сложности");
}

TEST_CASE("Missing and unsupported difficulty values use the fallback")
{
    CHECK(core::DifficultyName(std::nullopt) == "не определена");
    CHECK(core::DifficultyName(-1) == "не определена");
    CHECK(core::DifficultyName(5) == "не определена");
}
