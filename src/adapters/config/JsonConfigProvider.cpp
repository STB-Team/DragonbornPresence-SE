#include "DragonbornPresence/adapters/config/JsonConfigProvider.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <optional>
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
        constexpr std::string_view kBaseConfigPath =
            R"(Data\SKSE\Plugins\DragonbornPresence.json)";
        constexpr std::string_view kUserConfigPath =
            R"(Data\SKSE\Plugins\DragonbornPresence.user.json)";

        /// Translation-unit-local parser that converts JSON values into core models.
        ///
        /// from depending on its parsing helpers.
        class ConfigLoader final
        {
        public:
            [[nodiscard]] static std::optional<core::Config> Load()
            {
                core::Config config;

                const auto baseDocument =
                    ReadDocument(kBaseConfigPath, "base config");
                if (!baseDocument.valid)
                    return std::nullopt;
                if (baseDocument.root)
                    ReadBaseConfig(*baseDocument.root, config);

                const auto userDocument =
                    ReadDocument(kUserConfigPath, "user config");
                if (!userDocument.valid)
                    return std::nullopt;
                if (userDocument.root)
                    ReadUserConfig(*userDocument.root, config);

                return config;
            }

        private:
            struct DocumentResult
            {
                bool valid = true;
                std::optional<nlohmann::json> root;
            };

            [[nodiscard]] static DocumentResult ReadDocument(
                std::string_view path,
                std::string_view label)
            {
                std::error_code error;
                if (!std::filesystem::exists(path, error))
                    return {};
                if (error)
                {
                    spdlog::error(
                        "Failed to inspect {} '{}': {}.",
                        label,
                        path,
                        error.message());
                    return {false, std::nullopt};
                }

                std::ifstream file(path.data());
                if (!file)
                {
                    spdlog::error("Failed to open {} '{}'.", label, path);
                    return {false, std::nullopt};
                }

                try
                {
                    auto root = nlohmann::json::parse(file);
                    if (!root.is_object())
                    {
                        spdlog::warn(
                            "{} root must be an object.",
                            label);
                        return {false, std::nullopt};
                    }
                    if (!HasSupportedSchemaVersion(root, label))
                        return {false, std::nullopt};
                    return {true, std::move(root)};
                }
                catch (const nlohmann::json::exception &parseError)
                {
                    spdlog::error(
                        "Failed to parse {} JSON: {}",
                        label,
                        parseError.what());
                    return {false, std::nullopt};
                }
            }

            [[nodiscard]] static bool HasSupportedSchemaVersion(
                const nlohmann::json &root,
                std::string_view label)
            {
                const auto value = root.find("schema_version");
                if (value == root.end())
                {
                    spdlog::warn(
                        "{}: 'schema_version' is missing; treating the document as "
                        "legacy schema {}.",
                        label,
                        core::kSupportedConfigSchemaVersion);
                    return true;
                }

                if (!value->is_number_integer())
                {
                    spdlog::warn(
                        "{}: 'schema_version' must be an integer.",
                        label);
                    return false;
                }
                if (*value != core::kSupportedConfigSchemaVersion)
                {
                    spdlog::warn(
                        "{} schema {} is unsupported; expected {}.",
                        label,
                        value->dump(),
                        core::kSupportedConfigSchemaVersion);
                    return false;
                }
                return true;
            }

            static void ReadBaseConfig(
                const nlohmann::json &root,
                core::Config &config)
            {
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
            }

            static void ReadUserConfig(
                const nlohmann::json &root,
                core::Config &config)
            {
                if (const auto *discordConfig =
                        FindObject(root, "discord", "discord"))
                {
                    ReadBool(*discordConfig, "enabled", config.enabled, "discord");
                }

                if (const auto *presence =
                        FindObject(root, "presence", "presence"))
                {
                    ReadString(
                        *presence,
                        "details",
                        config.detailsTemplate,
                        "presence");
                    ReadString(
                        *presence,
                        "state",
                        config.stateTemplate,
                        "presence");
                    if (config.stateTemplate ==
                        core::kLegacyDefaultStateTemplate)
                    {
                        config.stateTemplate =
                            core::kDefaultStateTemplate;
                    }
                    ReadString(
                        *presence,
                        "large_text",
                        config.largeTextTemplate,
                        "presence");
                    ReadString(
                        *presence,
                        "combat_text",
                        config.combatTextTemplate,
                        "presence");
                }
            }

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
                    spdlog::warn("Config: '{}' must be an object.", path);
                    return nullptr;
                }
                return &*value;
            }

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
                    const auto readOptionalString =
                        [&](const char *key, std::string &target)
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
        baseState_ = Inspect(kBaseConfigPath);
        userState_ = Inspect(kUserConfigPath);
        hasObservedFiles_ = true;

        const auto config = ConfigLoader::Load();
        if (!config)
        {
            spdlog::warn(
                "Initial configuration is invalid; using safe defaults.");
            return {};
        }

        spdlog::info("Configuration loaded.");
        return *config;
    }

    std::optional<core::Config> JsonConfigProvider::ReloadIfChanged()
    {
        const auto baseState = Inspect(kBaseConfigPath);
        const auto userState = Inspect(kUserConfigPath);
        if (hasObservedFiles_ &&
            baseState == baseState_ &&
            userState == userState_)
        {
            return std::nullopt;
        }

        baseState_ = baseState;
        userState_ = userState;
        hasObservedFiles_ = true;

        const auto config = ConfigLoader::Load();
        if (!config)
        {
            spdlog::error(
                "Configuration reload failed; keeping the last valid configuration.");
            return std::nullopt;
        }

        spdlog::info("Configuration files changed; configuration reloaded.");
        return config;
    }

    JsonConfigProvider::FileState JsonConfigProvider::Inspect(
        const std::filesystem::path &path) noexcept
    {
        FileState state;
        std::error_code error;
        state.exists = std::filesystem::exists(path, error);
        if (error || !state.exists)
            return state;

        state.writeTime = std::filesystem::last_write_time(path, error);
        if (error)
            state.writeTime = {};

        error.clear();
        state.size = std::filesystem::file_size(path, error);
        if (error)
            state.size = 0;
        return state;
    }
} // namespace DragonbornPresence::adapters::config