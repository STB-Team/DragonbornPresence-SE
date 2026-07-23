#include "DragonbornPresence/adapters/config/JsonConfigProvider.h"

#include "DragonbornPresence/core/Config.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>
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

        void WriteBase(std::string_view contents) const
        {
            WriteFile("DragonbornPresence.json", contents);
        }

        void WriteUser(std::string_view contents) const
        {
            WriteFile("DragonbornPresence.user.json", contents);
        }

    private:
        void WriteFile(
            std::string_view filename,
            std::string_view contents) const
        {
            const auto path =
                root_ / "Data" / "SKSE" / "Plugins" / filename;
            std::ofstream file(
                path,
                std::ios::binary | std::ios::trunc);
            file << contents;
            file.close();

            std::error_code error;
            const auto writeTime =
                std::filesystem::last_write_time(path, error);
            if (!error)
            {
                std::filesystem::last_write_time(
                    path,
                    writeTime + std::chrono::seconds(++revision_),
                    error);
            }
        }

        std::filesystem::path previousPath_;
        std::filesystem::path root_;
        mutable int revision_ = 0;
    };

    void CheckDefaults(const core::Config &config)
    {
        const core::Config defaults;
        CHECK(config.enabled == defaults.enabled);
        CHECK(config.largeImage == defaults.largeImage);
        CHECK(config.largeText == defaults.largeText);
        CHECK(config.loadingImage == defaults.loadingImage);
        CHECK(config.combatImage == defaults.combatImage);
        CHECK(config.locationImageRules.empty());
        CHECK(config.detailsTemplate == defaults.detailsTemplate);
        CHECK(config.stateTemplate == defaults.stateTemplate);
        CHECK(config.largeTextTemplate == defaults.largeTextTemplate);
        CHECK(config.combatTextTemplate == defaults.combatTextTemplate);
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
    directory.WriteBase(R"json(
{
  "schema_version": 1,
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
    directory.WriteUser(R"json(
{
  "schema_version": 1,
  "discord": {
    "enabled": false
  },
  "presence": {
    "details": "{stone} / {difficulty}",
    "state": "{deaths} | lvl-{lvl}",
    "large_text": "{player} — {god}",
    "combat_text": "{combat} / {vampire}"
  }
}
)json");

    config_adapter::JsonConfigProvider provider;
    const core::Config config = provider.Load();

    CHECK_FALSE(config.enabled);
    CHECK(config.detailsTemplate == "{stone} / {difficulty}");
    CHECK(config.stateTemplate == "{deaths} | lvl-{lvl}");
    CHECK(config.largeTextTemplate == "{player} — {god}");
    CHECK(config.combatTextTemplate == "{combat} / {vampire}");
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

TEST_CASE("Legacy default state template migrates emoji out of editable text")
{
    const ScopedConfigDirectory directory;
    directory.WriteUser(R"json(
{
  "schema_version": 1,
  "presence": {
    "state": "lvl-{lvl} 💀-{deaths} {stone}"
  }
}
)json");

    config_adapter::JsonConfigProvider provider;
    CHECK(provider.Load().stateTemplate == core::kDefaultStateTemplate);
}

TEST_CASE("Invalid optional fields preserve defaults and malformed rules are discarded")
{
    const ScopedConfigDirectory directory;
    directory.WriteBase(R"json(
{
  "schema_version": 1,
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
    directory.WriteUser(R"json(
{
  "schema_version": 1,
  "discord": {"enabled": "yes"},
  "presence": {
    "details": ["difficulty"],
    "state": 7
  }
}
)json");

    config_adapter::JsonConfigProvider provider;
    const core::Config config = provider.Load();
    const core::Config defaults;

    CHECK(config.enabled == defaults.enabled);
    CHECK(config.largeImage == defaults.largeImage);
    CHECK(config.largeText == "Valid text");
    CHECK(config.loadingImage == defaults.loadingImage);
    CHECK(config.combatImage == "valid_combat");
    REQUIRE(config.locationImageRules.size() == 1);
    CHECK(config.locationImageRules.front().cell == "ValidCell");
    CHECK(config.locationImageRules.front().image == "valid_rule");
    CHECK(config.detailsTemplate == defaults.detailsTemplate);
    CHECK(config.stateTemplate == defaults.stateTemplate);
    CHECK(config.largeTextTemplate == defaults.largeTextTemplate);
    CHECK(config.combatTextTemplate == defaults.combatTextTemplate);
}

TEST_CASE("Changed configuration reloads atomically and rejects malformed input")
{
    const ScopedConfigDirectory directory;
    directory.WriteBase(R"json(
{
  "schema_version": 1,
  "assets": {"large_text": "Initial"}
}
)json");
    directory.WriteUser(R"json(
{
  "schema_version": 1,
  "discord": {"enabled": true}
}
)json");

    config_adapter::JsonConfigProvider provider;
    CHECK(provider.Load().largeText == "Initial");
    CHECK_FALSE(provider.ReloadIfChanged().has_value());

    directory.WriteBase(R"json(
{
  "schema_version": 1,
  "assets": {"large_text": "Reloaded"}
}
)json");
    const auto reloaded = provider.ReloadIfChanged();
    REQUIRE(reloaded);
    CHECK(reloaded->largeText == "Reloaded");
    CHECK(reloaded->enabled);

    directory.WriteUser("{ invalid json");
    CHECK_FALSE(provider.ReloadIfChanged().has_value());
    CHECK_FALSE(provider.ReloadIfChanged().has_value());

    directory.WriteUser(R"json(
{
  "schema_version": 1,
  "discord": {"enabled": false},
  "presence": {
    "details": "",
    "state": "{stone} | {lvl}",
    "large_text": "{player}",
    "combat_text": "{combat} — {god}"
  },
  "reload_revision": 3
}
)json");
    const auto recovered = provider.ReloadIfChanged();
    REQUIRE(recovered);
    CHECK_FALSE(recovered->enabled);
    CHECK(recovered->detailsTemplate.empty());
    CHECK(recovered->stateTemplate == "{stone} | {lvl}");
    CHECK(recovered->largeTextTemplate == "{player}");
    CHECK(recovered->combatTextTemplate == "{combat} — {god}");
}

TEST_CASE("Malformed JSON configuration returns complete defaults")
{
    const ScopedConfigDirectory directory;
    directory.WriteBase("{ invalid json");
    config_adapter::JsonConfigProvider provider;

    CheckDefaults(provider.Load());
}

TEST_CASE("Non-object JSON root returns complete defaults")
{
    const ScopedConfigDirectory directory;
    directory.WriteBase("[]");
    config_adapter::JsonConfigProvider provider;

    CheckDefaults(provider.Load());
}

TEST_CASE("Configuration without schema_version uses legacy schema one")
{
    const ScopedConfigDirectory directory;
    directory.WriteUser(R"json(
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
        directory.WriteBase(R"json({"schema_version": "1", "discord": {"enabled": false}})json");
        config_adapter::JsonConfigProvider provider;
        CheckDefaults(provider.Load());
    }

    SECTION("schema version is older than the supported version")
    {
        directory.WriteBase(R"json({"schema_version": 0, "discord": {"enabled": false}})json");
        config_adapter::JsonConfigProvider provider;
        CheckDefaults(provider.Load());
    }

    SECTION("schema version is newer than the supported version")
    {
        directory.WriteBase(R"json({"schema_version": 2, "discord": {"enabled": false}})json");
        config_adapter::JsonConfigProvider provider;
        CheckDefaults(provider.Load());
    }
}
