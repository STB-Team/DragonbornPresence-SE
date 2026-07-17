#include "DragonbornPresence.h"
#include "AdditionalFunctions.h"
#include "discord.h"
#include <atomic>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <thread>
#include <nlohmann/json.hpp>

namespace DragonbornPresence {

namespace {

constexpr discord::ClientId kDefaultAppId = 565627104608256015LL;

enum class State { Loading, MainMenu, EditingCharacter, Playing };
enum class PresenceMode {
    Loading,
    MainMenu,
    EditingCharacter,
    Exploring,
    Quest,
    Combat,
    Menu,
    Map,
    Inventory,
    Dialogue,
    Crafting,
    Alchemy,
    Smithing,
    Enchanting,
    Cooking,
    Tanning,
    Smelting,
    Waiting
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
    bool              enabled              = true;
    discord::ClientId applicationId        = kDefaultAppId;
    bool              showElapsedTime      = true;
    bool              showCharacterDetails = true;
    bool              showLocation         = true;
    bool              showQuest            = true;
    bool              showCombat           = true;
    bool              showUiState          = true;
    std::string       separator            = " \xC2\xB7 ";
    std::string       largeImage           = "skyrim_logo";
    std::string       largeText            = "The Elder Scrolls V: Skyrim";
    std::string       loadingLargeImage;
    std::string       mainMenuLargeImage;
    std::string       editingCharacterLargeImage;
    std::string       playingLargeImage;
    std::string       loadingImage;
    std::string       mainMenuImage;
    std::string       editingCharacterImage;
    std::string       exploringImage;
    std::string       questImage;
    std::string       combatImage;
    std::string       menuImage;
    std::string       mapImage;
    std::string       inventoryImage;
    std::string       dialogueImage;
    std::string       craftingImage;
    std::string       alchemyImage;
    std::string       smithingImage;
    std::string       enchantingImage;
    std::string       cookingImage;
    std::string       tanningImage;
    std::string       smeltingImage;
    std::string       waitingImage;
    std::vector<LocationImageRule> locationImageRules;
};

Config         g_config;
State          g_state             = State::Loading;
discord::Core* g_core              = nullptr;
int64_t        g_startTime         = 0;
std::unordered_map<std::string, std::string> g_locale = {
    {"main_menu",         "Main menu"},
    {"editing_character", "Editing character"},
    {"loading",           "Loading"},
    {"exploring",         "Exploring"},
    {"active_quest",      "Active quest"},
    {"combat_fighting",   "In combat with {name}"},
    {"combat_no_target",  "In combat"},
    {"in_menu",           "In menus"},
    {"viewing_map",       "Viewing map"},
    {"inventory",         "Managing inventory"},
    {"dialogue",          "In dialogue"},
    {"crafting",          "Crafting"},
    {"crafting_alchemy",    "Brewing potions"},
    {"crafting_smithing",   "Smithing"},
    {"crafting_enchanting", "Enchanting"},
    {"crafting_cooking",    "Cooking"},
    {"crafting_tanning",    "Tanning hides"},
    {"crafting_smelting",   "Smelting ore"},
    {"waiting",           "Waiting"},
};
std::string g_lastPosition;
std::string g_combatTarget;
std::string g_lastActivitySignature;
std::string g_pendingActivitySignature;
std::unordered_set<std::string> g_openContextMenus;

static std::string SafeStr(const char* s) {
    if (!s || *s == '\0') return "";
    return IsValidUtf8(s) ? std::string(s) : Cp1251ToUtf8(s);
}

static const std::string& Locale(const std::string& key) {
    static const std::string kFallback;
    auto it = g_locale.find(key);
    return it != g_locale.end() ? it->second : kFallback;
}

static const nlohmann::json* FindObject(
    const nlohmann::json& parent,
    const char* key,
    std::string_view path)
{
    auto it = parent.find(key);
    if (it == parent.end()) return nullptr;
    if (!it->is_object()) {
        SKSE::log::warn("Config: '{}' must be an object; using defaults.", path);
        return nullptr;
    }
    return &*it;
}

static void ReadBool(
    const nlohmann::json& object,
    const char* key,
    bool& target,
    std::string_view path)
{
    auto it = object.find(key);
    if (it == object.end()) return;
    if (!it->is_boolean()) {
        SKSE::log::warn("Config: '{}.{}' must be a boolean; using default.", path, key);
        return;
    }
    target = it->get<bool>();
}

static void ReadString(
    const nlohmann::json& object,
    const char* key,
    std::string& target,
    std::string_view path)
{
    auto it = object.find(key);
    if (it == object.end()) return;
    if (!it->is_string()) {
        SKSE::log::warn("Config: '{}.{}' must be a string; using default.", path, key);
        return;
    }
    target = it->get<std::string>();
}

static void ReadApplicationId(const nlohmann::json& object) {
    auto it = object.find("application_id");
    if (it == object.end()) return;
    if (!it->is_string()) {
        SKSE::log::warn("Config: 'discord.application_id' must be a string; using default.");
        return;
    }

    const auto& text = it->get_ref<const std::string&>();
    discord::ClientId value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value <= 0) {
        SKSE::log::warn("Config: invalid Discord application ID '{}'; using default.", text);
        return;
    }
    g_config.applicationId = value;
}

static void ReadLocationImageRules(const nlohmann::json& assets) {
    auto rules = assets.find("location_images");
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
            auto field = value.find(key);
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
                "Config: 'assets.location_images[{}]' requires 'image' and at least one "
                "of 'worldspace', 'location', 'cell', or 'match'; ignoring.",
                index);
            continue;
        }
        parsed.push_back(std::move(rule));
    }
    g_config.locationImageRules = std::move(parsed);
}

static std::string LimitDiscordText(std::string_view text) {
    constexpr std::size_t kMaxBytes = 127;
    if (text.size() <= kMaxBytes) return std::string(text);

    std::size_t size = kMaxBytes;
    while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80)
        --size;
    return std::string(text.substr(0, size));
}

struct LargeAssetSelection {
    std::string_view image;
    std::string_view text;
};

static bool ContainsAsciiInsensitive(std::string_view text, std::string_view pattern) {
    if (pattern.empty()) return true;
    if (pattern.size() > text.size()) return false;

    const auto fold = [](unsigned char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A'))
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

static bool EditorIdEquals(const RE::TESForm* form, std::string_view expected) {
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
        for (auto* location = player->GetCurrentLocation(); location; location = location->parentLoc) {
            if (EditorIdEquals(location, rule.location)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return rule.match.empty() || ContainsAsciiInsensitive(displayLocation, rule.match);
}

static const std::string& LargeImage(PresenceMode mode) {
    const std::string* overrideImage = nullptr;
    switch (mode) {
    case PresenceMode::Loading:
        overrideImage = &g_config.loadingLargeImage;
        break;
    case PresenceMode::MainMenu:
        overrideImage = &g_config.mainMenuLargeImage;
        break;
    case PresenceMode::EditingCharacter:
        overrideImage = &g_config.editingCharacterLargeImage;
        break;
    default:
        overrideImage = &g_config.playingLargeImage;
        break;
    }
    return overrideImage->empty() ? g_config.largeImage : *overrideImage;
}

static LargeAssetSelection ResolveLargeAsset(
    PresenceMode mode,
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
    return {LargeImage(mode), g_config.largeText};
}

static const std::string& SpecificCraftingImage(const std::string& image) {
    return image.empty() ? g_config.craftingImage : image;
}

static const std::string& SmallImage(PresenceMode mode) {
    switch (mode) {
    case PresenceMode::Loading:          return g_config.loadingImage;
    case PresenceMode::MainMenu:         return g_config.mainMenuImage;
    case PresenceMode::EditingCharacter: return g_config.editingCharacterImage;
    case PresenceMode::Exploring:        return g_config.exploringImage;
    case PresenceMode::Quest:            return g_config.questImage;
    case PresenceMode::Combat:           return g_config.combatImage;
    case PresenceMode::Menu:             return g_config.menuImage;
    case PresenceMode::Map:              return g_config.mapImage;
    case PresenceMode::Inventory:        return g_config.inventoryImage;
    case PresenceMode::Dialogue:         return g_config.dialogueImage;
    case PresenceMode::Crafting:         return g_config.craftingImage;
    case PresenceMode::Alchemy:          return SpecificCraftingImage(g_config.alchemyImage);
    case PresenceMode::Smithing:         return SpecificCraftingImage(g_config.smithingImage);
    case PresenceMode::Enchanting:       return SpecificCraftingImage(g_config.enchantingImage);
    case PresenceMode::Cooking:          return SpecificCraftingImage(g_config.cookingImage);
    case PresenceMode::Tanning:          return SpecificCraftingImage(g_config.tanningImage);
    case PresenceMode::Smelting:         return SpecificCraftingImage(g_config.smeltingImage);
    case PresenceMode::Waiting:          return g_config.waitingImage;
    }
    return g_config.exploringImage;
}

static std::string SmallText(PresenceMode mode) {
    switch (mode) {
    case PresenceMode::Loading:          return Locale("loading");
    case PresenceMode::MainMenu:         return Locale("main_menu");
    case PresenceMode::EditingCharacter: return Locale("editing_character");
    case PresenceMode::Exploring:        return Locale("exploring");
    case PresenceMode::Quest:            return Locale("active_quest");
    case PresenceMode::Combat:
        return g_combatTarget.empty() ? Locale("combat_no_target") : g_combatTarget;
    case PresenceMode::Menu:      return Locale("in_menu");
    case PresenceMode::Map:       return Locale("viewing_map");
    case PresenceMode::Inventory: return Locale("inventory");
    case PresenceMode::Dialogue:  return Locale("dialogue");
    case PresenceMode::Crafting:  return Locale("crafting");
    case PresenceMode::Alchemy:    return Locale("crafting_alchemy");
    case PresenceMode::Smithing:   return Locale("crafting_smithing");
    case PresenceMode::Enchanting: return Locale("crafting_enchanting");
    case PresenceMode::Cooking:    return Locale("crafting_cooking");
    case PresenceMode::Tanning:    return Locale("crafting_tanning");
    case PresenceMode::Smelting:   return Locale("crafting_smelting");
    case PresenceMode::Waiting:   return Locale("waiting");
    }
    return {};
}

// Formats the combat string for a known enemy name.
// If the template contains {name}, replaces it; otherwise appends " " + name (legacy fallback).
static std::string FormatCombat(const std::string& tmpl, const std::string& name) {
    constexpr std::string_view kPlaceholder = "{name}";
    auto pos = tmpl.find(kPlaceholder);
    if (pos == std::string::npos)
        return tmpl + " " + name;
    std::string result = tmpl;
    result.replace(pos, kPlaceholder.size(), name);
    return result;
}

static std::string BuildPosition(RE::PlayerCharacter* player) {
    auto* ws   = player->GetWorldspace();
    auto* loc  = player->GetCurrentLocation();
    auto* cell = player->GetParentCell();

    std::string locName;
    for (auto* l = loc; l && locName.empty(); l = l->parentLoc)
        locName = SafeStr(l->GetName());

    std::string wsName   = ws   ? SafeStr(ws->GetName())   : "";
    std::string cellName = cell ? SafeStr(cell->GetName()) : "";

if (!wsName.empty())
        return (!locName.empty() && locName != wsName) ? wsName + ": " + locName : wsName;
    if (!locName.empty()) return locName;
    return cellName;
}

static std::string BuildActiveQuest(RE::PlayerCharacter* player) {
    if (REL::Module::IsVR()) return "";  // objectives not mapped for VR

    // objectives sits at 0x580 from PlayerCharacter* in SE/older AE,
    // shifted +8 to 0x588 for AE 1.6.629+ (PLAYER_RUNTIME_DATA base moves from 0x3D8 → 0x3E0)
    auto& objectives = REL::RelocateMemberIfNewer<RE::BSTArray<RE::BGSInstancedQuestObjective>>(
        SKSE::RUNTIME_SSE_1_6_629, player, 0x580, 0x588);

    RE::TESQuest* best = nullptr;
    for (const auto& instObj : objectives) {
        if (instObj.InstanceState != RE::QUEST_OBJECTIVE_STATE::kDisplayed) continue;
        auto* quest = instObj.Objective ? instObj.Objective->ownerQuest : nullptr;
        if (!quest) continue;
        if (!best || quest->data.priority > best->data.priority)
            best = quest;
    }
    return best ? SafeStr(best->GetFullName()) : "";
}

static std::string BuildPlayerInfo(RE::PlayerCharacter* player) {
    if (!g_config.showCharacterDetails) return "";

    auto* race = player->GetRace();
    // GetName() on the actor returns the display name (updated after char edit);
    // GetActorBase()->GetName() can lag behind by several frames.
    std::string name     = SafeStr(player->GetName());
    std::string raceName = race ? SafeStr(race->GetName()) : "";
    return name + " - " + raceName + " (" + std::to_string(player->GetLevel()) + ")";
}

static PresenceMode CurrentCraftingMode() {
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return PresenceMode::Crafting;

    const auto menu = ui->GetMenu<RE::CraftingMenu>();
    const auto* subMenu = menu ? menu->GetCraftingSubMenu() : nullptr;
    if (skyrim_cast<const RE::CraftingSubMenus::AlchemyMenu*>(subMenu))
        return PresenceMode::Alchemy;
    if (skyrim_cast<const RE::CraftingSubMenus::EnchantConstructMenu*>(subMenu))
        return PresenceMode::Enchanting;
    if (skyrim_cast<const RE::CraftingSubMenus::SmithingMenu*>(subMenu))
        return PresenceMode::Smithing;

    const auto* furniture = subMenu ? subMenu->furniture : nullptr;
    if (!furniture) return PresenceMode::Crafting;

    using BenchType = RE::TESFurniture::WorkBenchData::BenchType;
    switch (furniture->workBenchData.benchType.get()) {
    case BenchType::kAlchemy:
    case BenchType::kAlchemyExperiment:
        return PresenceMode::Alchemy;
    case BenchType::kEnchanting:
    case BenchType::kEnchantingExperiment:
        return PresenceMode::Enchanting;
    case BenchType::kSmithingWeapon:
    case BenchType::kSmithingArmor:
        return PresenceMode::Smithing;
    case BenchType::kCreateObject:
        break;
    default:
        return PresenceMode::Crafting;
    }

    const std::string editorId = SafeStr(furniture->GetFormEditorID());
    if (ContainsAsciiInsensitive(editorId, "cook") ||
        ContainsAsciiInsensitive(editorId, "oven")) {
        return PresenceMode::Cooking;
    }
    if (ContainsAsciiInsensitive(editorId, "tanning") ||
        ContainsAsciiInsensitive(editorId, "tanrack")) {
        return PresenceMode::Tanning;
    }
    if (ContainsAsciiInsensitive(editorId, "smelter") ||
        ContainsAsciiInsensitive(editorId, "smelting")) {
        return PresenceMode::Smelting;
    }
    if (furniture->workBenchData.usesSkill.get() == RE::ActorValue::kSmithing ||
        ContainsAsciiInsensitive(editorId, "forge") ||
        ContainsAsciiInsensitive(editorId, "smith") ||
        ContainsAsciiInsensitive(editorId, "grindstone") ||
        ContainsAsciiInsensitive(editorId, "armorbench")) {
        return PresenceMode::Smithing;
    }
    return PresenceMode::Crafting;
}

static std::optional<PresenceMode> ContextModeForMenu(std::string_view menu) {
    if (menu == RE::DialogueMenu::MENU_NAME) return PresenceMode::Dialogue;
    if (menu == RE::CraftingMenu::MENU_NAME) return CurrentCraftingMode();
    if (menu == RE::MapMenu::MENU_NAME) return PresenceMode::Map;
    if (menu == RE::SleepWaitMenu::MENU_NAME) return PresenceMode::Waiting;
    if (menu == RE::InventoryMenu::MENU_NAME ||
        menu == RE::MagicMenu::MENU_NAME ||
        menu == RE::FavoritesMenu::MENU_NAME ||
        menu == RE::StatsMenu::MENU_NAME ||
        menu == RE::BarterMenu::MENU_NAME ||
        menu == RE::ContainerMenu::MENU_NAME) {
        return PresenceMode::Inventory;
    }
    if (menu == RE::JournalMenu::MENU_NAME || menu == RE::TweenMenu::MENU_NAME) {
        return PresenceMode::Menu;
    }
    return std::nullopt;
}

static int ContextPriority(PresenceMode mode) {
    switch (mode) {
    case PresenceMode::Dialogue:  return 6;
    case PresenceMode::Crafting:  return 5;
    case PresenceMode::Alchemy:
    case PresenceMode::Smithing:
    case PresenceMode::Enchanting:
    case PresenceMode::Cooking:
    case PresenceMode::Tanning:
    case PresenceMode::Smelting:
        return 5;
    case PresenceMode::Map:       return 4;
    case PresenceMode::Inventory: return 3;
    case PresenceMode::Waiting:   return 2;
    case PresenceMode::Menu:      return 1;
    default:                      return 0;
    }
}

static std::optional<PresenceMode> ActiveContextMode() {
    std::optional<PresenceMode> selected;
    for (const auto& menu : g_openContextMenus) {
        const auto mode = ContextModeForMenu(menu);
        if (mode && (!selected || ContextPriority(*mode) > ContextPriority(*selected))) {
            selected = mode;
        }
    }
    return selected;
}

void SendPresence(
    std::string_view state,
    std::string_view details,
    PresenceMode mode,
    RE::PlayerCharacter* player = nullptr,
    std::string_view displayLocation = {})
{
    if (!g_core) return;

    const std::string stateText   = LimitDiscordText(state);
    const std::string detailsText = LimitDiscordText(details);
    const std::string smallText   = LimitDiscordText(SmallText(mode));
    const std::string& smallImage = SmallImage(mode);
    const auto largeAsset = ResolveLargeAsset(mode, player, displayLocation);
    const std::string largeText = LimitDiscordText(largeAsset.text);

    std::string signature;
    signature.reserve(
        stateText.size() + detailsText.size() + largeAsset.image.size() +
        largeText.size() + smallImage.size() + smallText.size() + 8);
    for (const auto value :
         {std::string_view(stateText), std::string_view(detailsText),
          largeAsset.image, std::string_view(largeText),
          std::string_view(smallImage), std::string_view(smallText)}) {
        signature.append(value);
        signature.push_back('\0');
    }
    signature.push_back(g_config.showElapsedTime ? '\1' : '\0');

    if (signature == g_lastActivitySignature || signature == g_pendingActivitySignature) {
        SKSE::log::debug("Discord: unchanged activity skipped.");
        return;
    }

    discord::Activity activity{};
    activity.SetType(discord::ActivityType::Playing);
    if (!stateText.empty()) activity.SetState(stateText.c_str());
    if (!detailsText.empty()) activity.SetDetails(detailsText.c_str());
    if (g_config.showElapsedTime) activity.GetTimestamps().SetStart(g_startTime);
    if (!largeAsset.image.empty())
        activity.GetAssets().SetLargeImage(largeAsset.image.data());
    if (!largeText.empty())
        activity.GetAssets().SetLargeText(largeText.c_str());
    if (!smallImage.empty())
        activity.GetAssets().SetSmallImage(smallImage.c_str());
    if (!smallText.empty())
        activity.GetAssets().SetSmallText(smallText.c_str());

    SKSE::log::info(
        "Presence assets: large='{}' small='{}'.",
        largeAsset.image,
        smallImage);
    g_pendingActivitySignature = signature;
    g_core->ActivityManager().UpdateActivity(activity, [signature = std::move(signature)](discord::Result r) {
        if (r != discord::Result::Ok) {
            SKSE::log::error("Discord: UpdateActivity failed (result={})", static_cast<int>(r));
        } else {
            g_lastActivitySignature = signature;
            SKSE::log::info("Presence updated.");
        }
        if (g_pendingActivitySignature == signature)
            g_pendingActivitySignature.clear();
    });
}

void RefreshPosition(
    const char* trigger = nullptr,
    std::optional<PresenceMode> contextOverride = std::nullopt)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    std::string position;
    if (g_config.showLocation) {
        position = BuildPosition(player);
        if (!position.empty()) g_lastPosition = position;
    }
    std::string playerInfo = BuildPlayerInfo(player);

    const bool fallback = g_config.showLocation && position.empty() && !g_lastPosition.empty();
    const std::string& display = fallback ? g_lastPosition : position;
    std::string state = display;

    std::string quest;
    if (g_config.showQuest)
        quest = BuildActiveQuest(player);

    PresenceMode mode = PresenceMode::Exploring;
    std::string suffix;
    const auto contextMode = contextOverride
        ? contextOverride
        : (g_config.showUiState ? ActiveContextMode() : std::nullopt);
    if (g_config.showCombat && !g_combatTarget.empty()) {
        suffix = g_combatTarget;
        mode = PresenceMode::Combat;
    } else if (contextMode) {
        mode = *contextMode;
        suffix = SmallText(mode);
    } else if (!quest.empty()) {
        suffix = std::move(quest);
        mode = PresenceMode::Quest;
    }

    if (!suffix.empty()) {
        if (!state.empty()) state += g_config.separator;
        state += suffix;
    }
    if (state.empty()) state = Locale("exploring");

    SKSE::log::info("[{}] player='{}' location='{}' suffix='{}'{}",
        trigger ? trigger : "refresh", playerInfo, display, suffix,
        fallback ? " [fallback]" : "");
    SendPresence(state, playerInfo, mode, player, display);
}

void DeferredRefresh(int ticks, std::string trigger) {
    SKSE::GetTaskInterface()->AddTask([ticks, trigger = std::move(trigger)]() mutable {
        if (ticks > 1)
            DeferredRefresh(ticks - 1, std::move(trigger));
        else if (g_state == State::Playing)
            RefreshPosition(trigger.c_str());
    });
}
void DeferredCraftingRefresh(int attempts) {
    SKSE::GetTaskInterface()->AddTask([attempts]() {
        if (g_state != State::Playing) return;
        const PresenceMode mode = CurrentCraftingMode();
        if (mode == PresenceMode::Crafting && attempts > 1) {
            DeferredCraftingRefresh(attempts - 1);
            return;
        }
        RefreshPosition("crafting-menu-open", mode);
    });
}


void TransitionTo(State next) {
    State prev = g_state;
    g_state = next;
    switch (g_state) {
    case State::MainMenu:
        SKSE::log::info("State -> MainMenu");
        g_openContextMenus.clear();
        SendPresence(Locale("main_menu"), {}, PresenceMode::MainMenu);
        break;
    case State::EditingCharacter:
        SKSE::log::info("State -> EditingCharacter");
        SendPresence(Locale("editing_character"), {}, PresenceMode::EditingCharacter);
        break;
    case State::Playing:
        SKSE::log::info("State -> Playing");
        if (prev == State::EditingCharacter) {
            // Skyrim commits new name/race to actor base after the menu-close
            // event fires — defer 10 ticks (~167ms at 60fps) to let it finish.
            DeferredRefresh(10, "post-charcreate");
        } else {
            RefreshPosition("state-change");
        }
        break;
    case State::Loading:
        SKSE::log::info("State -> Loading");
        g_combatTarget.clear();
        g_openContextMenus.clear();
        SendPresence(Locale("loading"), {}, PresenceMode::Loading);
        break;
    }
}

class MenuEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* ev,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        if (!ev) return RE::BSEventNotifyControl::kContinue;

        const bool  opening = ev->opening;
        const auto& menu    = ev->menuName;

        if (menu == "Main Menu") {
            SKSE::log::info("Menu: '{}' {}", menu.c_str(), opening ? "open" : "close");
            if (opening) TransitionTo(State::MainMenu);
        } else if (menu == "Loading Menu") {
            SKSE::log::info("Menu: '{}' {}", menu.c_str(), opening ? "open" : "close");
            if (opening) {
                TransitionTo(State::Loading);
            } else if (g_state == State::Loading) {
                TransitionTo(State::Playing);
            }
        } else if (menu == "RaceSex Menu") {
            SKSE::log::info("Menu: '{}' {}", menu.c_str(), opening ? "open" : "close");
            TransitionTo(opening ? State::EditingCharacter : State::Playing);
        } else if (ContextModeForMenu(menu.c_str())) {
            const bool changed = opening
                ? g_openContextMenus.emplace(menu.c_str()).second
                : g_openContextMenus.erase(menu.c_str()) > 0;
            SKSE::log::info("Menu context: '{}' {}", menu.c_str(), opening ? "open" : "close");
            if (changed && g_state == State::Playing) {
                if (opening && menu == RE::CraftingMenu::MENU_NAME)
                    DeferredCraftingRefresh(30);
                else
                    RefreshPosition(opening ? "menu-open" : "menu-close");
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

class LocationChangeSink : public RE::BSTEventSink<RE::TESActorLocationChangeEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESActorLocationChangeEvent* ev,
        RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override
    {
        if (!ev) return RE::BSEventNotifyControl::kContinue;
        if (g_state == State::Playing &&
            ev->actor.get() == RE::PlayerCharacter::GetSingleton())
            RefreshPosition("location-change");
        return RE::BSEventNotifyControl::kContinue;
    }
};

class CellLoadSink : public RE::BSTEventSink<RE::TESCellFullyLoadedEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCellFullyLoadedEvent* ev,
        RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
    {
        if (!ev) return RE::BSEventNotifyControl::kContinue;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (g_state == State::Playing && player && player->GetParentCell() == ev->cell)
            RefreshPosition("cell-loaded");
        return RE::BSEventNotifyControl::kContinue;
    }
};

class QuestStageSink : public RE::BSTEventSink<RE::TESQuestStageEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESQuestStageEvent* ev,
        RE::BSTEventSource<RE::TESQuestStageEvent>*) override
    {
        if (!ev) return RE::BSEventNotifyControl::kContinue;
        if (g_state == State::Playing)
            SKSE::GetTaskInterface()->AddTask([]() { RefreshPosition("quest-stage"); });
        return RE::BSEventNotifyControl::kContinue;
    }
};

class QuestStartStopSink : public RE::BSTEventSink<RE::TESQuestStartStopEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESQuestStartStopEvent* ev,
        RE::BSTEventSource<RE::TESQuestStartStopEvent>*) override
    {
        if (!ev) return RE::BSEventNotifyControl::kContinue;
        if (g_state == State::Playing)
            SKSE::GetTaskInterface()->AddTask([]() { RefreshPosition("quest-startstop"); });
        return RE::BSEventNotifyControl::kContinue;
    }
};

class CombatSink : public RE::BSTEventSink<RE::TESCombatEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCombatEvent* ev,
        RE::BSTEventSource<RE::TESCombatEvent>*) override
    {
        if (!ev) return RE::BSEventNotifyControl::kContinue;
        if (g_state != State::Playing) return RE::BSEventNotifyControl::kContinue;

        // TESCombatEvent fires for the NPC side too (actor=NPC, targetActor=player),
        // so check IsPlayerRef() on both sides rather than comparing pointers.
        bool actorIsPlayer  = ev->actor       && ev->actor->IsPlayerRef();
        bool targetIsPlayer = ev->targetActor && ev->targetActor->IsPlayerRef();
        bool involvesPlayer = actorIsPlayer || targetIsPlayer;
        // kNone events not involving the player may signal that the player also exited
        // combat — only trigger if we're currently showing combat to avoid spam.
        bool mayEndCombat = ev->newState.get() == RE::ACTOR_COMBAT_STATE::kNone
                            && !g_combatTarget.empty();
        SKSE::log::info("TESCombatEvent: state={} actorIsPlayer={} targetIsPlayer={}",
            static_cast<int>(ev->newState.get()), actorIsPlayer, targetIsPlayer);
        if (!involvesPlayer && !mayEndCombat)
            return RE::BSEventNotifyControl::kContinue;

        // Read game-object state on the game thread via AddTask.
        SKSE::GetTaskInterface()->AddTask([]() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || g_state != State::Playing) return;

            if (player->IsInCombat()) {
                if (auto target = player->GetActorRuntimeData().currentCombatTarget.get()) {
                    std::string name = SafeStr(target->GetName());
                    g_combatTarget = name.empty() ? Locale("combat_no_target")
                                                  : FormatCombat(Locale("combat_fighting"), name);
                } else if (g_combatTarget.empty()) {
                    g_combatTarget = Locale("combat_no_target");
                }
                // currentCombatTarget temporarily null — keep existing name
            } else {
                g_combatTarget.clear();
            }

            SKSE::log::info("Combat: '{}'", g_combatTarget);
            RefreshPosition("combat");
        });
        return RE::BSEventNotifyControl::kContinue;
    }
};

MenuEventSink      g_menuSink;
LocationChangeSink g_locationSink;
CellLoadSink       g_cellSink;
QuestStageSink     g_questStageSink;
QuestStartStopSink g_questStartStopSink;
CombatSink         g_combatSink;

std::atomic<bool> g_callbackThreadRunning{false};

static void StartCallbackThread() {
    if (g_callbackThreadRunning.exchange(true)) return;
    std::thread([]() {
        while (g_callbackThreadRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            SKSE::GetTaskInterface()->AddTask([]() {
                if (g_core) g_core->RunCallbacks();
            });
        }
    }).detach();
}

void InitDiscord() {
    if (!g_config.enabled) {
        SKSE::log::info("Discord presence disabled by configuration.");
        return;
    }

    g_startTime = static_cast<int64_t>(std::time(nullptr));
    discord::Result result;
    try {
        result = discord::Core::Create(g_config.applicationId, DiscordCreateFlags_Default, &g_core);
    } catch (...) {
        // discord_game_sdk.dll not found — delay-load threw; run without presence.
        SKSE::log::warn("Discord: discord_game_sdk.dll not found — presence disabled.");
        g_core = nullptr;
        return;
    }
    if (result != discord::Result::Ok) {
        SKSE::log::error("Discord: failed to initialize (result={})", static_cast<int>(result));
        g_core = nullptr;
        return;
    }
    g_core->SetLogHook(discord::LogLevel::Warn, [](discord::LogLevel, const char* msg) {
        SKSE::log::warn("Discord: {}", msg);
    });
    SKSE::log::info("Discord Game SDK initialized for application {}.", g_config.applicationId);
    StartCallbackThread();
}

} // anonymous namespace

void LoadConfig() {
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

        if (auto schema = root.find("schema_version"); schema != root.end() &&
            (!schema->is_number_integer() || schema->get<int>() != 1)) {
            SKSE::log::warn("Config: unsupported 'schema_version'; reading known fields only.");
        }

        if (const auto* discord = FindObject(root, "discord", "discord")) {
            ReadBool(*discord, "enabled", g_config.enabled, "discord");
            ReadApplicationId(*discord);
        }

        if (const auto* display = FindObject(root, "display", "display")) {
            ReadBool(*display, "show_elapsed_time", g_config.showElapsedTime, "display");
            ReadBool(*display, "show_character_details", g_config.showCharacterDetails, "display");
            ReadBool(*display, "show_location", g_config.showLocation, "display");
            ReadBool(*display, "show_quest", g_config.showQuest, "display");
            ReadBool(*display, "show_combat", g_config.showCombat, "display");
            ReadBool(*display, "show_ui_state", g_config.showUiState, "display");
            ReadString(*display, "separator", g_config.separator, "display");
        }

        if (const auto* assets = FindObject(root, "assets", "assets")) {
            ReadString(*assets, "large_image", g_config.largeImage, "assets");
            ReadString(*assets, "large_text", g_config.largeText, "assets");
            if (const auto* largeImages = FindObject(*assets, "large_images", "assets.large_images")) {
                ReadString(*largeImages, "loading", g_config.loadingLargeImage, "assets.large_images");
                ReadString(*largeImages, "main_menu", g_config.mainMenuLargeImage, "assets.large_images");
                ReadString(*largeImages, "editing_character", g_config.editingCharacterLargeImage, "assets.large_images");
                ReadString(*largeImages, "playing", g_config.playingLargeImage, "assets.large_images");
            }
            if (const auto* smallImages = FindObject(*assets, "small_images", "assets.small_images")) {
                ReadString(*smallImages, "loading", g_config.loadingImage, "assets.small_images");
                ReadString(*smallImages, "main_menu", g_config.mainMenuImage, "assets.small_images");
                ReadString(*smallImages, "editing_character", g_config.editingCharacterImage, "assets.small_images");
                ReadString(*smallImages, "exploring", g_config.exploringImage, "assets.small_images");
                ReadString(*smallImages, "quest", g_config.questImage, "assets.small_images");
                ReadString(*smallImages, "combat", g_config.combatImage, "assets.small_images");
                ReadString(*smallImages, "menu", g_config.menuImage, "assets.small_images");
                ReadString(*smallImages, "map", g_config.mapImage, "assets.small_images");
                ReadString(*smallImages, "inventory", g_config.inventoryImage, "assets.small_images");
                ReadString(*smallImages, "dialogue", g_config.dialogueImage, "assets.small_images");
                ReadString(*smallImages, "crafting", g_config.craftingImage, "assets.small_images");
                ReadString(*smallImages, "alchemy", g_config.alchemyImage, "assets.small_images");
                ReadString(*smallImages, "smithing", g_config.smithingImage, "assets.small_images");
                ReadString(*smallImages, "enchanting", g_config.enchantingImage, "assets.small_images");
                ReadString(*smallImages, "cooking", g_config.cookingImage, "assets.small_images");
                ReadString(*smallImages, "tanning", g_config.tanningImage, "assets.small_images");
                ReadString(*smallImages, "smelting", g_config.smeltingImage, "assets.small_images");
                ReadString(*smallImages, "waiting", g_config.waitingImage, "assets.small_images");
            }
            ReadLocationImageRules(*assets);
        }

        SKSE::log::info("Config loaded.");
    } catch (const nlohmann::json::exception& e) {
        SKSE::log::error("Failed to parse config JSON: {}", e.what());
        g_config = Config{};
    }
}

void SetLocale() {
    std::ifstream file(R"(Data\SKSE\Plugins\DragonbornPresenceLocale.json)");
    if (!file) return;

    try {
        auto j = nlohmann::json::parse(file);
        for (auto& [key, val] : j.items())
            if (val.is_string()) g_locale[key] = val.get<std::string>();
    } catch (const nlohmann::json::exception& e) {
        SKSE::log::error("Failed to parse locale JSON: {}", e.what());
    }
}

void RegisterGameEventHandlers() {
    SKSE::log::info("Registering game event handlers...");
    InitDiscord();
    // Plugin loads before Main Menu appears, but the open-event may still
    // race. Start in MainMenu state so Discord shows something right away.
    TransitionTo(State::MainMenu);

    auto* ui = RE::UI::GetSingleton();
    if (ui) {
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_menuSink);
    } else {
        SKSE::log::error("Failed to get RE::UI singleton — menu events won't be tracked.");
    }

    auto* src = RE::ScriptEventSourceHolder::GetSingleton();
    if (src) {
        src->AddEventSink<RE::TESActorLocationChangeEvent>(&g_locationSink);
        src->AddEventSink<RE::TESCellFullyLoadedEvent>(&g_cellSink);
        src->AddEventSink<RE::TESQuestStageEvent>(&g_questStageSink);
        src->AddEventSink<RE::TESQuestStartStopEvent>(&g_questStartStopSink);
        src->AddEventSink<RE::TESCombatEvent>(&g_combatSink);
    } else {
        SKSE::log::error("Failed to get RE::ScriptEventSourceHolder singleton.");
    }
}

void OnGameLoaded() {
    // kPostLoadGame / kNewGame fires on the main thread after the engine
    // has fully committed all save data — call RefreshPosition directly.
    SKSE::log::info("Game loaded — forcing presence refresh");
    g_state = State::Playing;
    RefreshPosition("game-loaded");
}

} // namespace DragonbornPresence
