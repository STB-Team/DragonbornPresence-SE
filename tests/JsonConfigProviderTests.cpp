#include "DragonbornPresence/adapters/config/JsonConfigProvider.h"

#include "DragonbornPresence/core/Config.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

namespace config_adapter = DragonbornPresence::adapters::config;
namespace core = DragonbornPresence::core;

namespace
{

    class ScopedConfigDirectory final
    {
    public:
        ScopedConfigDirectory()
            : previousPath_(std::filesystem::current_path()),
              root_(std::filesystem::temp_directory_path() /
                    "DragonbornPresenceConfigTests")
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
            std::filesystem::create_directories(
                root_ / "Data" / "SKSE" / "Plugins");
            std::filesystem::current_path(root_);
        }

        ~ScopedConfigDirectory()
        {
            std::error_code error;
            std::filesystem::current_path(previousPath_, error);
            std::filesystem::remove_all(root_, error);
        }

        void Write(std::string_view contents) const
        {
            std::ofstream file(
                root_ / "Data" / "SKSE" / "Plugins" /
                "DragonbornPresence.json",
                std::ios::binary | std::ios::trunc);
            file << contents;
        }

    private:
        std::filesystem::path previousPath_;
        std::filesystem::path root_;
    };

    void CheckDefaults(const core::Config &config)
    {
        const core::Config defaults;
        CHECK(config.enabled == defaults.enabled);
        CHECK(config.applicationId == defaults.applicationId);
        CHECK(config.largeImage == defaults.largeImage);
        CHECK(config.largeText == defaults.largeText);
        CHECK(config.loadingImage == defaults.loadingImage);
        CHECK(config.combatImage == defaults.combatImage);
        CHECK(config.locationImageRules.empty());
    }

} // namespace

TEST_CASE("Missing JSON configuration returns complete defaults")
{
    const ScopedConfigDirectory directory;
    config_adapter::JsonConfigProvider provider;

    CheckDefaults(provider.Load());
}

TEST_CASE("Valid JSON configuration maps every supported field")
{
    const ScopedConfigDirectory directory;
    directory.Write(R"json(
{
  "schema_version": 1,
  "discord": {
    "enabled": false,
    "application_id": "123456789012345678"
  },
  "assets": {
    "large_image": "custom_large",
    "large_text": "Custom large text",
    "small_images": {
      "loading": "custom_loading",
      "combat": "custom_combat"
    },
    "location_images": [
      {
        "worldspace": "Tamriel",
        "location": "WhiterunLocation",
        "cell": "WhiterunExterior01",
        "match": "Whiterun",
        "image": "whiterun",
        "text": "Whiterun text"
      },
      {
        "match": "Riften",
        "image": "riften"
      }
    ]
  }
}
)json");

    config_adapter::JsonConfigProvider provider;
    const core::Config config = provider.Load();

    CHECK_FALSE(config.enabled);
    CHECK(config.applicationId == 123456789012345678);
    CHECK(config.largeImage == "custom_large");
    CHECK(config.largeText == "Custom large text");
    CHECK(config.loadingImage == "custom_loading");
    CHECK(config.combatImage == "custom_combat");
    REQUIRE(config.locationImageRules.size() == 2);

    const auto &whiterun = config.locationImageRules[0];
    CHECK(whiterun.worldspace == "Tamriel");
    CHECK(whiterun.location == "WhiterunLocation");
    CHECK(whiterun.cell == "WhiterunExterior01");
    CHECK(whiterun.match == "Whiterun");
    CHECK(whiterun.image == "whiterun");
    CHECK(whiterun.text == "Whiterun text");

    const auto &riften = config.locationImageRules[1];
    CHECK(riften.match == "Riften");
    CHECK(riften.image == "riften");
    CHECK(riften.text.empty());
}

TEST_CASE("Invalid optional fields preserve defaults and malformed rules are discarded")
{
    const ScopedConfigDirectory directory;
    directory.Write(R"json(
{
  "schema_version": 1,
  "discord": {
    "enabled": "yes",
    "application_id": "-5"
  },
  "assets": {
    "large_image": 42,
    "large_text": "Valid text",
    "small_images": {
      "loading": false,
      "combat": "valid_combat"
    },
    "location_images": [
      {"image": "missing_selector"},
      {"match": "MissingImage"},
      {"match": 7, "image": "wrong_selector_type"},
      {"cell": "ValidCell", "image": "valid_rule"}
    ]
  }
}
)json");

    config_adapter::JsonConfigProvider provider;
    const core::Config config = provider.Load();
    const core::Config defaults;

    CHECK(config.enabled == defaults.enabled);
    CHECK(config.applicationId == defaults.applicationId);
    CHECK(config.largeImage == defaults.largeImage);
    CHECK(config.largeText == "Valid text");
    CHECK(config.loadingImage == defaults.loadingImage);
    CHECK(config.combatImage == "valid_combat");
    REQUIRE(config.locationImageRules.size() == 1);
    CHECK(config.locationImageRules.front().cell == "ValidCell");
    CHECK(config.locationImageRules.front().image == "valid_rule");
}

TEST_CASE("Malformed JSON configuration returns complete defaults")
{
    const ScopedConfigDirectory directory;
    directory.Write("{ invalid json");
    config_adapter::JsonConfigProvider provider;

    CheckDefaults(provider.Load());
}

TEST_CASE("Non-object JSON root returns complete defaults")
{
    const ScopedConfigDirectory directory;
    directory.Write("[]");
    config_adapter::JsonConfigProvider provider;

    CheckDefaults(provider.Load());
}

TEST_CASE("Configuration without schema_version uses legacy schema one")
{
    const ScopedConfigDirectory directory;
    directory.Write(R"json(
{
  "discord": {
    "enabled": false
  }
}
)json");

    config_adapter::JsonConfigProvider provider;
    CHECK_FALSE(provider.Load().enabled);
}

TEST_CASE("Invalid and unsupported schema versions return complete defaults")
{
    const ScopedConfigDirectory directory;

    SECTION("schema version has the wrong type")
    {
        directory.Write(R"json({"schema_version": "1", "discord": {"enabled": false}})json");
        config_adapter::JsonConfigProvider provider;
        CheckDefaults(provider.Load());
    }

    SECTION("schema version is older than the supported version")
    {
        directory.Write(R"json({"schema_version": 0, "discord": {"enabled": false}})json");
        config_adapter::JsonConfigProvider provider;
        CheckDefaults(provider.Load());
    }

    SECTION("schema version is newer than the supported version")
    {
        directory.Write(R"json({"schema_version": 2, "discord": {"enabled": false}})json");
        config_adapter::JsonConfigProvider provider;
        CheckDefaults(provider.Load());
    }
}
