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
SKSE\Plugins\DragonbornPresence.pdb
SKSE\Plugins\discord_game_sdk.dll
SKSE\Plugins\DragonbornPresence.json
```

Обязательные зависимости текущей сборки:

- Skyrim SE/AE и соответствующий SKSE64;
- Address Library for SKSE Plugins;
- текущий `STB.esp` с MCM data-storage quest;
- запущенный Discord Desktop;
- Discord Application ID `1527543892151373937` и загруженные STB Art Assets.

Перед инициализацией плагин проверяет обработчик `discord://`, связанный исполняемый файл, запущенный процесс и локальный `discord_game_sdk.dll`. Если Discord Desktop не установлен или не запущен, работа плагина прекращается до вызова SDK. Runtime-код не скачивает, не устанавливает, не восстанавливает и не запускает Discord. Если клиент завершится позже, SDK вернёт ошибку, callback зависнет на 10 секунд или возникнет C++-исключение, дальнейшая работа интеграции прекращается; исключения не выпускаются в Skyrim.

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
D:\Stb\[STB] Mod Organizer\mods\DragonbornPresence\SKSE\Plugins\DragonbornPresence.pdb
D:\Stb\[STB] Mod Organizer\mods\DragonbornPresence\SKSE\Plugins\discord_game_sdk.dll
D:\Stb\[STB] Mod Organizer\mods\DragonbornPresence\SKSE\Plugins\DragonbornPresence.json
```

Каталог развертывания уже задан пресетом `vs2026`; при сборке JSON из `config` обновляется вместе с DLL. Готовый ZIP мода с корневым каталогом `SKSE` устанавливается напрямую через MO2:

```text
D:\Dev\STB-Discord-Integration\DragonbornPresence-SE\build\vs2026\DragonbornPresence.zip
```

## Автоматические проверки

Тестовая сборка не создаёт SKSE-плагин и не требует запущенной игры:

```bash
cmake --preset vs2026-tests
cmake --build --preset tests
ctest --preset tests
```

CTest запускает три изолированные цели:

| Цель | Контракты |
|---|---|
| `DragonbornPresenceCoreTests` | UTF-8, лимит Discord-строк, сложность и выбор первого правила локации |
| `DragonbornPresenceApplicationTests` | запуск, loading/game transitions, polling, бой, ошибки и постоянная остановка координатора |
| `DragonbornPresenceAdapterTests` | CP1251 → UTF-8 и чтение/валидация JSON-конфигурации |

Workflow `.github/workflows/ci.yml` выполняет эти команды на каждом push в `dev`, pull request и ручном запуске. Тестовые цели не линкуют Discord SDK и Skyrim runtime; зависящее от RE/SKSE поведение дополнительно проверяется полной Release-сборкой и запуском в игре.

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

Подробная строка снимка записывается только при изменении отправляемого Presence; неизменный polling раз в 500 мс лог не засоряет.

Если Skyrim не обрабатывает задачу Discord в главном потоке дольше 500 мс, лог
записывает имя задачи, приблизительное время ожидания и число объединённых
callback-запросов. При продолжающемся ожидании предупреждение повторяется раз в
5 секунд:

```text
Discord task 'RunCallbacks/RefreshPresence' has been waiting on Skyrim's main thread for about 500 ms; 1 callback request coalesced.
```

Боевые события не создают отдельные задачи главного потока: несколько событий
объединяются и обрабатываются ближайшим общим polling-циклом через не более чем
500 мс.

Ошибки Discord записываются с названием, числовым кодом и объяснением. Все 45
кодов `discord::Result` из SDK 3.2.1 имеют явную расшифровку. Пример:

```text
Discord operation 'UpdateActivity callback' failed: RateLimited (code 34). Explanation: Discord is rate-limiting requests; retrying was disabled. The integration is being disabled; Skyrim can continue normally.
```

`DragonbornPresence.pdb` содержит символы оптимизированной Release-сборки для
расшифровки адресов в crash-log. PDB не меняет поведение DLL и сам по себе не
предотвращает сбои, но позволяет определить точную функцию и строку при разборе
ошибки.

## Релиз 3.1.8

```bash
git tag -a v3.1.8 -m "DragonbornPresence 3.1.8"
git push origin v3.1.8
```

Workflow `.github/workflows/release.yml` использует runner `windows-2025-vs2026`, собирает Visual Studio 2026 Release и прикрепляет `DragonbornPresence.zip` к GitHub Release.

Для следующего релиза сначала обновляется версия в `CMakeLists.txt`, затем создаётся новый тег `vX.Y.Z`.

## Архитектура

Зависимости направлены внутрь: адаптеры знают application-порты и core-модели, но `core` и `application` не включают RE/SKSE или Discord SDK.

| Слой | Ответственность |
|---|---|
| `core` | конфигурация, снимки игрока, Presence payload, UTF-8, сложность и выбор ресурса локации |
| `application::ports` | интерфейсы конфигурации, игровых данных, Presence-транспорта и логирования |
| `application::PresenceCoordinator` | loading/game/combat transitions, polling и формирование Presence без типов Skyrim/Discord |
| `adapters::config::JsonConfigProvider` | чтение и валидация `DragonbornPresence.json` |
| `adapters::SkyrimTrueBeliever` | формы STB, Papyrus, строки CP1251, события Skyrim, scheduler и SKSE-логирование |
| `adapters::discord` | проверка Discord Desktop, безопасная загрузка SDK, callbacks, timeout и подавление дубликатов |
| `src/DragonbornPresence.cpp` | composition root: создаёт адаптеры и связывает их с координатором |
| `src/main.cpp` | узкий SKSE entry point и маршрутизация lifecycle-сообщений |

Структура репозитория:

```text
include/DragonbornPresence/core/          — engine-independent модели и правила
include/DragonbornPresence/application/   — coordinator и порты
include/DragonbornPresence/adapters/      — публичные контракты инфраструктуры
src/core/                                 — чистая доменная логика
src/application/                          — orchestration через порты
src/adapters/SkyrimTrueBeliever/          — единственный Skyrim/STB runtime adapter
src/adapters/discord/                     — Discord Game SDK adapter
src/adapters/config/                      — JSON adapter
tests/                                    — автоматические core/application/adapter tests
config/                                   — исходный JSON релизного архива
docs/                                     — веб-документация и описание Nexus
.github/workflows/                        — CI и GitHub Release
```

Ключевой путь выполнения:

```text
SKSE message/event
  → StbRuntimeAdapter
  → PresenceCoordinator
  → IGameDataSource / IPresenceClient / ILogger
  → StbGameDataSource / DiscordPresenceClient / SkseLogger
```

## Ссылки

- [Внутренняя веб-документация](https://stb-team.github.io/DragonbornPresence-SE/)
- [История изменений](CHANGELOG.md)

Лицензия: [GPL-3.0](LICENSE).
