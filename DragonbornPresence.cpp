#include "DragonbornPresence.h"
#include "AdditionalFunctions.h"
#include "discord.h"
#include <ctime>
#include <fstream>
#include <string>

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

static std::string SafeStr(const char* s) {
    if (!s || *s == '\0') return "";
    return IsValidUtf8(s) ? std::string(s) : Cp1251ToUtf8(s);
}

static std::string BuildPosition(RE::PlayerCharacter* player) {
    auto* ws   = player->GetWorldspace();
    auto* loc  = player->GetCurrentLocation();
    auto* cell = player->GetParentCell();

    std::string wsName   = ws   ? SafeStr(ws->GetName())   : "";
    std::string locName  = loc  ? SafeStr(loc->GetName())  : "";
    std::string cellName = cell ? SafeStr(cell->GetName()) : "";

    if (!wsName.empty()) {
        return (!locName.empty() && locName != wsName)
            ? wsName + ": " + locName
            : wsName;
    }
    if (!locName.empty()) {
        return (!cellName.empty() && cellName != locName)
            ? locName + ": " + cellName
            : locName;
    }
    return cellName;
}

static std::string BuildPlayerInfo(RE::PlayerCharacter* player) {
    auto* base = player->GetActorBase();
    auto* race = player->GetRace();
    std::string name     = base ? SafeStr(base->GetName()) : "";
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
    g_core->RunCallbacks();
}

void RefreshPosition() {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    std::string position   = BuildPosition(player);
    std::string playerInfo = BuildPlayerInfo(player);
    SKSE::log::info("Position: {}", position);
    SendPresence(position.c_str(), playerInfo.c_str());
}

void TransitionTo(State next) {
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
        RefreshPosition();
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
            if (opening) TransitionTo(State::MainMenu);
        } else if (menu == "Loading Menu") {
            TransitionTo(opening ? State::Loading : State::Playing);
        } else if (menu == "RaceSex Menu") {
            TransitionTo(opening ? State::EditingCharacter : State::Playing);
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
            RefreshPosition();
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
            RefreshPosition();
        return RE::BSEventNotifyControl::kContinue;
    }
};

MenuEventSink      g_menuSink;
LocationChangeSink g_locationSink;
CellLoadSink       g_cellSink;

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
}

} // anonymous namespace

void SetLocale() {
    std::ifstream file(R"(Data\SKSE\Plugins\DragonbornPresenceLocale.txt)");
    if (!file) return;

    int index = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == ';') continue;
        if (index == 0)      g_localeMainMenu    = line;
        else if (index == 1) g_localeEditingChar = line;
        ++index;
    }
}

void RegisterGameEventHandlers() {
    SKSE::log::info("Registering game event handlers...");
    InitDiscord();

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
    } else {
        SKSE::log::error("Failed to get RE::ScriptEventSourceHolder singleton.");
    }
}

} // namespace DragonbornPresence
