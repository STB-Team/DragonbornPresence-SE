# DragonbornPresence SE

An SKSE plugin for the **Skyrim True Believer** setup that publishes selected STB character data as Discord Rich Presence.

During gameplay Discord shows two stable lines: character level and active standing stone, then death count and selected difficulty. The large image follows the current location; loading and combat use the only small state icons.

---

## Features

- Fixed first line: `Уровень: <value> • Камень: <value>`
- Fixed second line: `Смертей: <value> • Сложность: <value>`
- Level read directly from the player; deaths, difficulty, and standing stone read from STB runtime data
- 414 ordered location rules with city, settlement, dungeon, and encounter art
- `loading` small icon while the game is loading
- `combat` small icon in combat; its hover text shows `В бою с <enemy name>` when Skyrim exposes the current target
- One-second game-thread polling keeps STB values, combat, and location art current without per-frame work
- Built-in Russian Presence text; no locale files or language installer
- Graceful degradation if Discord is not running or an STB value is unavailable

---

## Requirements

- Skyrim Special Edition or Anniversary Edition supported by CommonLibSSE-NG
- [SKSE64](https://skse.silverlock.org/) matching the runtime
- Address Library for SKSE Plugins
- Skyrim True Believer data (`STB.esp` and its MCM data-storage quest) for deaths, standing stone, and difficulty
- Discord Desktop with activity sharing enabled

---

## Installation

**Mod manager (recommended):** Install the archive normally. The FOMOD installer has no language-selection step.

**Manual:** Extract the archive and copy `SKSE\` into your Skyrim `Data\` directory.

Launch the game through SKSE. Discord must be running before or alongside the game — if it is not running the plugin loads normally and presence is simply disabled.

---

## Discord configuration

`Data\SKSE\Plugins\DragonbornPresence.json` controls the Discord application and image assets. Invalid values are logged and fall back to built-in defaults. The plugin uses Discord Game SDK and does not open an OAuth or authorization window.

A printable Russian guide is available at [`docs/Discord-Presence-Configuration-RU.pdf`](docs/Discord-Presence-Configuration-RU.pdf). The same guide is published as a [GitHub Pages site](https://stb-team.github.io/DragonbornPresence-SE/).

```json
{
  "schema_version": 1,
  "discord": {
    "enabled": true,
    "application_id": "1527543892151373937"
  },
  "assets": {
    "large_image": "stb_logo",
    "large_text": "The Elder Scrolls V: Skyrim",
    "small_images": {
      "loading": "loading",
      "combat": "combat"
    },
    "location_images": [
      { "location": "WhiterunLocation", "image": "whiteruncapital", "text": "Вайтран" },
      { "worldspace": "DLC2SolstheimWorld", "image": "solstheim", "text": "Солстхейм" },
      { "match": "STB Dev Room", "image": "stb_room", "text": "STB Dev Room" }
    ]
  }
}
```

### Fixed Presence contract

The text layout is intentionally not configurable:

| Discord field | Runtime text |
|---|---|
| `details` | `Уровень: <level> • Камень: <standing stone>` |
| `state` | `Смертей: <deaths> • Сложность: <difficulty>` |
| large-image hover | Matched location-rule `text`, otherwise `large_text` |
| combat small-image hover | `В бою с <enemy>`; falls back to `В бою` while the target is unresolved |
| loading small-image hover | `Загрузка` |

No selected gods, character name, race, quest, menu, crafting, timer, or other state is sent.

Discord itself renders `details` and `state` with different colors and may prefix the state/party-status row with a people glyph. Discord Game SDK exposes the text and asset fields, but not client font color or that built-in glyph, so the plugin cannot make both rows white or remove that UI element.

### Asset selection

- `large_image` is the fallback when no location rule matches.
- `large_text` is the fallback hover text for the large image.
- `location_images` is evaluated from top to bottom; the first matching rule wins.
- `worldspace`, `location`, and `cell` match stable Creation Kit/xEdit Editor IDs.
- `match` searches the displayed location; ASCII comparison is case-insensitive.
- If a rule has several selectors, all selectors must match.
- `image` is the Discord Art Asset key; `text` is its hover text.
- The bundled JSON contains 414 location rules. Adding or changing a location requires only a JSON edit and a full Skyrim restart; it does not require rebuilding the DLL.
- `small_images` supports only `loading` and `combat`.

### Images to upload

The Discord application referenced by `application_id` must contain:

- `stb_logo` or another configured fallback image;
- every `image` key used by the desired `location_images` rules;
- `loading` and `combat`.

File names may differ, but the **Asset key in Discord Developer Portal** must exactly match the JSON value. Square 1024×1024 PNG files are recommended. Discord crops small assets to a circle, so keep important details centered.

## Language

All Presence state labels are built into the plugin in Russian. The archive does not include language choices or `DragonbornPresenceLocale.json`; changing the game language does not change these labels.

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

For local development, configure an optional MO2 deployment directory:

```bash
cmake -S . -B build -DDRAGONBORNPRESENCE_DEPLOY_DIR="D:/path/to/Mod Organizer/mods/DragonbornPresence SE/SKSE/Plugins"
cmake --build build --config Release
```

Each successful build copies `DragonbornPresence.dll` and `discord_game_sdk.dll` to that directory. The user-edited `DragonbornPresence.json` file is intentionally left untouched.

---

## Architecture

Pure C++ SKSE DLL — no plugin-owned `.esp` or Papyrus scripts. The plugin reads data exposed by the installed STB mod.

**`main.cpp`** — plugin entry point, logging, configuration load, and SKSE messaging. `kDataLoaded` initializes STB forms and event sinks; `kNewGame` and `kPostLoadGame` trigger the first gameplay refresh.

**`DragonbornPresence.cpp`** — STB data collection and Discord activity:

- **`InitializeStbData()`** resolves `aaMZgv_NowDeath`, the STB MCM data-storage quest, and the 19 standing-stone description spells.
- **`ReadPlayerSnapshot()`** reads player level, deaths, selected stone, selected difficulty, location, combat state, and the current combat target.
- **`BuildPosition()`** walks the parent-location chain and produces a stable worldspace/location string used only for location-image matching.
- **`ResolveLargeAsset()`** applies the 414 ordered JSON rules and falls back to `assets.large_image`.
- **`MenuEventSink`** switches to loading Presence for the main menu and loading screens.
- **`CombatSink`** requests an immediate refresh on player combat transitions.
- **`StartCallbackThread()`** posts Discord callbacks to the game thread every 100 ms and polls the complete Presence snapshot once per second.
- **`SendActivity()`** enforces Discord's 127-byte UTF-8 limits, skips duplicate payloads, sends both fixed text rows, location art, and loading/combat hover text.

**`ScriptUtils.h`** — reads the selected difficulty from `aaMZ_MCMDataStorage` through the quest alias script property/variable.

**`AdditionalFunctions.cpp`** — UTF-8 validation and CP1251-to-UTF-8 conversion for Russian game data.

**`discord_loader.cpp`** — delay-load hook resolving `discord_game_sdk.dll` from the SKSE plugin directory.

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
