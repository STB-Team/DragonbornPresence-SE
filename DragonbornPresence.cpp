#include "DragonbornPresence.h"
#include "AdditionalFunctions.h"
#include "ScriptUtils.h"
#include "discord.h"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace DragonbornPresence {

namespace {

namespace constants {

constexpr discord::ClientId kDefaultApplicationId = 1527543892151373937;
constexpr std::string_view kConfigPath = R"(Data\SKSE\Plugins\DragonbornPresence.json)";
constexpr std::chrono::milliseconds kDiscordCallbackInterval{100};
constexpr std::uint8_t kPresencePollIntervalInCallbackTicks = 10;
constexpr std::size_t kDiscordTextMaxBytes = 127;
constexpr std::uint32_t kDifficultyQuestFormId = 0x1417C4;
constexpr std::string_view kStbPluginName = "STB.esp";
constexpr std::string_view kDifficultyScriptName = "aamz_mcmdatastorage";
constexpr std::string_view kDifficultyPropertyName = "aaMZ_SelectedLevel_OfDifficulty";
constexpr std::string_view kDeathsGlobalEditorId = "aaMZgv_NowDeath";
constexpr std::string_view kMainMenuName = "Main Menu";
constexpr std::string_view kLoadingMenuName = "Loading Menu";
constexpr std::string_view kLoadingText = "Загрузка";
constexpr std::string_view kUnknownDeathsText = "—";
constexpr std::string_view kUnknownDifficultyText = "не определена";
constexpr std::string_view kNoStoneText = "не выбран";
constexpr std::string_view kCombatText = "В бою";

}  // namespace constants

namespace model {

/// Describes an STB standing stone and the localized fallback shown by Discord.
struct StoneDefinition {
    std::string_view descriptionSpellEditorId;
    std::string_view fallbackName;
};

constexpr std::array kStoneDefinitions{
    StoneDefinition{"aaMZs_DoomstoneWarriorDesc", "🪓-Воин"},
    StoneDefinition{"aaMZs_DoomstoneSteedDesc", "🐴-Конь"},
    StoneDefinition{"aaMZs_DoomstoneAtronachDesc", "🎭-Атронах"},
    StoneDefinition{"aaMZs_DoomstoneApprenticeDesc", "📜-Ученик"},
    StoneDefinition{"aaMZs_DoomstoneLordDesc", "👑-Лорд"},
    StoneDefinition{"aaMZs_DoomstoneThiefDesc", "🧤-Вор"},
    StoneDefinition{"aaMZs_DoomstoneMageDesc", "🧙‍-Маг"},
    StoneDefinition{"aaMZs_DoomstoneRitualDesc", "👻-Ритуал"},
    StoneDefinition{"aaMZs_DoomstoneSnakeDesc", "🐍-Змей"},
    StoneDefinition{"aaMZs_DoomstoneLadyDesc", "👠-Леди"},
    StoneDefinition{"aaMZs_DoomstoneLoverDesc", "💖-Любовник"},
    StoneDefinition{"aaMZs_DoomstoneShadowDesc", "🌙-Тень"},
    StoneDefinition{"aaMZs_DoomstoneTowerDesc", "🛡️-Башня"},
    StoneDefinition{"aaMZs_DoomstoneBeastDesc", "🦧-Зверь"},
    StoneDefinition{"aaMZs_DoomstoneWindDesc", "🌪️-Ветер"},
    StoneDefinition{"aaMZs_DoomstoneWaterDesc", "🌊-Вода"},
    StoneDefinition{"aaMZs_DoomstoneTreeDesc", "🌲-Дерево"},
    StoneDefinition{"aaMZs_DoomstoneSunDesc", "☀️-Солнце"},
    StoneDefinition{"aaMZs_DoomstoneEarthDesc", "⛰️-Земля"},
};

/// Defines one ordered rule that maps the player's location to a Discord asset.
struct LocationImageRule {
    std::string worldspace;
    std::string location;
    std::string cell;
    std::string match;
    std::string image;
    std::string text;
};

/// Contains all user-configurable Discord presence settings.
struct Config {
    bool enabled = true;
    discord::ClientId applicationId = constants::kDefaultApplicationId;
    std::string largeImage = "stb_logo";
    std::string largeText = "Skyrim True Believer";
    std::string loadingImage = "loading";
    std::string combatImage = "combat";
    std::vector<LocationImageRule> locationImageRules;
};

/// Stores a resolved standing-stone spell and its display name.
struct StoneRuntimeData {
    RE::SpellItem* descriptionSpell = nullptr;
    std::string name;
};

/// Caches STB forms required to read player data without repeated lookups.
struct StbRuntimeData {
    RE::TESGlobal* deaths = nullptr;
    RE::TESQuest* difficultyQuest = nullptr;
    std::vector<StoneRuntimeData> stones;
};

/// Immutable value snapshot used to build one Discord presence update.
struct PlayerSnapshot {
    int level = 0;
    std::optional<int> deaths;
    std::string stone;
    std::string difficulty;
    std::string location;
    std::string combatText;
    bool inCombat = false;
};

/// References the selected large image key and hover text in the active config.
struct LargeAssetSelection {
    std::string_view image;
    std::string_view text;
};

/// Carries all text and asset fields required for one Discord activity.
struct ActivityPayload {
    std::string_view details;
    std::string_view state;
    std::string_view largeImage;
    std::string_view largeText;
    std::string_view smallImage;
    std::string_view smallText;
};

/// Identifies why a presence refresh was requested.
enum class RefreshReason {
    kGameLoaded,
    kLoadingFinished,
    kCombat,
    kPoll,
};

/// Provides the stable log label associated with a refresh reason.
[[nodiscard]] constexpr std::string_view ToLogLabel(RefreshReason reason) noexcept
{
    switch (reason) {
    case RefreshReason::kGameLoaded: return "game-loaded";
    case RefreshReason::kLoadingFinished: return "loading-finished";
    case RefreshReason::kCombat: return "combat";
    case RefreshReason::kPoll: return "poll";
    }
    return "unknown";
}

/// Strongly types the numeric difficulty values stored by the STB quest.
enum class Difficulty : int {
    kAdventure = 0,
    kTactics = 1,
    kHeroic = 2,
    kTrialOfTheGods = 3,
    kCustom = 4,
};

}  // namespace model

namespace text {

/// Converts a nullable Skyrim string to valid UTF-8 without altering valid input.
[[nodiscard]] std::string FromGameString(const char* value)
{
    if (!value || *value == '\0') return {};
    return IsValidUtf8(value) ? std::string(value) : Cp1251ToUtf8(value);
}

/// Truncates a Discord text field at a UTF-8 code-point boundary.
[[nodiscard]] std::string LimitForDiscord(std::string_view value)
{
    if (value.size() <= constants::kDiscordTextMaxBytes) return std::string(value);

    std::size_t validSize = constants::kDiscordTextMaxBytes;
    while (validSize > 0 &&
           (static_cast<unsigned char>(value[validSize]) & 0xC0) == 0x80) {
        --validSize;
    }
    return std::string(value.substr(0, validSize));
}

/// Tests whether an ASCII pattern occurs in text using case-insensitive comparison.
[[nodiscard]] bool ContainsAsciiInsensitive(
    std::string_view value,
    std::string_view pattern) noexcept
{
    if (pattern.empty()) return true;
    if (pattern.size() > value.size()) return false;

    const auto foldAscii = [](unsigned char character) {
        return character >= 'A' && character <= 'Z'
            ? static_cast<unsigned char>(character + ('a' - 'A'))
            : character;
    };

    for (std::size_t offset = 0; offset + pattern.size() <= value.size(); ++offset) {
        bool matches = true;
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            if (foldAscii(static_cast<unsigned char>(value[offset + index])) !=
                foldAscii(static_cast<unsigned char>(pattern[index]))) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }
    return false;
}

}  // namespace text

namespace configuration {

/// Loads and validates DragonbornPresence.json while preserving safe defaults.
class ConfigLoader final {
public:
    /// Reads the configured file and returns defaults for missing or invalid input.
    [[nodiscard]] static model::Config Load()
    {
        model::Config config;
        std::ifstream file(constants::kConfigPath.data());
        if (!file) {
            SKSE::log::info("Config not found; using defaults.");
            return config;
        }

        try {
            const auto root = nlohmann::json::parse(file);
            if (!root.is_object()) {
                SKSE::log::warn("Config root must be an object; using defaults.");
                return config;
            }

            if (const auto* discordConfig = FindObject(root, "discord", "discord")) {
                ReadBool(*discordConfig, "enabled", config.enabled, "discord");
                ReadApplicationId(*discordConfig, config);
            }
            if (const auto* assets = FindObject(root, "assets", "assets")) {
                ReadString(*assets, "large_image", config.largeImage, "assets");
                ReadString(*assets, "large_text", config.largeText, "assets");
                if (const auto* smallImages =
                        FindObject(*assets, "small_images", "assets.small_images")) {
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
            SKSE::log::info("Config loaded.");
        } catch (const nlohmann::json::exception& error) {
            SKSE::log::error("Failed to parse config JSON: {}", error.what());
            return {};
        }
        return config;
    }

private:
    /// Returns an object-valued child or reports a type mismatch.
    [[nodiscard]] static const nlohmann::json* FindObject(
        const nlohmann::json& parent,
        const char* key,
        std::string_view path)
    {
        const auto value = parent.find(key);
        if (value == parent.end()) return nullptr;
        if (!value->is_object()) {
            SKSE::log::warn("Config: '{}' must be an object; using defaults.", path);
            return nullptr;
        }
        return &*value;
    }

    /// Reads an optional boolean property without replacing its default on error.
    static void ReadBool(
        const nlohmann::json& object,
        const char* key,
        bool& target,
        std::string_view path)
    {
        const auto value = object.find(key);
        if (value == object.end()) return;
        if (!value->is_boolean()) {
            SKSE::log::warn(
                "Config: '{}.{}' must be a boolean; using default.",
                path,
                key);
            return;
        }
        target = value->get<bool>();
    }

    /// Reads an optional string property without replacing its default on error.
    static void ReadString(
        const nlohmann::json& object,
        const char* key,
        std::string& target,
        std::string_view path)
    {
        const auto value = object.find(key);
        if (value == object.end()) return;
        if (!value->is_string()) {
            SKSE::log::warn(
                "Config: '{}.{}' must be a string; using default.",
                path,
                key);
            return;
        }
        target = value->get<std::string>();
    }

    /// Parses a positive Discord application ID from an integer or decimal string.
    static void ReadApplicationId(
        const nlohmann::json& discordConfig,
        model::Config& config)
    {
        const auto value = discordConfig.find("application_id");
        if (value == discordConfig.end()) return;

        if (value->is_number_integer()) {
            const auto parsedId = value->get<std::int64_t>();
            if (parsedId > 0) config.applicationId = parsedId;
            return;
        }
        if (value->is_string()) {
            const std::string encodedId = value->get<std::string>();
            discord::ClientId parsedId = 0;
            const auto [end, error] = std::from_chars(
                encodedId.data(),
                encodedId.data() + encodedId.size(),
                parsedId);
            if (error == std::errc{} && end == encodedId.data() + encodedId.size() &&
                parsedId > 0) {
                config.applicationId = parsedId;
                return;
            }
        }
        SKSE::log::warn(
            "Config: 'discord.application_id' is invalid; using default.");
    }

    /// Parses ordered location-image rules and discards malformed entries.
    static void ReadLocationImageRules(
        const nlohmann::json& assets,
        model::Config& config)
    {
        const auto rules = assets.find("location_images");
        if (rules == assets.end()) return;
        if (!rules->is_array()) {
            SKSE::log::warn(
                "Config: 'assets.location_images' must be an array; using defaults.");
            return;
        }

        std::vector<model::LocationImageRule> parsedRules;
        parsedRules.reserve(rules->size());
        for (std::size_t ruleIndex = 0; ruleIndex < rules->size(); ++ruleIndex) {
            const auto& value = (*rules)[ruleIndex];
            if (!value.is_object()) {
                SKSE::log::warn(
                    "Config: 'assets.location_images[{}]' must be an object; ignoring.",
                    ruleIndex);
                continue;
            }

            model::LocationImageRule rule;
            bool isValid = true;
            const auto readOptionalString = [&](const char* key, std::string& target) {
                const auto field = value.find(key);
                if (field == value.end()) return;
                if (!field->is_string()) {
                    SKSE::log::warn(
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
            if (!isValid || !hasSelector || rule.image.empty()) {
                SKSE::log::warn(
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

}  // namespace configuration

namespace assets {

/// Resolves the first configured Discord image rule matching the player location.
class LocationAssetResolver final {
public:
    /// Binds the resolver to the active immutable configuration.
    explicit LocationAssetResolver(const model::Config& config) noexcept : config_(config) {}

    /// Returns the matched location asset or the configured fallback asset.
    [[nodiscard]] model::LargeAssetSelection Resolve(
        RE::PlayerCharacter* player,
        std::string_view displayLocation) const
    {
        if (player) {
            for (const auto& rule : config_.locationImageRules) {
                if (RuleMatches(rule, player, displayLocation)) {
                    return {
                        rule.image,
                        rule.text.empty() ? std::string_view(config_.largeText)
                                          : std::string_view(rule.text),
                    };
                }
            }
        }
        return {config_.largeImage, config_.largeText};
    }

private:
    /// Compares a form's Editor ID to a configured selector.
    [[nodiscard]] static bool EditorIdEquals(
        const RE::TESForm* form,
        std::string_view expected)
    {
        if (expected.empty()) return true;
        if (!form) return false;
        const char* editorId = form->GetFormEditorID();
        return editorId && expected == editorId;
    }

    /// Checks every non-empty selector in a location-image rule.
    [[nodiscard]] static bool RuleMatches(
        const model::LocationImageRule& rule,
        RE::PlayerCharacter* player,
        std::string_view displayLocation)
    {
        if (!player) return false;
        if (!rule.worldspace.empty() &&
            !EditorIdEquals(player->GetWorldspace(), rule.worldspace)) {
            return false;
        }
        if (!rule.cell.empty() && !EditorIdEquals(player->GetParentCell(), rule.cell)) {
            return false;
        }
        if (!rule.location.empty()) {
            bool found = false;
            for (auto* location = player->GetCurrentLocation(); location;
                 location = location->parentLoc) {
                if (EditorIdEquals(location, rule.location)) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return rule.match.empty() ||
               text::ContainsAsciiInsensitive(displayLocation, rule.match);
    }

    const model::Config& config_;
};

}  // namespace assets

namespace game {

/// Resolves STB forms and creates stable snapshots of the current player state.
class StbDataProvider final {
public:
    /// Resolves and caches all STB forms used by Discord presence.
    void Initialize()
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::error("STB integration: TESDataHandler is unavailable.");
            return;
        }

        runtimeData_.deaths = RE::TESForm::LookupByEditorID<RE::TESGlobal>(
            constants::kDeathsGlobalEditorId.data());
        runtimeData_.difficultyQuest = dataHandler->LookupForm<RE::TESQuest>(
            constants::kDifficultyQuestFormId,
            constants::kStbPluginName.data());

        runtimeData_.stones.clear();
        runtimeData_.stones.reserve(model::kStoneDefinitions.size());
        for (const auto& definition : model::kStoneDefinitions) {
            auto* descriptionSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>(
                definition.descriptionSpellEditorId.data());
            runtimeData_.stones.push_back({
                descriptionSpell,
                ParseStoneName(descriptionSpell, definition.fallbackName),
            });
        }

        SKSE::log::info(
            "STB integration: deaths={} difficulty={} stones={}/{}.",
            runtimeData_.deaths != nullptr,
            runtimeData_.difficultyQuest != nullptr,
            std::ranges::count_if(runtimeData_.stones, [](const auto& stone) {
                return stone.descriptionSpell != nullptr;
            }),
            runtimeData_.stones.size());
    }

    /// Reads all game values required for a single presence refresh.
    [[nodiscard]] model::PlayerSnapshot ReadPlayerSnapshot()
    {
        model::PlayerSnapshot snapshot;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return snapshot;

        snapshot.level = player->GetLevel();
        if (runtimeData_.deaths) {
            snapshot.deaths = static_cast<int>(runtimeData_.deaths->value);
        }
        snapshot.stone = ReadSelectedStone(player);
        snapshot.difficulty = ReadSelectedDifficulty();
        snapshot.location = BuildDisplayPosition(player);
        snapshot.inCombat = player->IsInCombat();
        if (snapshot.inCombat) {
            if (const auto target = player->GetActorRuntimeData().currentCombatTarget.get()) {
                const std::string targetName = text::FromGameString(target->GetName());
                if (!targetName.empty()) lastCombatTargetName_ = targetName;
            }
            snapshot.combatText = lastCombatTargetName_.empty()
                ? std::string(constants::kCombatText)
                : std::format("{} с {}", constants::kCombatText, lastCombatTargetName_);
        } else {
            lastCombatTargetName_.clear();
        }
        return snapshot;
    }

private:
    /// Extracts a localized stone name from the spell description JSON.
    [[nodiscard]] static std::string ParseStoneName(
        RE::SpellItem* descriptionSpell,
        std::string_view fallbackName)
    {
        if (!descriptionSpell) return std::string(fallbackName);

        RE::BSString description;
        descriptionSpell->GetDescription(description, descriptionSpell);
        if (description.empty()) return std::string(fallbackName);

        try {
            const auto root = nlohmann::json::parse(description.c_str());
            const auto name = root.find("name");
            if (name != root.end() && name->is_string()) {
                const auto parsedName = name->get<std::string>();
                if (!parsedName.empty()) return parsedName;
            }
        } catch (const nlohmann::json::exception& error) {
            SKSE::log::warn(
                "STB stone '{}': invalid description JSON: {}",
                descriptionSpell->GetFormEditorID(),
                error.what());
        }
        return std::string(fallbackName);
    }

    /// Maps an STB difficulty enum to the existing localized presence text.
    [[nodiscard]] static std::string_view DifficultyName(int rawDifficulty) noexcept
    {
        switch (static_cast<model::Difficulty>(rawDifficulty)) {
        case model::Difficulty::kAdventure: return "🟢Приключение";
        case model::Difficulty::kTactics: return "🟡Тактика";
        case model::Difficulty::kHeroic: return "🔴Героический";
        case model::Difficulty::kTrialOfTheGods: return "⚫Испытание богов";
        case model::Difficulty::kCustom: return "⚪Свой уровень сложности";
        default: return constants::kUnknownDifficultyText;
        }
    }

    /// Reads the selected STB difficulty from the data-storage quest.
    [[nodiscard]] std::string ReadSelectedDifficulty() const
    {
        const auto difficulty =
            ScriptUtils::GetFirstAliasScriptPropertyOrVariable<int>(
                runtimeData_.difficultyQuest,
                constants::kDifficultyScriptName.data(),
                constants::kDifficultyPropertyName.data());
        return difficulty ? std::string(DifficultyName(*difficulty))
                          : std::string(constants::kUnknownDifficultyText);
    }

    /// Finds the first configured stone spell currently affecting the player.
    [[nodiscard]] std::string ReadSelectedStone(RE::PlayerCharacter* player) const
    {
        if (!player) return std::string(constants::kNoStoneText);
        const auto selectedStone = std::ranges::find_if(
            runtimeData_.stones,
            [player](const auto& stone) {
                return stone.descriptionSpell && player->HasSpell(stone.descriptionSpell);
            });
        return selectedStone != runtimeData_.stones.end()
            ? selectedStone->name
            : std::string(constants::kNoStoneText);
    }

    /// Builds the same worldspace/location/cell display hierarchy used by Discord.
    [[nodiscard]] static std::string BuildDisplayPosition(RE::PlayerCharacter* player)
    {
        if (!player) return {};

        auto* worldspace = player->GetWorldspace();
        auto* location = player->GetCurrentLocation();
        auto* cell = player->GetParentCell();

        std::string locationName;
        for (auto* current = location; current && locationName.empty();
             current = current->parentLoc) {
            locationName = text::FromGameString(current->GetName());
        }

        const std::string worldspaceName =
            worldspace ? text::FromGameString(worldspace->GetName()) : "";
        const std::string cellName = cell ? text::FromGameString(cell->GetName()) : "";
        if (!worldspaceName.empty()) {
            return !locationName.empty() && locationName != worldspaceName
                ? std::format("{}: {}", worldspaceName, locationName)
                : worldspaceName;
        }
        return !locationName.empty() ? locationName : cellName;
    }

    model::StbRuntimeData runtimeData_;
    std::string lastCombatTargetName_;
};

}  // namespace game

namespace integration {

/// Owns the Discord Game SDK connection and suppresses duplicate activities.
class DiscordPresenceClient final {
public:
    /// Creates the Discord SDK core when presence is enabled and available.
    [[nodiscard]] bool Initialize(const model::Config& config)
    {
        if (!config.enabled) {
            SKSE::log::info("Discord presence disabled by configuration.");
            return false;
        }

        discord::Result result;
        try {
            result = discord::Core::Create(
                config.applicationId,
                DiscordCreateFlags_Default,
                &core_);
        } catch (...) {
            SKSE::log::warn(
                "Discord: discord_game_sdk.dll not found — presence disabled.");
            core_ = nullptr;
            return false;
        }
        if (result != discord::Result::Ok) {
            SKSE::log::error(
                "Discord: failed to initialize (result={}).",
                static_cast<int>(result));
            core_ = nullptr;
            return false;
        }

        core_->SetLogHook(
            discord::LogLevel::Warn,
            [](discord::LogLevel, const char* message) {
                SKSE::log::warn("Discord: {}", message);
            });
        SKSE::log::info(
            "Discord Game SDK initialized for application {}.",
            config.applicationId);
        return true;
    }

    /// Runs pending Discord SDK callbacks when the SDK is initialized.
    void RunCallbacks() const
    {
        if (core_) core_->RunCallbacks();
    }

    /// Sends a changed activity after enforcing Discord's UTF-8 field limits.
    void UpdateActivity(const model::ActivityPayload& payload)
    {
        if (!core_) return;

        const std::string detailsText = text::LimitForDiscord(payload.details);
        const std::string stateText = text::LimitForDiscord(payload.state);
        const std::string largeHoverText = text::LimitForDiscord(payload.largeText);
        const std::string smallHoverText = text::LimitForDiscord(payload.smallText);
        const std::array activityFields{
            std::string_view(detailsText),
            std::string_view(stateText),
            payload.largeImage,
            std::string_view(largeHoverText),
            payload.smallImage,
            std::string_view(smallHoverText),
        };

        std::size_t signatureSize = activityFields.size();
        for (const auto field : activityFields) signatureSize += field.size();

        std::string signature;
        signature.reserve(signatureSize);
        for (const auto field : activityFields) {
            signature.append(field);
            signature.push_back('\0');
        }

        if (signature == lastActivitySignature_ || signature == pendingActivitySignature_) {
            return;
        }

        discord::Activity activity{};
        activity.SetType(discord::ActivityType::Playing);
        if (!detailsText.empty()) activity.SetDetails(detailsText.c_str());
        if (!stateText.empty()) activity.SetState(stateText.c_str());
        if (!payload.largeImage.empty()) {
            activity.GetAssets().SetLargeImage(payload.largeImage.data());
        }
        if (!largeHoverText.empty()) {
            activity.GetAssets().SetLargeText(largeHoverText.c_str());
        }
        if (!payload.smallImage.empty()) {
            activity.GetAssets().SetSmallImage(payload.smallImage.data());
        }
        if (!smallHoverText.empty()) {
            activity.GetAssets().SetSmallText(smallHoverText.c_str());
        }

        pendingActivitySignature_ = signature;
        core_->ActivityManager().UpdateActivity(
            activity,
            [this, signature = std::move(signature)](discord::Result result) {
                if (result == discord::Result::Ok) {
                    lastActivitySignature_ = signature;
                    SKSE::log::info("Presence updated.");
                } else {
                    SKSE::log::error(
                        "Discord: UpdateActivity failed (result={}).",
                        static_cast<int>(result));
                }
                if (pendingActivitySignature_ == signature) {
                    pendingActivitySignature_.clear();
                }
            });
    }

private:
    discord::Core* core_ = nullptr;
    std::string lastActivitySignature_;
    std::string pendingActivitySignature_;
};

}  // namespace integration

namespace application {

class PresenceCoordinator;

/// Adapts Skyrim menu events to the application-level presence coordinator.
class MenuEventSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    /// Binds the sink to the coordinator that owns its lifecycle.
    explicit MenuEventSink(PresenceCoordinator& coordinator) noexcept
        : coordinator_(coordinator)
    {}

    /// Forwards valid menu events and always allows later sinks to run.
    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

private:
    PresenceCoordinator& coordinator_;
};

/// Adapts Skyrim combat events to the application-level presence coordinator.
class CombatEventSink final : public RE::BSTEventSink<RE::TESCombatEvent> {
public:
    /// Binds the sink to the coordinator that owns its lifecycle.
    explicit CombatEventSink(PresenceCoordinator& coordinator) noexcept
        : coordinator_(coordinator)
    {}

    /// Forwards valid combat events and always allows later sinks to run.
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCombatEvent* event,
        RE::BSTEventSource<RE::TESCombatEvent>*) override;

private:
    PresenceCoordinator& coordinator_;
};

/// Coordinates configuration, game data, Discord transport, and Skyrim events.
class PresenceCoordinator final {
public:
    /// Constructs the coordinator and binds both event adapters to it.
    PresenceCoordinator() noexcept
        : menuEventSink_(*this), combatEventSink_(*this)
    {}

    /// Replaces the active configuration with validated file contents or defaults.
    void LoadConfig()
    {
        config_ = configuration::ConfigLoader::Load();
    }

    /// Initializes integrations, publishes loading state, and registers event sinks.
    void RegisterGameEventHandlers()
    {
        SKSE::log::info("Registering game event handlers...");
        stbDataProvider_.Initialize();
        if (discordClient_.Initialize(config_)) StartCallbackThread();
        SendLoadingPresence();

        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuEventSink_);
        } else {
            SKSE::log::error("Failed to get RE::UI singleton.");
        }

        if (auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton()) {
            eventSource->AddEventSink<RE::TESCombatEvent>(&combatEventSink_);
        } else {
            SKSE::log::error(
                "Failed to get RE::ScriptEventSourceHolder singleton.");
        }
    }

    /// Marks the game ready and immediately publishes the complete player state.
    void OnGameLoaded()
    {
        SKSE::log::info("Game loaded — refreshing STB presence data.");
        gameLoaded_ = true;
        loading_ = false;
        RefreshPresence(model::RefreshReason::kGameLoaded);
    }

    /// Applies menu transitions to the loading and game-ready state machine.
    void HandleMenuEvent(const RE::MenuOpenCloseEvent& event)
    {
        if (event.menuName == constants::kMainMenuName && event.opening) {
            gameLoaded_ = false;
            SetLoading(true);
        } else if (event.menuName == constants::kLoadingMenuName) {
            if (event.opening) {
                SetLoading(true);
            } else if (gameLoaded_) {
                SetLoading(false);
            }
        }
    }

    /// Schedules a refresh when combat may have changed for the player.
    void HandleCombatEvent(const RE::TESCombatEvent& event)
    {
        if (!gameLoaded_ || loading_) return;

        const bool involvesPlayer =
            (event.actor && event.actor->IsPlayerRef()) ||
            (event.targetActor && event.targetActor->IsPlayerRef());
        const bool mayEndCombat =
            event.newState.get() == RE::ACTOR_COMBAT_STATE::kNone && lastCombatState_;
        if (involvesPlayer || mayEndCombat) {
            SKSE::GetTaskInterface()->AddTask([this]() {
                RefreshPresence(model::RefreshReason::kCombat);
            });
        }
    }

private:
    /// Sends the stable loading activity used before a playable save is ready.
    void SendLoadingPresence()
    {
        discordClient_.UpdateActivity({
            {},
            constants::kLoadingText,
            config_.largeImage,
            config_.largeText,
            config_.loadingImage,
            constants::kLoadingText,
        });
    }

    /// Builds and submits one complete activity from the latest player snapshot.
    void RefreshPresence(model::RefreshReason reason)
    {
        if (loading_ || !gameLoaded_) {
            SendLoadingPresence();
            return;
        }

        const model::PlayerSnapshot snapshot = stbDataProvider_.ReadPlayerSnapshot();
        const std::string deathsText = snapshot.deaths
            ? std::to_string(*snapshot.deaths)
            : std::string(constants::kUnknownDeathsText);
        const std::string detailsText = snapshot.difficulty;
        const std::string stateText = std::format(
            "lvl-{} 💀-{} {}",
            snapshot.level,
            deathsText,
            snapshot.stone);
        const std::string_view smallImage = snapshot.inCombat
            ? std::string_view(config_.combatImage)
            : std::string_view{};
        const std::string_view smallText = snapshot.inCombat
            ? std::string_view(snapshot.combatText)
            : std::string_view{};
        const assets::LocationAssetResolver assetResolver(config_);
        const auto largeAsset = assetResolver.Resolve(
            RE::PlayerCharacter::GetSingleton(),
            snapshot.location);

        lastCombatState_ = snapshot.inCombat;
        SKSE::log::info(
            "[{}] level={} deaths={} stone='{}' difficulty='{}' location='{}' "
            "large='{}' combat='{}'.",
            model::ToLogLabel(reason),
            snapshot.level,
            deathsText,
            snapshot.stone,
            snapshot.difficulty,
            snapshot.location,
            largeAsset.image,
            snapshot.combatText);
        discordClient_.UpdateActivity({
            detailsText,
            stateText,
            largeAsset.image,
            largeAsset.text,
            smallImage,
            smallText,
        });
    }

    /// Updates the loading state and publishes the corresponding presence.
    void SetLoading(bool isLoading)
    {
        loading_ = isLoading;
        if (isLoading) {
            SendLoadingPresence();
        } else if (gameLoaded_) {
            RefreshPresence(model::RefreshReason::kLoadingFinished);
        }
    }

    /// Starts the detached Discord callback and one-second polling loop once.
    void StartCallbackThread()
    {
        if (callbackThreadRunning_.exchange(true)) return;

        std::thread([this]() {
            while (callbackThreadRunning_) {
                std::this_thread::sleep_for(constants::kDiscordCallbackInterval);
                SKSE::GetTaskInterface()->AddTask([this]() {
                    discordClient_.RunCallbacks();
                    if (++presencePollTicks_ >=
                        constants::kPresencePollIntervalInCallbackTicks) {
                        presencePollTicks_ = 0;
                        if (gameLoaded_ && !loading_) {
                            RefreshPresence(model::RefreshReason::kPoll);
                        }
                    }
                });
            }
        }).detach();
    }

    model::Config config_;
    game::StbDataProvider stbDataProvider_;
    integration::DiscordPresenceClient discordClient_;
    MenuEventSink menuEventSink_;
    CombatEventSink combatEventSink_;
    std::atomic<bool> callbackThreadRunning_{false};
    std::uint8_t presencePollTicks_ = 0;
    bool loading_ = true;
    bool gameLoaded_ = false;
    bool lastCombatState_ = false;
};

/// Forwards a menu event to the coordinator when the event pointer is valid.
RE::BSEventNotifyControl MenuEventSink::ProcessEvent(
    const RE::MenuOpenCloseEvent* event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
    if (event) coordinator_.HandleMenuEvent(*event);
    return RE::BSEventNotifyControl::kContinue;
}

/// Forwards a combat event to the coordinator when the event pointer is valid.
RE::BSEventNotifyControl CombatEventSink::ProcessEvent(
    const RE::TESCombatEvent* event,
    RE::BSTEventSource<RE::TESCombatEvent>*)
{
    if (event) coordinator_.HandleCombatEvent(*event);
    return RE::BSEventNotifyControl::kContinue;
}

}  // namespace application

application::PresenceCoordinator g_presenceCoordinator;

}  // namespace

/// Loads DragonbornPresence.json into the process-wide presence coordinator.
void LoadConfig()
{
    g_presenceCoordinator.LoadConfig();
}

/// Initializes Discord, STB data access, and Skyrim event subscriptions.
void RegisterGameEventHandlers()
{
    g_presenceCoordinator.RegisterGameEventHandlers();
}

/// Publishes the first complete presence after Skyrim finishes loading a save.
void OnGameLoaded()
{
    g_presenceCoordinator.OnGameLoaded();
}

}  // namespace DragonbornPresence
