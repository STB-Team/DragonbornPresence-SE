#pragma once
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace DragonbornPresence {

void LoadConfig() noexcept;
void RegisterGameEventHandlers() noexcept;
void OnGameLoaded() noexcept;

}
