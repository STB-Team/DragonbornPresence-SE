# DragonbornPresence SE

An SKSE plugin for **Skyrim Special Edition** that displays your current in-game state as Discord Rich Presence.

While you play, Discord shows your character's name, race, level, current location, and active quest — and updates automatically as you move through the world.

---

## Features

- Current location displayed in Discord (worldspace + named location)
- Character info: name, race, and level
- Active quest name shown alongside the location
- Session timer showing how long you've been playing
- State-aware presence: Main Menu, Character Creation, Loading, and In-Game are all handled separately
- Localization support — English labels can be replaced via a JSON file
- Graceful degradation if Discord is not running

---

## Requirements

- Skyrim Special Edition or Anniversary Edition (any runtime — uses Address Library)
- [SKSE64](https://skse.silverlock.org/) matching your runtime

---

## Installation

1. Install SKSE64 for your Skyrim SE/AE runtime.
2. Extract the archive. It contains:
   ```
   discord_game_sdk.dll              → Skyrim Special Edition\
   Data\SKSE\Plugins\
       DragonbornPresence.dll
       DragonbornPresenceLocale.json
   ```
3. Copy `discord_game_sdk.dll` to the **game root** (same folder as `SkyrimSE.exe`).
4. Copy the `Data\` folder into your Skyrim `Data\` directory.
5. Launch the game through SKSE.

Discord must be running before or alongside the game. If it is not running the plugin loads normally — presence is simply disabled.

---

## Localization

The two UI strings shown in Discord (main menu and character creation labels) can be changed by editing:

```
Data\SKSE\Plugins\DragonbornPresenceLocale.json
```

The file format:
```json
{
    "main_menu": "Main menu",
    "editing_character": "Editing character"
}
```

If the file is missing or a key is absent, English defaults are used.

---

## Building from Source

### Requirements

- CMake 3.28 or later
- Visual Studio 2022 with the **Desktop development with C++** workload
- Internet access for the first CMake configure

### Steps

```bash
git clone https://github.com/your-repo/DragonbornPresence-SE.git
cd DragonbornPresence-SE
```

Open `CMakeLists.txt` in CLion, or configure from the command line:

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

On the first configure CMake downloads [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) v3.7.0, [Discord Game SDK 3.2.1](https://discord.com/developers/docs/game-sdk/sdk-starter-guide), and several header-only libraries — this takes a minute or two. Subsequent builds use the cache and are fast.

The post-build step produces `DragonbornPresence.zip` in the build directory with the full install layout ready to drop into the game.

---

## Architecture

Pure C++ DLL — no `.esp`, no Papyrus scripts.

**`main.cpp`** — Plugin entry point (`SKSEPluginLoad`). Sets up spdlog logging, reads the locale file, registers the SKSE messaging listener. Reacts to `kDataLoaded` (registers event sinks) and `kNewGame`/`kPostLoadGame` (forces presence refresh).

**`DragonbornPresence.cpp`** — All state and Discord logic.

- **State machine** (`enum class State`): `Loading → MainMenu → Playing / EditingCharacter`. Transitions via `TransitionTo()`.
- **`MenuEventSink`** — listens to `MenuOpenCloseEvent`. Maps `"Main Menu"`, `"Loading Menu"`, `"RaceSex Menu"`, and `"Journal Menu"` to state transitions or presence refreshes.
- **`LocationChangeSink`** — listens to `TESActorLocationChangeEvent`; calls `RefreshPosition()` when the player changes named location.
- **`CellLoadSink`** — listens to `TESCellFullyLoadedEvent`; calls `RefreshPosition()` when the player's cell finishes loading.
- **`QuestStageSink`** / **`QuestStartStopSink`** — listen to quest stage and start/stop events; refresh presence via an SKSE task so the update runs on the game thread.
- **`BuildPosition()`** — traverses the `parentLoc` chain to find the first named ancestor. Returns `"Worldspace: Location"` for exterior, `"Location"` for interior, cell name as last resort. Caches the last non-empty result to handle null pointers during location boundary crossing.
- **`BuildActiveQuest()`** — scans displayed quest objectives and returns the name of the highest-priority active quest. Appended to the location string with a `·` separator.
- **`BuildPlayerInfo()`** — returns `"Name - Race (Level)"`.
- **`SendPresence()`** — sends a `discord::Activity` to the Discord Game SDK.
- **`StartCallbackThread()`** — background thread that posts one `SKSE::GetTaskInterface()->AddTask` per 100 ms to call `g_core->RunCallbacks()` on the game thread. Keeps Discord IPC processing off the hot path without per-frame overhead.
- **`DeferredRefresh(int ticks)`** — self-rescheduling SKSE task used after `EditingCharacter → Playing` to wait ~10 frames for the engine to commit the new character name.

**`AdditionalFunctions.cpp`** — `Cp1251ToUtf8`, `IsValidUtf8`. Used by `SafeStr()` to handle Russian locale game data.

**`discord_loader.cpp`** — `__pfnDliNotifyHook2` delay-load hook. Intercepts `discord_game_sdk.dll` load and resolves it from the plugin's own directory instead of the system search path.

### Log file

`%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\DragonbornPresence.log`

---

## Dependencies (bundled or auto-fetched)

| Dependency | Version | How |
|---|---|---|
| [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) | v3.7.0 | CMake FetchContent |
| [Discord Game SDK](https://discord.com/developers/docs/game-sdk/sdk-starter-guide) | 3.2.1 | CMake download |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | CMake FetchContent |
| fmt | 10.2.1 | CMake FetchContent |
| spdlog | v1.13.0 | CMake FetchContent |

---

## License

[GPL-3.0](LICENSE)
