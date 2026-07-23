#include "DragonbornPresence/adapters/config/JsonConfigProvider.h"

#include <spdlog/spdlog.h>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace DragonbornPresence::adapters::config
{

    namespace
    {

        /// Runtime location used by Skyrim for the plugin configuration.
        ///
        /// The path is an infrastructure detail, so it remains inside this translation
        /// unit instead of becoming part of core::Config or the public adapter API.
        constexpr std::string_view kConfigPath =
            R"(Data\SKSE\Plugins\DragonbornPresence.json)";

        /// Translation-unit-local parser that converts JSON values into core models.
        ///
        /// Keeping this class in the anonymous namespace prevents other components
        /// from depending on its parsing helpers. JsonConfigProvider::Load is the only
        /// public entry point of this adapter.
        class ConfigLoader final
        {
        public:
            /// Reads the configured file and returns defaults for missing or invalid input.
            [[nodiscard]] static core::Config Load()
            {
                core::Config config;
                std::ifstream file(kConfigPath.data());
                if (!file)
                {
                    spdlog::info("Config not found; using defaults.");
                    return config;
                }

                try
                {
                    const auto root = nlohmann::json::parse(file);
                    if (!root.is_object())
                    {
                        spdlog::warn("Config root must be an object; using defaults.");
                        return config;
                    }

                    if (const auto *discordConfig = FindObject(root, "discord", "discord"))
                    {
                        ReadBool(*discordConfig, "enabled", config.enabled, "discord");
                        ReadApplicationId(*discordConfig, config);
                    }
                    if (const auto *assets = FindObject(root, "assets", "assets"))
                    {
                        ReadString(*assets, "large_image", config.largeImage, "assets");
                        ReadString(*assets, "large_text", config.largeText, "assets");
                        if (const auto *smallImages =
                                FindObject(*assets, "small_images", "assets.small_images"))
                        {
                            ReadString(
                                *smallImages,
                                "loading",
                                config.loadingImage,
                                "assets.small_images");
                            ReadString(
                                *smallImages,
                                "combat",
                                config.combatImage,
                                "assets.small_images");
                        }
                        ReadLocationImageRules(*assets, config);
                    }
                    spdlog::info("Config loaded.");
                }
                catch (const nlohmann::json::exception &error)
                {
                    spdlog::error("Failed to parse config JSON: {}", error.what());
                    return {};
                }
                return config;
            }

        private:
            /// Returns an object-valued child or reports a type mismatch.
            [[nodiscard]] static const nlohmann::json *FindObject(
                const nlohmann::json &parent,
                const char *key,
                std::string_view path)
            {
                const auto value = parent.find(key);
                if (value == parent.end())
                    return nullptr;
                if (!value->is_object())
                {
                    spdlog::warn("Config: '{}' must be an object; using defaults.", path);
                    return nullptr;
                }
                return &*value;
            }

            /// Reads an optional boolean property without replacing its default on error.
            static void ReadBool(
                const nlohmann::json &object,
                const char *key,
                bool &target,
                std::string_view path)
            {
                const auto value = object.find(key);
                if (value == object.end())
                    return;
                if (!value->is_boolean())
                {
                    spdlog::warn(
                        "Config: '{}.{}' must be a boolean; using default.",
                        path,
                        key);
                    return;
                }
                target = value->get<bool>();
            }

            /// Reads an optional string property without replacing its default on error.
            static void ReadString(
                const nlohmann::json &object,
                const char *key,
                std::string &target,
                std::string_view path)
            {
                const auto value = object.find(key);
                if (value == object.end())
                    return;
                if (!value->is_string())
                {
                    spdlog::warn(
                        "Config: '{}.{}' must be a string; using default.",
                        path,
                        key);
                    return;
                }
                target = value->get<std::string>();
            }

            /// Parses a positive Discord application ID from an integer or decimal string.
            static void ReadApplicationId(
                const nlohmann::json &discordConfig,
                core::Config &config)
            {
                const auto value = discordConfig.find("application_id");
                if (value == discordConfig.end())
                    return;

                if (value->is_number_integer())
                {
                    const auto parsedId = value->get<std::int64_t>();
                    if (parsedId > 0)
                        config.applicationId = parsedId;
                    return;
                }
                if (value->is_string())
                {
                    const std::string encodedId = value->get<std::string>();
                    core::ApplicationId parsedId = 0;
                    const auto [end, error] = std::from_chars(
                        encodedId.data(),
                        encodedId.data() + encodedId.size(),
                        parsedId);
                    if (error == std::errc{} && end == encodedId.data() + encodedId.size() &&
                        parsedId > 0)
                    {
                        config.applicationId = parsedId;
                        return;
                    }
                }
                spdlog::warn(
                    "Config: 'discord.application_id' is invalid; using default.");
            }

            /// Parses ordered location-image rules and discards malformed entries.
            static void ReadLocationImageRules(
                const nlohmann::json &assets,
                core::Config &config)
            {
                const auto rules = assets.find("location_images");
                if (rules == assets.end())
                    return;
                if (!rules->is_array())
                {
                    spdlog::warn(
                        "Config: 'assets.location_images' must be an array; using defaults.");
                    return;
                }

                std::vector<core::LocationImageRule> parsedRules;
                parsedRules.reserve(rules->size());
                for (std::size_t ruleIndex = 0; ruleIndex < rules->size(); ++ruleIndex)
                {
                    const auto &value = (*rules)[ruleIndex];
                    if (!value.is_object())
                    {
                        spdlog::warn(
                            "Config: 'assets.location_images[{}]' must be an object; ignoring.",
                            ruleIndex);
                        continue;
                    }

                    core::LocationImageRule rule;
                    bool isValid = true;
                    const auto readOptionalString = [&](const char *key, std::string &target)
                    {
                        const auto field = value.find(key);
                        if (field == value.end())
                            return;
                        if (!field->is_string())
                        {
                            spdlog::warn(
                                "Config: 'assets.location_images[{}].{}' must be a string; "
                                "ignoring rule.",
                                ruleIndex,
                                key);
                            isValid = false;
                            return;
                        }
                        target = field->get<std::string>();
                    };

                    readOptionalString("worldspace", rule.worldspace);
                    readOptionalString("location", rule.location);
                    readOptionalString("cell", rule.cell);
                    readOptionalString("match", rule.match);
                    readOptionalString("image", rule.image);
                    readOptionalString("text", rule.text);

                    const bool hasSelector =
                        !rule.worldspace.empty() || !rule.location.empty() ||
                        !rule.cell.empty() || !rule.match.empty();
                    if (!isValid || !hasSelector || rule.image.empty())
                    {
                        spdlog::warn(
                            "Config: 'assets.location_images[{}]' requires 'image' and a "
                            "selector; ignoring.",
                            ruleIndex);
                        continue;
                    }
                    parsedRules.push_back(std::move(rule));
                }
                config.locationImageRules = std::move(parsedRules);
            }
        };
    } // namespace
    core::Config JsonConfigProvider::Load()
    {
        return ConfigLoader::Load();
    }
} // namespace DragonbornPresence::adapters::config