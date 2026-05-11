#pragma once
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace DragonbornPresence {

void SetLocale();
void RegisterGameEventHandlers();
bool RegisterFuncs(RE::BSScript::IVirtualMachine* vm);

}
