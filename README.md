# DragonbornPresence SE

An SKSE plugin for **Skyrim Special Edition** that displays your current in-game state as Discord Rich Presence.

While you play, Discord shows your character's name, race, level, and current location — and updates automatically as you move through the world.

---

## Features

- Current location displayed in Discord (worldspace + named location)
- Character info: name, race, and level
- Session timer showing how long you've been playing
- State-aware presence: Main Menu, Character Creation, Loading, and In-Game are all handled separately
- Localization support — English labels can be replaced via a text file

---

## Requirements

- Skyrim Special Edition **1.6.323**
- [SKSE64](https://skse.silverlock.org/) matching the above runtime

---

## Installation

1. Install SKSE64 for Skyrim SE 1.6.323.
2. Copy `DragonbornPresence.dll` into:
   ```
   Skyrim Special Edition\Data\SKSE\Plugins\
   ```
3. Copy the contents of the `src\` folder into your `Data\` directory (Papyrus scripts and the locale file).
4. Launch the game through SKSE.

Discord must be running before or alongside the game.

---

## Localization

The two UI strings shown in Discord (main menu and character creation labels) can be changed by editing:

```
Data\SKSE\Plugins\DragonbornPresenceLocale.txt
```

The file format:
```
; Line starting with ; is a comment
Main menu
Editing character
```

The first non-comment, non-empty line becomes the main menu label; the second becomes the character creation label. If the file is missing, English defaults are used.

---

## Building from Source

### Requirements

- CMake 3.21 or later
- Visual Studio 2022 with the **Desktop development with C++** workload
- Internet access for the first CMake configure (CommonLibSSE-NG is fetched automatically)

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

On the first configure CMake downloads [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) v3.7.0 — this takes a minute or two. Subsequent builds use the cache and are fast.

The output `DragonbornPresence.dll` appears in `build\Release\`.

### Targeting a different runtime

Edit `CMakeLists.txt` and change the `COMPATIBLE_RUNTIMES` value:

```cmake
add_commonlibsse_plugin(DragonbornPresence
    ...
    COMPATIBLE_RUNTIMES "1.6.640.0"   # change as needed
)
```

---

## Architecture

The plugin consists of three C++ source files and a set of Papyrus scripts.

### C++ side

**`main.cpp`** — Plugin entry point (`SKSEPluginLoad`). Sets up spdlog logging, reads the locale file, registers Papyrus native functions, and attaches the menu event handler.

**`DragonbornPresence.cpp`** — All Discord and game-state logic.

- **State machine** ([TinyFSM](https://github.com/digint/tinyfsm)): four states driven by `MenuOpenCloseEvent`.

  ```
  LoadingState (initial)
      ↓ Main Menu opens       → MainMenuState
      ↓ Loading Menu closes   → PlayingState
      ↓ RaceSex Menu opens    → EditingCharacterState
  ```

- **`DiscordMenuEventHandler::ProcessEvent`** — listens for `"Main Menu"`, `"Loading Menu"`, and `"RaceSex Menu"` events and dispatches the corresponding state transitions.

- **`UpdatePresence()`** — builds and sends a `DiscordRichPresence` struct. Called on every state entry and whenever Papyrus calls `UpdatePresenceData`.

- **`RegisterFuncs()`** — registers `UpdatePresenceData` and `SetGameLoaded` as Papyrus native functions, then initialises the Discord RPC connection.

**`AdditionalFunctions.cpp`** — Utilities: `Cp1251ToUtf8` (Windows-1251 → UTF-8 for Russian text), `is_valid_utf8`, `Format` (printf-style string formatting), `GetPlayer`.

### Papyrus side

**`DragonbornPresence.psc`** — Declares the two native functions and helpers:
- `UpdatePresenceData(String position, String playerInfo)` — called periodically by the tracker quest.
- `SetGameLoaded()` — called once on game load to trigger the Playing state.
- `GetCurrentPosition(worldspace, location, cell)` — formats a readable location string.
- `GetPlayerInfo(actor)` — formats `"Name - Race (Level)"`.

**`FZR_PlayerTrackerQuestScript.psc`** — Quest script that fires `SetGameLoaded()` on init and coordinates position updates.

**`FZR_PlayerReferenceLoadScript.psc`**, **`FZR_TrackInSameCell.psc`**, **`FZR_XMarkerReferenceScript.psc`** — Supporting scripts for the position tracking system.

### Log file

`%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\DragonbornPresence.log`

---

## Dependencies (bundled or auto-fetched)

| Dependency | Version | How |
|---|---|---|
| [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) | v3.7.0 | CMake FetchContent |
| [discord-rpc](https://github.com/discord/discord-rpc) | prebuilt lib | `discord_rpc/` in repo |
| [TinyFSM](https://github.com/digint/tinyfsm) | header-only | `tinyfsm/` in repo |
| spdlog | via CommonLibSSE-NG | transitive |

---

## License

[GPL-3.0](LICENSE)
