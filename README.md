# DragonbornPresence SE — STB

Внутренний SKSE-плагин команды Skyrim True Believer. Репозиторий обслуживает только текущую сборку STB и не является универсальной интеграцией Discord Rich Presence для сторонних сборок Skyrim.

**Автор оригинала:** Fozar · **Адаптация для STB:** Frem

## Текущий контракт Presence

| Поле Discord | Значение | Пример |
|---|---|---|
| `details` | выбранная сложность STB | `🟢Приключение` |
| `state` | `lvl-<уровень> 💀-<смерти> <камень>` | `lvl-14 💀-3 🎭-Атронах` |
| большая картинка | первое совпавшее правило текущей локации | `whiteruncapital` |
| маленькая картинка | только `loading` или `combat` | `combat` |
| таймер | длительность текущего запуска Skyrim | не сбрасывается при обновлении Presence |

При наведении на боевую иконку отображается `В бою с <имя противника> (ур. <уровень>)`. Пока Skyrim не определил цель — `В бою`.

Таймер получает один Unix timestamp при загрузке плагина. Смена локации, загрузка, бой и секундное обновление Presence не меняют время начала.

## Источники данных STB

| Значение | Источник |
|---|---|
| уровень | `RE::PlayerCharacter::GetLevel()` |
| смерти | Global `aaMZgv_NowDeath` |
| сложность | `aaMZ_SelectedLevel_OfDifficulty` в alias-скрипте `aaMZ_MCMDataStorage` |
| камень | первое активное заклинание из 19 описаний камней STB |
| локация | worldspace, текущая location и parent cell |
| противник | имя и уровень текущей combat target игрока |

Значения сложности:

| Индекс | Presence |
|---:|---|
| `0` | `🟢Приключение` |
| `1` | `🟡Тактика` |
| `2` | `🔴Героический` |
| `3` | `⚫Испытание богов` |
| `4` | `⚪Свой уровень сложности` |

Если форма STB недоступна, используются значения `—`, `не выбран` или `не определена`. Плагин не останавливает Skyrim из-за отсутствующего значения.

## Рабочее окружение команды

```text
Репозиторий:
D:\Dev\STB-Discord-Integration\DragonbornPresence-SE

Корень мода MO2:
D:\Stb\[STB] Mod Organizer\mods\DragonbornPresence

Плагины мода:
D:\Stb\[STB] Mod Organizer\mods\DragonbornPresence\SKSE\Plugins
```

Файлы runtime:

```text
SKSE\Plugins\DragonbornPresence.dll
SKSE\Plugins\discord_game_sdk.dll
SKSE\Plugins\DragonbornPresence.json
```

Обязательные зависимости текущей сборки:

- Skyrim SE/AE и соответствующий SKSE64;
- Address Library for SKSE Plugins;
- текущий `STB.esp` с MCM data-storage quest;
- запущенный Discord Desktop;
- Discord Application ID `1527543892151373937` и загруженные STB Art Assets.

Перед инициализацией плагин проверяет обработчик `discord://`, связанный исполняемый файл и соответствующий запущенный процесс. Если Discord Desktop не установлен или не запущен, точка входа SKSE возвращает отказ до инициализации плагина: `discord_game_sdk.dll` не загружается, Discord автоматически не запускается. Если клиент завершится позже либо Discord SDK вернёт ошибку, callback-loop и вся дальнейшая работа Presence немедленно прекращаются без повторных обращений к SDK.

## Конфигурация

Рабочий файл:

```text
D:\Stb\[STB] Mod Organizer\mods\DragonbornPresence\SKSE\Plugins\DragonbornPresence.json
```

Основной блок:

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
    "location_images": []
  }
}
```

### Правила локаций

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

| Поле | Условие |
|---|---|
| `worldspace` | Editor ID игрового мира |
| `location` | Editor ID текущей локации или любого родителя |
| `cell` | Editor ID текущей ячейки |
| `match` | подстрока отображаемого имени; ASCII без учёта регистра |
| `image` | обязательный Asset key |
| `text` | подсказка; пустое значение заменяется `large_text` |

В текущем JSON находятся 438 правил. Изменение JSON требует полного перезапуска Skyrim, но не пересборки DLL.

## Сборка Visual Studio 2026

Требования:

- Visual Studio 2026 с C++ workload;
- CMake 4.2 или новее;
- генератор `Visual Studio 18 2026`;
- платформа `x64` и toolset `v145`.

Первая настройка:

```bash
cmake --preset vs2026
```

Release:

```bash
cmake --build --preset release
```

Debug:

```bash
cmake --build --preset debug
```

После каждой сборки автоматически обновляются:

```text
D:\Stb\[STB] Mod Organizer\mods\DragonbornPresence\SKSE\Plugins\DragonbornPresence.dll
D:\Stb\[STB] Mod Organizer\mods\DragonbornPresence\SKSE\Plugins\discord_game_sdk.dll
D:\Stb\[STB] Mod Organizer\mods\DragonbornPresence\SKSE\Plugins\DragonbornPresence.json
```

Каталог развертывания уже задан пресетом `vs2026`; при сборке JSON из `config` обновляется вместе с DLL. Готовый ZIP мода с корневым каталогом `SKSE` устанавливается напрямую через MO2:

```text
D:\Dev\STB-Discord-Integration\DragonbornPresence-SE\build\vs2026\DragonbornPresence.zip
```

## Диагностика

Лог:

```text
%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\DragonbornPresence.log
```

Проверка таймера:

```text
Discord Game SDK initialized for application ...; session_start=1784380000.
Presence updated; session_start=1784380000.
Presence updated; session_start=1784380000.
```

`session_start` должен оставаться одинаковым после загрузки и смены локации.

Проверка локации:

```text
[<причина>] level=<...> deaths=<...> location='<...>' large='<asset>' combat='<...>'
```

Подробная строка снимка записывается только при изменении отправляемого Presence; неизменный секундный polling лог не засоряет.

## Релиз 3.1.4

```bash
git tag -a v3.1.4 -m "DragonbornPresence 3.1.4"
git push origin v3.1.4
```

Workflow `.github/workflows/release.yml` использует runner `windows-2025-vs2026`, собирает Visual Studio 2026 Release и прикрепляет `DragonbornPresence.zip` к GitHub Release.

Для следующего релиза сначала обновляется версия в `CMakeLists.txt`, затем создаётся новый тег `vX.Y.Z`.

## Архитектура

| Слой | Ответственность |
|---|---|
| `model` | конфигурация, снимки состояния и payload |
| `text` | UTF-8, CP1251 и ограничение Discord-полей |
| `configuration::ConfigLoader` | чтение и валидация JSON |
| `assets::LocationAssetResolver` | применение 438 правил локаций |
| `game::StbDataProvider` | формы STB и снимок игрока |
| `integration::DiscordPresenceClient` | Discord SDK, timestamp и подавление дубликатов |
| `application::PresenceCoordinator` | загрузка, бой, события и секундный polling |

Структура репозитория:

```text
src/       — реализации и точка входа SKSE
include/   — заголовки и precompiled header
config/    — исходный DragonbornPresence.json для релизного архива
docs/      — веб-документация и описание Nexus
.github/   — workflow сборки GitHub Release
```

Ключевые файлы:

- `src/main.cpp` — точка входа SKSE;
- `include/ScriptUtils.h` — чтение сложности из alias-скрипта STB;
- `src/AdditionalFunctions.cpp` — преобразование игровых строк;
- `src/discord_loader.cpp` — проверка установленного и запущенного Discord и отложенная загрузка Discord SDK из `SKSE/Plugins`.

## Ссылки

- [Внутренняя веб-документация](https://stb-team.github.io/DragonbornPresence-SE/)
- [История изменений](CHANGELOG.md)

Лицензия: [GPL-3.0](LICENSE).
