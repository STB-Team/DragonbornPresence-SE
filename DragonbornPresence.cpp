#include "DragonbornPresence.h"
#include "AdditionalFunctions.h"
#include "discord_rpc.h"
#include <ctime>
#include <fstream>
#include <string>

namespace DragonbornPresence {

namespace {

constexpr const char* kAppId          = "565627104608256015";
constexpr const char* kSteamAppId     = "72850";
constexpr const char* kLargeImageKey  = "skyrim_logo";
constexpr const char* kLargeImageText = "The Elder Scrolls V: Skyrim";

enum class State { Loading, MainMenu, EditingCharacter, Playing };

State       g_state               = State::Loading;
bool        g_discordConnected    = false;
int64_t     g_startTime           = 0;
std::string g_position;
std::string g_playerInfo;
std::string g_localeMainMenu      = "Main menu";
std::string g_localeEditingChar   = "Editing character";

void SendPresence(const char* state, const char* details) {
    if (!g_discordConnected) return;

    DiscordRichPresence presence{};
    presence.state          = state;
    presence.details        = details;
    presence.startTimestamp = g_startTime;
    presence.largeImageKey  = kLargeImageKey;
    presence.largeImageText = kLargeImageText;
    Discord_UpdatePresence(&presence);
    Discord_RunCallbacks();
    SKSE::log::info("Presence updated.");
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
        SendPresence(g_position.c_str(), g_playerInfo.c_str());
        break;
    case State::Loading:
        SKSE::log::info("State -> Loading");
        break;
    }
}

void OnDiscordReady(const DiscordUser* user) {
    SKSE::log::info("Discord: connected as {}#{} ({})", user->username, user->discriminator, user->userId);
    g_discordConnected = true;
}

void OnDiscordError(int code, const char* message) {
    SKSE::log::error("Discord error {}: {}", code, message);
}

void OnDiscordDisconnected(int code, const char* message) {
    SKSE::log::warn("Discord disconnected {}: {}", code, message);
    g_discordConnected = false;
}

void InitDiscord() {
    g_startTime = static_cast<int64_t>(std::time(nullptr));
    DiscordEventHandlers handlers{};
    handlers.ready        = OnDiscordReady;
    handlers.errored      = OnDiscordError;
    handlers.disconnected = OnDiscordDisconnected;
    Discord_Initialize(kAppId, &handlers, 1, kSteamAppId);
    SKSE::log::info("Discord RPC initialized.");
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

MenuEventSink g_menuSink;

void UpdatePresenceData(RE::StaticFunctionTag*, RE::BSFixedString position, RE::BSFixedString playerInfo) {
    const char* pos  = position.data();
    const char* info = playerInfo.data();

    g_position   = IsValidUtf8(pos)  ? std::string(pos)  : Cp1251ToUtf8(pos);
    g_playerInfo = IsValidUtf8(info) ? std::string(info) : Cp1251ToUtf8(info);

    SKSE::log::info("Position: {}", g_position);

    if (g_state == State::Playing)
        SendPresence(g_position.c_str(), g_playerInfo.c_str());
}

void SetGameLoaded(RE::StaticFunctionTag*) {
    TransitionTo(State::Playing);
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
}

bool RegisterFuncs(RE::BSScript::IVirtualMachine* vm) {
    vm->RegisterFunction("UpdatePresenceData", "DragonbornPresence", UpdatePresenceData);
    vm->RegisterFunction("SetGameLoaded",      "DragonbornPresence", SetGameLoaded);
    return true;
}

} // namespace DragonbornPresence
