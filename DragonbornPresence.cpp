#include "DragonbornPresence.h"
#include "AdditionalFunctions.h"
#include "ScriptUtils.h"
#include "discord.h"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace DragonbornPresence {

namespace {

constexpr discord::ClientId kDefaultAppId = 1527543892151373937LL;
constexpr std::chrono::milliseconds kCallbackInterval{100};
constexpr std::uint8_t kPresencePollTicks = 10;

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
    StoneDefinition{"aaMZs_DoomstoneSnakeDesc", "🐍-Змея"},
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

struct LocationImageRule {
    std::string worldspace;
    std::string location;
    std::string cell;
    std::string match;
    std::string image;
    std::string text;
};

struct Config {
    bool enabled = true;
    discord::ClientId applicationId = kDefaultAppId;
    std::string largeImage = "stb_logo";
    std::string largeText = "Skyrim True Believer";
    std::string loadingImage = "loading";
    std::string combatImage = "combat";
    std::vector<LocationImageRule> locationImageRules;
};

struct StoneRuntimeData {
    RE::SpellItem* descriptionSpell = nullptr;
    std::string name;
};

struct StbRuntimeData {
    RE::TESGlobal* deaths = nullptr;
    RE::TESQuest* difficultyQuest = nullptr;
    std::vector<StoneRuntimeData> stones;
};

struct PlayerSnapshot {
    int level = 0;
    std::optional<int> deaths;
    std::string stone;
    std::string difficulty;
    std::string location;
    std::string combatText;
    bool inCombat = false;
};

Config g_config;
StbRuntimeData g_stb;
discord::Core* g_core = nullptr;
std::string g_lastActivitySignature;
std::string g_pendingActivitySignature;
std::atomic<bool> g_callbackThreadRunning{false};
bool g_loading = true;
bool g_gameLoaded = false;
bool g_lastCombat = false;
std::string g_lastCombatTargetName;

static std::string SafeStr(const char* value)
{
    if (!value || *value == '\0') return {};
    return IsValidUtf8(value) ? std::string(value) : Cp1251ToUtf8(value);
}

static std::string LimitDiscordText(std::string_view text)
{
    constexpr std::size_t kMaxBytes = 127;
    if (text.size() <= kMaxBytes) return std::string(text);

    std::size_t size = kMaxBytes;
    while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80) {
        --size;
    }
    return std::string(text.substr(0, size));
}

static const nlohmann::json* FindObject(
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

static void ReadBool(
    const nlohmann::json& object,
    const char* key,
    bool& target,
    std::string_view path)
{
    const auto value = object.find(key);
    if (value == object.end()) return;
    if (!value->is_boolean()) {
        SKSE::log::warn("Config: '{}.{}' must be a boolean; using default.", path, key);
        return;
    }
    target = value->get<bool>();
}

static void ReadString(
    const nlohmann::json& object,
    const char* key,
    std::string& target,
    std::string_view path)
{
    const auto value = object.find(key);
    if (value == object.end()) return;
    if (!value->is_string()) {
        SKSE::log::warn("Config: '{}.{}' must be a string; using default.", path, key);
        return;
    }
    target = value->get<std::string>();
}

static void ReadApplicationId(const nlohmann::json& discord)
{
    const auto value = discord.find("application_id");
    if (value == discord.end()) return;

    if (value->is_number_integer()) {
        const auto parsed = value->get<std::int64_t>();
        if (parsed > 0) g_config.applicationId = parsed;
        return;
    }
    if (value->is_string()) {
        const std::string text = value->get<std::string>();
        discord::ClientId parsed = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (error == std::errc{} && end == text.data() + text.size() && parsed > 0) {
            g_config.applicationId = parsed;
            return;
        }
    }
    SKSE::log::warn("Config: 'discord.application_id' is invalid; using default.");
}

static void ReadLocationImageRules(const nlohmann::json& assets)
{
    const auto rules = assets.find("location_images");
    if (rules == assets.end()) return;
    if (!rules->is_array()) {
        SKSE::log::warn("Config: 'assets.location_images' must be an array; using defaults.");
        return;
    }

    std::vector<LocationImageRule> parsed;
    parsed.reserve(rules->size());
    for (std::size_t index = 0; index < rules->size(); ++index) {
        const auto& value = (*rules)[index];
        if (!value.is_object()) {
            SKSE::log::warn(
                "Config: 'assets.location_images[{}]' must be an object; ignoring.",
                index);
            continue;
        }

        LocationImageRule rule;
        bool valid = true;
        const auto readOptional = [&](const char* key, std::string& target) {
            const auto field = value.find(key);
            if (field == value.end()) return;
            if (!field->is_string()) {
                SKSE::log::warn(
                    "Config: 'assets.location_images[{}].{}' must be a string; ignoring rule.",
                    index,
                    key);
                valid = false;
                return;
            }
            target = field->get<std::string>();
        };

        readOptional("worldspace", rule.worldspace);
        readOptional("location", rule.location);
        readOptional("cell", rule.cell);
        readOptional("match", rule.match);
        readOptional("image", rule.image);
        readOptional("text", rule.text);

        const bool hasSelector =
            !rule.worldspace.empty() || !rule.location.empty() ||
            !rule.cell.empty() || !rule.match.empty();
        if (!valid || !hasSelector || rule.image.empty()) {
            SKSE::log::warn(
                "Config: 'assets.location_images[{}]' requires 'image' and a selector; ignoring.",
                index);
            continue;
        }
        parsed.push_back(std::move(rule));
    }
    g_config.locationImageRules = std::move(parsed);
}

static bool ContainsAsciiInsensitive(std::string_view text, std::string_view pattern)
{
    if (pattern.empty()) return true;
    if (pattern.size() > text.size()) return false;

    const auto fold = [](unsigned char value) {
        return value >= 'A' && value <= 'Z'
            ? static_cast<unsigned char>(value + ('a' - 'A'))
            : value;
    };
    for (std::size_t offset = 0; offset + pattern.size() <= text.size(); ++offset) {
        bool matches = true;
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            if (fold(static_cast<unsigned char>(text[offset + index])) !=
                fold(static_cast<unsigned char>(pattern[index]))) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }
    return false;
}

static bool EditorIdEquals(const RE::TESForm* form, std::string_view expected)
{
    if (expected.empty()) return true;
    if (!form) return false;
    const char* editorId = form->GetFormEditorID();
    return editorId && expected == editorId;
}

static bool LocationRuleMatches(
    const LocationImageRule& rule,
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
        for (auto* location = player->GetCurrentLocation();
             location;
             location = location->parentLoc) {
            if (EditorIdEquals(location, rule.location)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return rule.match.empty() || ContainsAsciiInsensitive(displayLocation, rule.match);
}

static std::string BuildPosition(RE::PlayerCharacter* player)
{
    if (!player) return {};

    auto* worldspace = player->GetWorldspace();
    auto* location = player->GetCurrentLocation();
    auto* cell = player->GetParentCell();

    std::string locationName;
    for (auto* current = location; current && locationName.empty(); current = current->parentLoc) {
        locationName = SafeStr(current->GetName());
    }

    const std::string worldspaceName = worldspace ? SafeStr(worldspace->GetName()) : "";
    const std::string cellName = cell ? SafeStr(cell->GetName()) : "";
    if (!worldspaceName.empty()) {
        return !locationName.empty() && locationName != worldspaceName
            ? worldspaceName + ": " + locationName
            : worldspaceName;
    }
    return !locationName.empty() ? locationName : cellName;
}

struct LargeAssetSelection {
    std::string_view image;
    std::string_view text;
};

static LargeAssetSelection ResolveLargeAsset(
    RE::PlayerCharacter* player,
    std::string_view displayLocation)
{
    if (player) {
        for (const auto& rule : g_config.locationImageRules) {
            if (LocationRuleMatches(rule, player, displayLocation)) {
                return {
                    rule.image,
                    rule.text.empty() ? std::string_view(g_config.largeText)
                                      : std::string_view(rule.text)
                };
            }
        }
    }
    return {g_config.largeImage, g_config.largeText};
}

static std::string ParseStoneName(RE::SpellItem* descriptionSpell, std::string_view fallback)
{
    if (!descriptionSpell) return std::string(fallback);

    RE::BSString description;
    descriptionSpell->GetDescription(description, descriptionSpell);
    if (description.empty()) return std::string(fallback);

    try {
        const auto root = nlohmann::json::parse(description.c_str());
        const auto name = root.find("name");
        if (name != root.end() && name->is_string()) {
            const auto parsed = name->get<std::string>();
            if (!parsed.empty()) return parsed;
        }
    } catch (const nlohmann::json::exception& error) {
        SKSE::log::warn(
            "STB stone '{}': invalid description JSON: {}",
            descriptionSpell->GetFormEditorID(),
            error.what());
    }
    return std::string(fallback);
}

static void InitializeStbData()
{
    auto* data = RE::TESDataHandler::GetSingleton();
    if (!data) {
        SKSE::log::error("STB integration: TESDataHandler is unavailable.");
        return;
    }

    g_stb.deaths = RE::TESForm::LookupByEditorID<RE::TESGlobal>("aaMZgv_NowDeath");
    g_stb.difficultyQuest = data->LookupForm<RE::TESQuest>(0x1417C4, "STB.esp");

    g_stb.stones.clear();
    g_stb.stones.reserve(kStoneDefinitions.size());
    for (const auto& definition : kStoneDefinitions) {
        auto* spell = RE::TESForm::LookupByEditorID<RE::SpellItem>(
            definition.descriptionSpellEditorId.data());
        g_stb.stones.push_back({spell, ParseStoneName(spell, definition.fallbackName)});
    }

    SKSE::log::info(
        "STB integration: deaths={} difficulty={} stones={}/{}.",
        g_stb.deaths != nullptr,
        g_stb.difficultyQuest != nullptr,
        std::ranges::count_if(g_stb.stones, [](const auto& stone) {
            return stone.descriptionSpell != nullptr;
        }),
        g_stb.stones.size());
}

static std::string DifficultyName(int index)
{
    switch (index) {
    case 0: return "🟢Приключение";
    case 1: return "🟡Тактика";
    case 2: return "🔴Героический";
    case 3: return "⚫Испытание богов";
    case 4: return "⚪Свой уровень сложности";
    default: return "не определена";
    }
}

static std::string SelectedDifficulty()
{
    const auto value = ScriptUtils::GetFirstAliasScriptPropertyOrVariable<int>(
        g_stb.difficultyQuest,
        "aamz_mcmdatastorage",
        "aaMZ_SelectedLevel_OfDifficulty");
    return value ? DifficultyName(*value) : "не определена";
}

static std::string SelectedStone(RE::PlayerCharacter* player)
{
    if (!player) return "не выбран";
    const auto stone = std::ranges::find_if(g_stb.stones, [player](const auto& value) {
        return value.descriptionSpell && player->HasSpell(value.descriptionSpell);
    });
    return stone != g_stb.stones.end() ? stone->name : "не выбран";
}


static PlayerSnapshot ReadPlayerSnapshot()
{
    PlayerSnapshot snapshot;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return snapshot;

    snapshot.level = player->GetLevel();
    if (g_stb.deaths) snapshot.deaths = static_cast<int>(g_stb.deaths->value);
    snapshot.stone = SelectedStone(player);
    snapshot.difficulty = SelectedDifficulty();
    snapshot.location = BuildPosition(player);
    snapshot.inCombat = player->IsInCombat();
    if (snapshot.inCombat) {
        if (const auto target = player->GetActorRuntimeData().currentCombatTarget.get()) {
            const std::string targetName = SafeStr(target->GetName());
            if (!targetName.empty()) g_lastCombatTargetName = targetName;
        }
        snapshot.combatText = g_lastCombatTargetName.empty()
            ? "В бою"
            : "В бою с " + g_lastCombatTargetName;
    } else {
        g_lastCombatTargetName.clear();
    }
    return snapshot;
}

static void SendActivity(
    std::string_view details,
    std::string_view state,
    std::string_view largeImage,
    std::string_view largeText,
    std::string_view smallImage,
    std::string_view smallText)
{
    if (!g_core) return;

    const std::string detailsText = LimitDiscordText(details);
    const std::string stateText = LimitDiscordText(state);
    const std::string hoverText = LimitDiscordText(largeText);
    const std::string smallHoverText = LimitDiscordText(smallText);

    std::string signature;
    signature.reserve(
        detailsText.size() + stateText.size() + largeImage.size() +
        hoverText.size() + smallImage.size() + smallHoverText.size() + 6);
    for (const auto value : {
             std::string_view(detailsText),
             std::string_view(stateText),
             largeImage,
             std::string_view(hoverText),
             smallImage,
             std::string_view(smallHoverText)}) {
        signature.append(value);
        signature.push_back('\0');
    }

    if (signature == g_lastActivitySignature || signature == g_pendingActivitySignature) return;

    discord::Activity activity{};
    activity.SetType(discord::ActivityType::Playing);
    if (!detailsText.empty()) activity.SetDetails(detailsText.c_str());
    if (!stateText.empty()) activity.SetState(stateText.c_str());
    if (!largeImage.empty()) activity.GetAssets().SetLargeImage(largeImage.data());
    if (!hoverText.empty()) activity.GetAssets().SetLargeText(hoverText.c_str());
    if (!smallImage.empty()) activity.GetAssets().SetSmallImage(smallImage.data());
    if (!smallHoverText.empty())
        activity.GetAssets().SetSmallText(smallHoverText.c_str());

    g_pendingActivitySignature = signature;
    g_core->ActivityManager().UpdateActivity(
        activity,
        [signature = std::move(signature)](discord::Result result) {
            if (result == discord::Result::Ok) {
                g_lastActivitySignature = signature;
                SKSE::log::info("Presence updated.");
            } else {
                SKSE::log::error(
                    "Discord: UpdateActivity failed (result={}).",
                    static_cast<int>(result));
            }
            if (g_pendingActivitySignature == signature) {
                g_pendingActivitySignature.clear();
            }
        });
}

static void SendLoadingPresence()
{
    SendActivity(
        {},
        "Загрузка",
        g_config.largeImage,
        g_config.largeText,
        g_config.loadingImage,
        "Загрузка");
}

static void RefreshPresence(const char* trigger)
{
    if (g_loading || !g_gameLoaded) {
        SendLoadingPresence();
        return;
    }

    const PlayerSnapshot snapshot = ReadPlayerSnapshot();
    const std::string deaths = snapshot.deaths
        ? std::to_string(*snapshot.deaths)
        : "—";
    const std::string firstLine = std::format(
        "{}",
        snapshot.difficulty);
    const std::string secondLine = std::format(
        "lvl-{} 💀-{} {}",
        snapshot.level,
        deaths,
        snapshot.stone);
    const std::string_view smallImage = snapshot.inCombat
        ? std::string_view(g_config.combatImage)
        : std::string_view{};
    const std::string_view smallText = snapshot.inCombat
        ? std::string_view(snapshot.combatText)
        : std::string_view{};
    const auto largeAsset = ResolveLargeAsset(
        RE::PlayerCharacter::GetSingleton(),
        snapshot.location);

    g_lastCombat = snapshot.inCombat;
    SKSE::log::info(
        "[{}] level={} deaths={} stone='{}' difficulty='{}' location='{}' "
        "large='{}' combat='{}'.",
        trigger,
        snapshot.level,
        deaths,
        snapshot.stone,
        snapshot.difficulty,
        snapshot.location,
        largeAsset.image,
        snapshot.combatText);
    SendActivity(
        firstLine,
        secondLine,
        largeAsset.image,
        largeAsset.text,
        smallImage,
        smallText);
}

static void SetLoading(bool loading)
{
    g_loading = loading;
    if (loading) {
        SendLoadingPresence();
    } else if (g_gameLoaded) {
        RefreshPresence("loading-finished");
    }
}

class MenuEventSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        if (!event) return RE::BSEventNotifyControl::kContinue;

        if (event->menuName == "Main Menu" && event->opening) {
            g_gameLoaded = false;
            SetLoading(true);
        } else if (event->menuName == "Loading Menu") {
            if (event->opening) {
                SetLoading(true);
            } else if (g_gameLoaded) {
                SetLoading(false);
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

class CombatSink final : public RE::BSTEventSink<RE::TESCombatEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCombatEvent* event,
        RE::BSTEventSource<RE::TESCombatEvent>*) override
    {
        if (!event || !g_gameLoaded || g_loading) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const bool involvesPlayer =
            (event->actor && event->actor->IsPlayerRef()) ||
            (event->targetActor && event->targetActor->IsPlayerRef());
        const bool mayEndCombat =
            event->newState.get() == RE::ACTOR_COMBAT_STATE::kNone && g_lastCombat;
        if (involvesPlayer || mayEndCombat) {
            SKSE::GetTaskInterface()->AddTask([]() {
                RefreshPresence("combat");
            });
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

MenuEventSink g_menuSink;
CombatSink g_combatSink;

static void StartCallbackThread()
{
    if (g_callbackThreadRunning.exchange(true)) return;

    std::thread([]() {
        while (g_callbackThreadRunning) {
            std::this_thread::sleep_for(kCallbackInterval);
            SKSE::GetTaskInterface()->AddTask([]() {
                if (g_core) g_core->RunCallbacks();

                static std::uint8_t pollTicks = 0;
                if (++pollTicks >= kPresencePollTicks) {
                    pollTicks = 0;
                    if (g_gameLoaded && !g_loading) {
                        RefreshPresence("poll");
                    }
                }
            });
        }
    }).detach();
}

static void InitDiscord()
{
    if (!g_config.enabled) {
        SKSE::log::info("Discord presence disabled by configuration.");
        return;
    }

    discord::Result result;
    try {
        result = discord::Core::Create(
            g_config.applicationId,
            DiscordCreateFlags_Default,
            &g_core);
    } catch (...) {
        SKSE::log::warn(
            "Discord: discord_game_sdk.dll not found — presence disabled.");
        g_core = nullptr;
        return;
    }
    if (result != discord::Result::Ok) {
        SKSE::log::error(
            "Discord: failed to initialize (result={}).",
            static_cast<int>(result));
        g_core = nullptr;
        return;
    }

    g_core->SetLogHook(discord::LogLevel::Warn, [](discord::LogLevel, const char* message) {
        SKSE::log::warn("Discord: {}", message);
    });
    SKSE::log::info(
        "Discord Game SDK initialized for application {}.",
        g_config.applicationId);
    StartCallbackThread();
}

}  // namespace

void LoadConfig()
{
    g_config = Config{};

    std::ifstream file(R"(Data\SKSE\Plugins\DragonbornPresence.json)");
    if (!file) {
        SKSE::log::info("Config not found; using defaults.");
        return;
    }

    try {
        const auto root = nlohmann::json::parse(file);
        if (!root.is_object()) {
            SKSE::log::warn("Config root must be an object; using defaults.");
            return;
        }

        if (const auto* discord = FindObject(root, "discord", "discord")) {
            ReadBool(*discord, "enabled", g_config.enabled, "discord");
            ReadApplicationId(*discord);
        }
        if (const auto* assets = FindObject(root, "assets", "assets")) {
            ReadString(*assets, "large_image", g_config.largeImage, "assets");
            ReadString(*assets, "large_text", g_config.largeText, "assets");
            if (const auto* smallImages = FindObject(
                    *assets,
                    "small_images",
                    "assets.small_images")) {
                ReadString(
                    *smallImages,
                    "loading",
                    g_config.loadingImage,
                    "assets.small_images");
                ReadString(
                    *smallImages,
                    "combat",
                    g_config.combatImage,
                    "assets.small_images");
            }
            ReadLocationImageRules(*assets);
        }
        SKSE::log::info("Config loaded.");
    } catch (const nlohmann::json::exception& error) {
        SKSE::log::error("Failed to parse config JSON: {}", error.what());
        g_config = Config{};
    }
}

void RegisterGameEventHandlers()
{
    SKSE::log::info("Registering game event handlers...");
    InitializeStbData();
    InitDiscord();
    SendLoadingPresence();

    if (auto* ui = RE::UI::GetSingleton()) {
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_menuSink);
    } else {
        SKSE::log::error("Failed to get RE::UI singleton.");
    }

    if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
        events->AddEventSink<RE::TESCombatEvent>(&g_combatSink);
    } else {
        SKSE::log::error("Failed to get RE::ScriptEventSourceHolder singleton.");
    }
}

void OnGameLoaded()
{
    SKSE::log::info("Game loaded — refreshing STB presence data.");
    g_gameLoaded = true;
    g_loading = false;
    RefreshPresence("game-loaded");
}

}  // namespace DragonbornPresence
