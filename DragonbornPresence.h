#pragma once
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include "discord_rpc.h"


namespace dragonborn_presence_namespace {

    void SetLocale();

    void HandleDiscordReady(const DiscordUser* connected_user);
    void HandleDiscordError(int error_code, const char* message);
    void HandleDiscordDisconnected(int error_code, const char* message);

    void InitDiscord();
    void UpdatePresence(const char* current_state, const char* current_details);

    class DiscordMenuEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                             RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;
    };

    extern DiscordMenuEventHandler g_discordMenuEventHandler;
    void RegisterGameEventHandlers();

    void UpdatePresenceData(RE::StaticFunctionTag*, RE::BSFixedString new_position,
                            RE::BSFixedString new_player_info);
    void SetGameLoaded(RE::StaticFunctionTag*);
    bool RegisterFuncs(RE::BSScript::IVirtualMachine* vm);
}
