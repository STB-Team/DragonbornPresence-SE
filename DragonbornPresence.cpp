#include "DragonbornPresence.h"
#include "AdditionalFunctions.h"
#include "discord.h"
#include <atomic>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <nlohmann/json.hpp>

namespace DragonbornPresence {

namespace {

constexpr discord::ClientId kDefaultAppId = 565627104608256015LL;

enum class State { Loading, MainMenu, EditingCharacter, Playing };
enum class PresenceMode { Loading, MainMenu, EditingCharacter, Exploring, Quest, Combat };

struct Config {
    bool              enabled              = true;
    discord::ClientId applicationId        = kDefaultAppId;
    bool              showElapsedTime      = true;
    bool              showCharacterDetails = true;
    bool              showLocation         = true;
    bool              showQuest            = true;
    bool              showCombat           = true;
    std::string       separator            = " \xC2\xB7 ";
    std::string       largeImage           = "skyrim_logo";
    std::string       largeText            = "The Elder Scrolls V: Skyrim";
    std::string       loadingImage;
    std::string       mainMenuImage;
    std::string       editingCharacterImage;
    std::string       exploringImage;
    std::string       questImage;
    std::string       combatImage;
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
};
std::string g_lastPosition;
std::string g_combatTarget;
std::string g_lastActivitySignature;
std::string g_pendingActivitySignature;

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

static std::string LimitDiscordText(std::string_view text) {
    constexpr std::size_t kMaxBytes = 127;
    if (text.size() <= kMaxBytes) return std::string(text);

    std::size_t size = kMaxBytes;
    while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80)
        --size;
    return std::string(text.substr(0, size));
}

static const std::string& SmallImage(PresenceMode mode) {
    switch (mode) {
    case PresenceMode::Loading:          return g_config.loadingImage;
    case PresenceMode::MainMenu:         return g_config.mainMenuImage;
    case PresenceMode::EditingCharacter: return g_config.editingCharacterImage;
    case PresenceMode::Exploring:        return g_config.exploringImage;
    case PresenceMode::Quest:            return g_config.questImage;
    case PresenceMode::Combat:           return g_config.combatImage;
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

void SendPresence(std::string_view state, std::string_view details, PresenceMode mode) {
    if (!g_core) return;

    const std::string stateText   = LimitDiscordText(state);
    const std::string detailsText = LimitDiscordText(details);
    const std::string smallText   = LimitDiscordText(SmallText(mode));
    const std::string& smallImage = SmallImage(mode);

    std::string signature;
    signature.reserve(
        stateText.size() + detailsText.size() + g_config.largeImage.size() +
        g_config.largeText.size() + smallImage.size() + smallText.size() + 8);
    for (const auto& value :
         {std::string_view(stateText), std::string_view(detailsText),
          std::string_view(g_config.largeImage), std::string_view(g_config.largeText),
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
    if (!g_config.largeImage.empty())
        activity.GetAssets().SetLargeImage(g_config.largeImage.c_str());
    if (!g_config.largeText.empty())
        activity.GetAssets().SetLargeText(LimitDiscordText(g_config.largeText).c_str());
    if (!smallImage.empty())
        activity.GetAssets().SetSmallImage(smallImage.c_str());
    if (!smallText.empty())
        activity.GetAssets().SetSmallText(smallText.c_str());

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

void RefreshPosition(const char* trigger = nullptr) {
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
    if (g_config.showCombat && !g_combatTarget.empty()) {
        suffix = g_combatTarget;
        mode = PresenceMode::Combat;
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
    SendPresence(state, playerInfo, mode);
}

void DeferredRefresh(int ticks) {
    SKSE::GetTaskInterface()->AddTask([ticks]() {
        if (ticks > 1)
            DeferredRefresh(ticks - 1);
        else if (g_state == State::Playing)
            RefreshPosition("post-charcreate");
    });
}

void TransitionTo(State next) {
    State prev = g_state;
    g_state = next;
    switch (g_state) {
    case State::MainMenu:
        SKSE::log::info("State -> MainMenu");
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
            DeferredRefresh(10);
        } else {
            RefreshPosition("state-change");
        }
        break;
    case State::Loading:
        SKSE::log::info("State -> Loading");
        g_combatTarget.clear();
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
        } else if (menu == "Journal Menu") {
            if (!opening && g_state == State::Playing)
                RefreshPosition("journal-close");
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
            ReadString(*display, "separator", g_config.separator, "display");
        }

        if (const auto* assets = FindObject(root, "assets", "assets")) {
            ReadString(*assets, "large_image", g_config.largeImage, "assets");
            ReadString(*assets, "large_text", g_config.largeText, "assets");
            if (const auto* smallImages = FindObject(*assets, "small_images", "assets.small_images")) {
                ReadString(*smallImages, "loading", g_config.loadingImage, "assets.small_images");
                ReadString(*smallImages, "main_menu", g_config.mainMenuImage, "assets.small_images");
                ReadString(*smallImages, "editing_character", g_config.editingCharacterImage, "assets.small_images");
                ReadString(*smallImages, "exploring", g_config.exploringImage, "assets.small_images");
                ReadString(*smallImages, "quest", g_config.questImage, "assets.small_images");
                ReadString(*smallImages, "combat", g_config.combatImage, "assets.small_images");
            }
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
