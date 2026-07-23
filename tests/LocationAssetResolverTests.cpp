#include "DragonbornPresence/core/LocationAssetResolver.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace core = DragonbornPresence::core;

namespace
{

    core::Config MakeConfig()
    {
        core::Config config;
        config.largeImage = "fallback_image";
        config.largeText = "Fallback text";
        config.locationImageRules.clear();
        return config;
    }

    core::LocationContext MakeLocation()
    {
        return {
            "Tamriel",
            "WhiterunExterior01",
            {"WhiterunHoldLocation", "WhiterunLocation"},
            "Skyrim: Whiterun",
        };
    }

} // namespace

TEST_CASE("Location resolver returns configured fallback without a matching rule")
{
    const core::Config config = MakeConfig();
    const core::LocationAssetResolver resolver(config);

    const auto selected = resolver.Resolve(MakeLocation());

    CHECK(selected.image == "fallback_image");
    CHECK(selected.text == "Fallback text");
}

TEST_CASE("Location resolver uses the first matching rule")
{
    core::Config config = MakeConfig();
    config.locationImageRules = {
        {"", "", "", "WHITERUN", "first", "First text"},
        {"", "", "", "Whiterun", "second", "Second text"},
    };
    const core::LocationAssetResolver resolver(config);

    const auto selected = resolver.Resolve(MakeLocation());

    CHECK(selected.image == "first");
    CHECK(selected.text == "First text");
}

TEST_CASE("Location resolver requires every populated selector to match")
{
    core::Config config = MakeConfig();
    config.locationImageRules = {
        {"Sovngarde", "WhiterunLocation", "WhiterunExterior01", "Whiterun", "wrong", "Wrong"},
        {"Tamriel", "WhiterunLocation", "WhiterunExterior01", "wHiTeRuN", "whiterun", "Whiterun"},
    };
    const core::LocationAssetResolver resolver(config);

    const auto selected = resolver.Resolve(MakeLocation());

    CHECK(selected.image == "whiterun");
    CHECK(selected.text == "Whiterun");
}

TEST_CASE("Location selector matches any location in the parent hierarchy")
{
    core::Config config = MakeConfig();
    config.locationImageRules = {
        {"", "WhiterunHoldLocation", "", "", "hold", "Hold"},
    };
    const core::LocationAssetResolver resolver(config);

    const auto selected = resolver.Resolve(MakeLocation());

    CHECK(selected.image == "hold");
}

TEST_CASE("Empty rule text inherits the configured large text")
{
    core::Config config = MakeConfig();
    config.locationImageRules = {
        {"Tamriel", "", "", "", "tamriel", ""},
    };
    const core::LocationAssetResolver resolver(config);

    const auto selected = resolver.Resolve(MakeLocation());

    CHECK(selected.image == "tamriel");
    CHECK(selected.text == "Fallback text");
}
