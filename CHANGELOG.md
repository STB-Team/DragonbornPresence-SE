# Changelog

## 2.5.0 — 2026-07-17

- Added dedicated Presence modes, localized text, and configurable small-image keys for alchemy, smithing, enchanting, cooking, tanning, and smelting.
- Detect crafting activities from the active crafting submenu, furniture bench metadata, and stable Editor ID hints.
- Fall back to the generic `crafting` text and image for unsupported stations; an empty activity-specific image key also uses the generic crafting image.


## 2.4.0 — 2026-07-17

- Added configurable large-image overrides for loading, the main menu, character creation, and gameplay.
- Added ordered location image rules using stable worldspace, location, or cell Editor IDs, with an optional display-name substring matcher.
- Added Rich Presence states and small-image keys for menus, the map, inventory and magic screens, dialogue, crafting, and waiting.
- Defined presence priority as combat, UI context, active quest, then exploration.
- Added localized UI-state text to all 11 bundled languages.
- Kept Discord Game SDK integration; no OAuth or additional authorization window is required.

## 2.3.0 — 2026-07-17

- Added `DragonbornPresence.json` for configuring the Discord application ID, activity fields, timer, separator, and image assets.
- Added dynamic small images and localized hover text for loading, menus, character editing, exploration, active quests, and combat.
- Added privacy controls for character details, location, quests, and combat information.
- Added a visible loading presence instead of retaining stale activity during loading screens.
- Added safe UTF-8 truncation for Discord's 127-byte text limit.
- Skip duplicate Discord activity updates when the displayed content has not changed.
- Include the default configuration in the FOMOD-ready release archive.
- Added an optional CMake deployment directory for copying rebuilt DLLs directly into an MO2 mod without overwriting user configuration.

## 2.2.0 — 2026-05-20

FOMOD installer included — mod managers will now ask you to pick a language during installation
Built-in translations for English, Russian, German, French, Spanish, Italian, Polish, Chinese (Simplified), Japanese, Korean, and Portuguese (Brazilian)
Combat presence now correctly reads as "In combat with Alduin" — the enemy name is placed inside the phrase rather than just appended, so each language can put the name where its grammar expects it
Added a separate display string for the rare moment when you are in combat but the game has not yet resolved the enemy name ("In combat" instead of nothing)

## 2.1.1 — 2026-05-20

discord_game_sdk.dll moved from the Skyrim root folder into Data\SKSE\Plugins where it belongs — mod managers now track and uninstall it cleanly
If you have an old discord_game_sdk.dll sitting next to SkyrimSE.exe, you can delete it
Fixed combat presence sometimes not clearing after combat ended

## 2.1.0 — 2026-05-15

Combat is now shown in Discord: while fighting, the active quest is replaced by the enemy's name (e.g. Skyrim: Helgen · In combat with Alduin)
Presence reverts to the quest name as soon as combat ends

## 2.0.1 — 2026-05-13

Active quest name now appears in Discord alongside your location (e.g. Skyrim: Whiterun · Bleak Falls Barrow)
Fixed a crash on startup when discord_game_sdk.dll was missing from the installation
Fixed presence sometimes freezing for several seconds during location transitions
Fixed a ghost "Loading" screen flash that appeared right after the game launched
Locale file format changed from .txt to .json — if you had a custom locale file, rename it and wrap the values in standard JSON
