#include "DragonbornPresence.h"
#include "AdditionalFunctions.h"
#include "discord.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>

namespace DragonbornPresence {

namespace {

constexpr discord::ClientId kAppId          = 565627104608256015LL;
constexpr const char*       kLargeImageKey  = "skyrim_logo";
constexpr const char*       kLargeImageText = "The Elder Scrolls V: Skyrim";

enum class State { Loading, MainMenu, EditingCharacter, Playing };

State          g_state             = State::Loading;
discord::Core* g_core              = nullptr;
int64_t        g_startTime         = 0;
std::string    g_localeMainMenu    = "Main menu";
std::string    g_localeEditingChar = "Editing character";
std::string    g_lastPosition;

static std::string SafeStr(const char* s) {
    if (!s || *s == '\0') return "";
    return IsValidUtf8(s) ? std::string(s) : Cp1251ToUtf8(s);
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
    auto* race = player->GetRace();
    // GetName() on the actor returns the display name (updated after char edit);
    // GetActorBase()->GetName() can lag behind by several frames.
    std::string name     = SafeStr(player->GetName());
    std::string raceName = race ? SafeStr(race->GetName()) : "";
    return name + " - " + raceName + " (" + std::to_string(player->GetLevel()) + ")";
}

void SendPresence(const char* state, const char* details) {
    if (!g_core) return;

    discord::Activity activity{};
    if (state   && *state)   activity.SetState(state);
    if (details && *details) activity.SetDetails(details);
    activity.GetTimestamps().SetStart(g_startTime);
    activity.GetAssets().SetLargeImage(kLargeImageKey);
    activity.GetAssets().SetLargeText(kLargeImageText);

    g_core->ActivityManager().UpdateActivity(activity, [](discord::Result r) {
        if (r != discord::Result::Ok)
            SKSE::log::error("Discord: UpdateActivity failed (result={})", static_cast<int>(r));
        else
            SKSE::log::info("Presence updated.");
    });
}

void RefreshPosition(const char* trigger = nullptr) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    std::string position   = BuildPosition(player);
    std::string playerInfo = BuildPlayerInfo(player);
    std::string quest      = BuildActiveQuest(player);

    const bool fallback = position.empty() && !g_lastPosition.empty();
    if (!position.empty()) g_lastPosition = position;

    const std::string& display = fallback ? g_lastPosition : position;
    std::string state = display;
    if (!quest.empty()) state += " \xC2\xB7 " + quest;  // · (U+00B7)

    SKSE::log::info("[{}] player='{}' location='{}' quest='{}'{}",
        trigger ? trigger : "refresh", playerInfo, display, quest,
        fallback ? " [fallback]" : "");
    SendPresence(state.c_str(), playerInfo.c_str());
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
        SendPresence(g_localeMainMenu.c_str(), nullptr);
        break;
    case State::EditingCharacter:
        SKSE::log::info("State -> EditingCharacter");
        SendPresence(g_localeEditingChar.c_str(), nullptr);
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

MenuEventSink      g_menuSink;
LocationChangeSink g_locationSink;
CellLoadSink       g_cellSink;
QuestStageSink     g_questStageSink;
QuestStartStopSink g_questStartStopSink;

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
    g_startTime = static_cast<int64_t>(std::time(nullptr));
    auto result = discord::Core::Create(kAppId, DiscordCreateFlags_Default, &g_core);
    if (result != discord::Result::Ok) {
        SKSE::log::error("Discord: failed to initialize (result={})", static_cast<int>(result));
        g_core = nullptr;
        return;
    }
    g_core->SetLogHook(discord::LogLevel::Warn, [](discord::LogLevel, const char* msg) {
        SKSE::log::warn("Discord: {}", msg);
    });
    SKSE::log::info("Discord Game SDK initialized.");
    StartCallbackThread();
}

} // anonymous namespace

void SetLocale() {
    std::ifstream file(R"(Data\SKSE\Plugins\DragonbornPresenceLocale.json)");
    if (!file) return;

    try {
        auto j = nlohmann::json::parse(file);
        if (j.contains("main_menu"))         g_localeMainMenu    = j["main_menu"].get<std::string>();
        if (j.contains("editing_character")) g_localeEditingChar = j["editing_character"].get<std::string>();
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
