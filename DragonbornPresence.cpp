#include "tinyfsm.hpp"
#include "DragonbornPresence.h"
#include "AdditionalFunctions.h"
#include <ctime>


bool is_user_connected = true;

int64_t start_time;

const char* application_id = "565627104608256015";
const char* steam_appid = "72850";

const char* current_player_info;
const char* current_position;

std::map<const char*, std::string> locales
{
    {{"MainMenu", "Main menu"}, {"EditingCharacter", "Editing character"}}
};


#pragma region State Machine

#pragma region Event Declaration
struct StatusEvent : tinyfsm::Event {};

struct GoToMainMenu : StatusEvent {};
struct GoToLoading : StatusEvent {};
struct GoToEditingCharacter : StatusEvent {};
struct GoToPlaying : StatusEvent {};
#pragma endregion

#pragma region State Machine Class Declaration
struct DiscordState : tinyfsm::Fsm<DiscordState> {
    static void react(tinyfsm::Event const&) {};

    virtual void react(GoToMainMenu const&) {};
    virtual void react(GoToLoading const&) {};
    virtual void react(GoToEditingCharacter const&) {};
    virtual void react(GoToPlaying const&) {};

    virtual void entry() {};
    virtual void exit() {};
};
#pragma endregion

#pragma region States Declaration
struct MainMenuState : DiscordState {
    void entry() override;
    void react(GoToLoading const&) override;
};

struct LoadingState : DiscordState {
    void entry() override;
    void react(GoToMainMenu const&) override;
    void react(GoToEditingCharacter const&) override;
    void react(GoToPlaying const&) override;
};

struct EditingCharacterState : DiscordState {
    void entry() override;
    void react(GoToPlaying const&) override;
};

struct PlayingState : DiscordState {
    void entry() override;
    void react(GoToEditingCharacter const&) override;
    void react(GoToLoading const&) override;
    void react(GoToMainMenu const&) override;
};

FSM_INITIAL_STATE(DiscordState, LoadingState)
#pragma endregion

#pragma region Actions and Event Reactions Implementation
void MainMenuState::entry() {
    SKSE::log::info("Main Menu State");
    dragonborn_presence_namespace::UpdatePresence(locales.find("MainMenu")->second.c_str(), nullptr);
}

void MainMenuState::react(GoToLoading const&) { transit<LoadingState>(); }

void LoadingState::entry() { SKSE::log::info("Loading State"); }

void LoadingState::react(GoToMainMenu const&) { transit<MainMenuState>(); }
void LoadingState::react(GoToEditingCharacter const&) { transit<EditingCharacterState>(); }
void LoadingState::react(GoToPlaying const&) { transit<PlayingState>(); }

void EditingCharacterState::entry() {
    SKSE::log::info("Editing Character State");
    dragonborn_presence_namespace::UpdatePresence(locales.find("EditingCharacter")->second.c_str(), nullptr);
}

void EditingCharacterState::react(GoToPlaying const&) { transit<PlayingState>(); }

void PlayingState::entry() {
    SKSE::log::info("Playing State");
    dragonborn_presence_namespace::UpdatePresence(current_position, current_player_info);
}

void PlayingState::react(GoToEditingCharacter const&) { transit<EditingCharacterState>(); }
void PlayingState::react(GoToLoading const&) { transit<LoadingState>(); }
void PlayingState::react(GoToMainMenu const&) { transit<MainMenuState>(); }
#pragma endregion

#pragma endregion


namespace dragonborn_presence_namespace {

    void SetLocale() {
        FILE* file = nullptr;
        const auto err = fopen_s(&file, R"(Data\SKSE\Plugins\DragonbornPresenceLocale.txt)", "r");

        if (err == 2 || file == nullptr)
            return;

        if (err != 0)
            return;

        char buffer[1024];
        auto index = 0;
        while (fgets(buffer, 1024, file) != nullptr) {
            std::string line = buffer;
            while (!line.empty() && (line[line.length() - 1] == '\r' || line[line.length() - 1] == '\n'))
                line = line.substr(0, line.length() - 1);

            if (line.empty()) continue;
            if (line[0] == ';') continue;

            if (index == 0)
                locales.find("MainMenu")->second = line;
            else if (index == 1)
                locales.find("EditingCharacter")->second = line;

            index++;
        }

        fclose(file);
    }

#pragma region Discord Callbacks

    void HandleDiscordReady(const DiscordUser* connected_user) {
        SKSE::log::info("Discord RPC: connected to user {}#{} - {}",
                        connected_user->username, connected_user->discriminator,
                        connected_user->userId);
        is_user_connected = true;
    }

    void HandleDiscordError(int error_code, const char* message) {
        SKSE::log::info("Discord RPC: an error occurred ({}: {})", error_code, message);
    }

    void HandleDiscordDisconnected(int error_code, const char* message) {
        SKSE::log::info("Discord RPC: disconnected ({}: {})", error_code, message);
        is_user_connected = false;
    }

#pragma endregion

#pragma region Discord Functions

    void InitDiscord() {
        start_time = time(nullptr);
        DiscordEventHandlers handlers;
        memset(&handlers, 0, sizeof(handlers));
        handlers.ready = HandleDiscordReady;
        handlers.errored = HandleDiscordError;
        handlers.disconnected = HandleDiscordDisconnected;

        Discord_Initialize(application_id, &handlers, 1, steam_appid);
        SKSE::log::info("Discord RPC Init OK.");
    }

    void UpdatePresence(const char* current_state, const char* current_details) {
        if (is_user_connected) {
            DiscordRichPresence discord_presence;
            memset(&discord_presence, 0, sizeof(discord_presence));
            discord_presence.state = current_state;
            discord_presence.details = current_details;
            discord_presence.startTimestamp = start_time;
            discord_presence.largeImageKey = "skyrim_logo";
            discord_presence.largeImageText = "The Elder Scrolls V: Skyrim";
            Discord_UpdatePresence(&discord_presence);

            Discord_RunCallbacks();

            SKSE::log::info("Updated presence.");
        }
    }

#pragma endregion

#pragma region ReceiveEvent

    RE::BSEventNotifyControl DiscordMenuEventHandler::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
    {
        if (a_event->opening && a_event->menuName == RE::BSFixedString("Main Menu")) {
            DiscordState::dispatch(GoToMainMenu());
        } else if (a_event->menuName == RE::BSFixedString("Loading Menu")) {
            if (a_event->opening)
                DiscordState::dispatch(GoToLoading());
            else
                DiscordState::dispatch(GoToPlaying());
        } else if (a_event->menuName == RE::BSFixedString("RaceSex Menu")) {
            if (a_event->opening)
                DiscordState::dispatch(GoToEditingCharacter());
            else
                DiscordState::dispatch(GoToPlaying());
        }

        return RE::BSEventNotifyControl::kContinue;
    }

#pragma endregion

#pragma region Event Handlers
    DiscordMenuEventHandler g_discordMenuEventHandler;
#pragma endregion

#pragma region Event registration
    void RegisterGameEventHandlers() {
        SKSE::log::info("Registering game event handlers...");

        DiscordState::start();

        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_discordMenuEventHandler);
        } else {
            SKSE::log::error("Failed to register SKSE menuEventHandler!");
        }
    }
#pragma endregion

#pragma region Papyrus functions
    void UpdatePresenceData(RE::StaticFunctionTag*, RE::BSFixedString new_position,
                            RE::BSFixedString new_player_info)
    {
        SKSE::log::info("CURRENT POSITION: {}", new_position.data());
        current_position = is_valid_utf8(new_position.data())
                               ? new_position.data()
                               : Cp1251ToUtf8(new_position.data());
        current_player_info = is_valid_utf8(new_player_info.data())
                                  ? new_player_info.data()
                                  : Cp1251ToUtf8(new_player_info.data());
        if (DiscordState::is_in_state<PlayingState>()) {
            UpdatePresence(current_position, current_player_info);
        }
    }

    void SetGameLoaded(RE::StaticFunctionTag*) {
        DiscordState::dispatch(GoToPlaying());
        UpdatePresence(current_position, current_player_info);
    }

    bool RegisterFuncs(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("UpdatePresenceData", "DragonbornPresence", UpdatePresenceData);
        vm->RegisterFunction("SetGameLoaded", "DragonbornPresence", SetGameLoaded);

        InitDiscord();

        return true;
    }
#pragma endregion
}
