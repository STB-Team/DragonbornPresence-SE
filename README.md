# DragonbornPresence SE

SKSE-плагин для Discord Rich Presence в сборке Skyrim True Believer. Плагин читает данные персонажа и STB напрямую из игры, выбирает изображение текущей локации и обновляет статус один раз в секунду.

## Что отображается

| Поле Discord | Формат | Пример |
|---|---|---|
| `details` | выбранная сложность STB | `🟢Приключение` |
| `state` | `lvl-<уровень> 💀-<смерти> <камень>` | `lvl-14 💀-3 🎭-Атронах` |
| большая картинка | первое подходящее правило локации | `whiteruncapital` |
| маленькая картинка | `loading` или `combat` | `combat` |

При наведении на боевую иконку отображается `В бою с <имя противника>`. Если цель ещё не определена — `В бою`.

Плагин не отправляет имя и расу персонажа, задания, содержимое меню, крафт, таймеры или выбранных богов.

## Требования

- Skyrim Special Edition или Anniversary Edition;
- [SKSE64](https://skse.silverlock.org/) для установленной версии игры;
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444);
- `STB.esp` и MCM-хранилище Skyrim True Believer;
- запущенный Discord Desktop с включённым отображением игровой активности.

## Установка

### Через менеджер модов

Установите `DragonbornPresence.zip` как обычный мод. Архив уже содержит структуру `SKSE/Plugins` и FOMOD.

### Вручную

Скопируйте каталог `SKSE` из архива в каталог `Data` игры.

Итоговые файлы:

```text
Data\SKSE\Plugins\DragonbornPresence.dll
Data\SKSE\Plugins\discord_game_sdk.dll
Data\SKSE\Plugins\DragonbornPresence.json
```

Запускайте игру через SKSE. Если Discord недоступен, плагин продолжит работать без Rich Presence.

## Конфигурация Discord

Файл настроек:

```text
Data\SKSE\Plugins\DragonbornPresence.json
```

Минимальная структура:

```json
{
  "schema_version": 1,
  "discord": {
    "enabled": true,
    "application_id": "1527543892151373937"
  },
  "assets": {
    "large_image": "stb_logo",
    "large_text": "Skyrim True Believer",
    "small_images": {
      "loading": "loading",
      "combat": "combat"
    },
    "location_images": [
      {
        "location": "WhiterunLocation",
        "image": "whiteruncapital",
        "text": "Вайтран"
      }
    ]
  }
}
```

### Основные параметры

| Ключ | Назначение |
|---|---|
| `discord.enabled` | включает или отключает Discord Presence |
| `discord.application_id` | ID приложения Discord Developer Portal |
| `assets.large_image` | резервная большая картинка |
| `assets.large_text` | резервная подсказка большой картинки |
| `assets.small_images.loading` | иконка загрузки |
| `assets.small_images.combat` | иконка боя |
| `assets.location_images` | упорядоченные правила картинок локаций |

Некорректные значения записываются в лог и заменяются безопасными значениями по умолчанию.

## Правила локаций

Правила проверяются сверху вниз. Используется первое правило, у которого совпали все заданные селекторы.

```json
"location_images": [
  {
    "worldspace": "DLC2SolstheimWorld",
    "image": "solstheim",
    "text": "Солстхейм"
  },
  {
    "location": "WhiterunLocation",
    "image": "whiteruncapital",
    "text": "Вайтран"
  },
  {
    "match": "Нилхейм",
    "image": "bandit",
    "text": "Нилхейм"
  }
]
```

| Поле | Сопоставление |
|---|---|
| `worldspace` | Editor ID игрового мира |
| `location` | Editor ID текущей локации или её родителя |
| `cell` | Editor ID текущей ячейки |
| `match` | подстрока отображаемого названия; ASCII сравнивается без учёта регистра |
| `image` | обязательный Asset key картинки |
| `text` | подсказка; пустое значение заменяется `large_text` |

В комплекте находятся 414 правил. Для изменения правил достаточно отредактировать JSON и полностью перезапустить Skyrim; пересборка DLL не нужна.

## Изображения Discord

Загрузите изображения в **Discord Developer Portal → Rich Presence → Art Assets**. Asset key должен точно совпадать со значением в JSON.

Обязательный минимум:

- `stb_logo`;
- `loading`;
- `combat`;
- ключи используемых правил `location_images`.

Рекомендуемый формат — квадратный PNG 1024×1024. Discord обрезает маленькие изображения до круга, поэтому важные детали размещайте по центру.

## Диагностика

Лог плагина:

```text
%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\DragonbornPresence.log
```

Порядок проверки:

1. Убедитесь, что `application_id` относится к нужному приложению Discord.
2. Сверьте Asset keys в Developer Portal и JSON с учётом регистра.
3. Проверьте наличие трёх файлов плагина в `Data\SKSE\Plugins`.
4. Полностью перезапустите Discord и Skyrim.
5. Найдите в логе строки `Presence updated`, `large='...'` и сообщения `Config:`.

## Сборка из исходников

### Требования

- Visual Studio 2026 с компонентом **Разработка классических приложений на C++**;
- CMake 4.2 или новее;
- доступ в интернет при первой настройке зависимостей.

Проект использует генератор `Visual Studio 18 2026`, платформу `x64` и набор инструментов `v145`.

```bash
git clone https://github.com/STB-Team/DragonbornPresence-SE.git
cd DragonbornPresence-SE
cmake --preset vs2026
cmake --build --preset release
```

Готовый архив:

```text
build\vs2026\DragonbornPresence.zip
```

Debug-сборка:

```bash
cmake --build --preset debug
```

### Развёртывание в MO2

```bash
cmake --preset vs2026 -DDRAGONBORNPRESENCE_DEPLOY_DIR="D:/Mod Organizer/mods/DragonbornPresence SE/SKSE/Plugins"
cmake --build --preset release
```

После сборки DLL и Discord SDK копируются в указанный каталог. Пользовательский `DragonbornPresence.json` не перезаписывается.

## Публикация релиза

Workflow `.github/workflows/release.yml` запускается по тегам `v*` на образе `windows-2025-vs2026`.

```bash
git tag -a v2.5.0 -m "DragonbornPresence 2.5.0"
git push origin v2.5.0
```

GitHub Actions собирает проект через Visual Studio 2026, создаёт Release и прикрепляет готовый `DragonbornPresence.zip`. Результат доступен в разделах **Actions** и **Releases** репозитория.

## Архитектура

| Слой | Ответственность |
|---|---|
| `model` | конфигурация, снимки состояния, payload и строгие enum-типы |
| `text` | UTF-8, CP1251, ограничение полей Discord и поиск подстрок |
| `configuration::ConfigLoader` | чтение и валидация JSON |
| `assets::LocationAssetResolver` | выбор изображения локации |
| `game::StbDataProvider` | чтение форм STB и состояния игрока |
| `integration::DiscordPresenceClient` | Discord Game SDK и подавление дубликатов |
| `application::PresenceCoordinator` | состояния загрузки, события и периодическое обновление |

Дополнительные модули:

- `main.cpp` — точка входа SKSE;
- `ScriptUtils.h` — чтение свойства сложности из alias-скрипта;
- `AdditionalFunctions.cpp` — преобразование игровых строк;
- `discord_loader.cpp` — загрузка `discord_game_sdk.dll` из каталога SKSE.

## Зависимости

| Зависимость | Версия |
|---|---|
| CommonLibSSE-NG | 3.7.0 |
| Discord Game SDK | 3.2.1 |
| nlohmann/json | 3.11.3 |
| fmt | 10.2.1 |
| spdlog | 1.13.0 |

Зависимости загружаются CMake при первой настройке проекта.

## Документация

- [Компактное руководство](https://stb-team.github.io/DragonbornPresence-SE/)
- [PDF для печати](docs/Discord-Presence-Configuration-RU.pdf)
- [История изменений](CHANGELOG.md)

## Лицензия

[GNU General Public License v3.0](LICENSE)
