# DragonbornPresence SE — STB

Внутренний SKSE-плагин команды Skyrim True Believer. Репозиторий обслуживает только текущую сборку STB и не является универсальной интеграцией Discord Rich Presence для сторонних сборок Skyrim.

**Автор оригинала:** Fozar · **Адаптация для STB:** Frem

## Текущий контракт Presence

| Поле Discord | Значение | Пример |
|---|---|---|
| `details` | настраиваемая первая строка из параметров STB | `🟢Приключение` |
| `state` | настраиваемая вторая строка; по умолчанию уровень, смерти и камень | `lvl-14 💀-3 🎭-Атронах` |
| большая картинка | первое совпавшее правило текущей локации | `whiteruncapital` |
| подпись большой картинки | настраиваемая строка; по умолчанию имя персонажа | `Довакин` |
| маленькая картинка | только `loading` или `combat` | `combat` |
| подпись боевой иконки | настраиваемая строка; по умолчанию текущий бой | `В бою с Драугр (ур. 30)` |
| таймер | длительность текущего запуска Skyrim | не сбрасывается при обновлении Presence |

При наведении на большую картинку по умолчанию отображается имя персонажа. Подпись боевой иконки по умолчанию содержит имя и уровень текущей цели; пока Skyrim не определил цель — `В бою`.

Таймер получает один Unix timestamp при загрузке плагина. Смена локации, загрузка, бой и секундное обновление Presence не меняют время начала.

Через раздел **STB Widgets → Discord Presence** можно независимо менять обе строки Presence, подпись большой картинки и подпись боевой иконки. Кнопки параметров добавляют значения в текущую строку-шаблон.

## Источники данных STB

| Значение | Источник |
|---|---|
| уровень | `RE::PlayerCharacter::GetLevel()` |
| смерти | Global `aaMZgv_NowDeath` |
| сложность | `aaMZ_SelectedLevel_OfDifficulty` в alias-скрипте `aaMZ_MCMDataStorage` |
| камень | первое активное заклинание из 19 описаний камней STB |
| имя персонажа | `RE::PlayerCharacter::GetName()` |
| боги | runtime-список STB `aaMZfl_ActiveGodsList`; поддерживаются 9 Аэдра и 16 Даэдра |
| проклятие | активное заклинание `aaMZs_AedraCurseDUPLICATE001` |
| вампир | Global `aaMZgv_VampireBlood`; выводится только при значении больше нуля |
| вервольф | Global `aaMZgv_WerewolfBlood`; выводится только при значении больше нуля |
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
SKSE\Plugins\DragonbornPresence.user.json  (создаётся STB-Widgets)
```

Обязательные зависимости текущей сборки:

- Skyrim SE/AE и соответствующий SKSE64;
- Address Library for SKSE Plugins;
- текущий `STB.esp` с MCM data-storage quest;
- Discord Application ID `1527543892151373937` и загруженные STB Art Assets;
- запущенный Discord Desktop — только для активной публикации Presence.

Плагин и игровой runtime запускаются даже при выключенной интеграции или недоступном Discord Desktop. Перед созданием transport-сессии проверяются обработчик `discord://`, связанный исполняемый файл, запущенный процесс и локальный `discord_game_sdk.dll`. Runtime-код не скачивает, не устанавливает, не восстанавливает и не запускает Discord.

После ошибки Discord автоматических повторов нет. Следующая попытка выполняется только после явного изменения настроек или нажатия **«Перезагрузить конфигурацию Discord»** в STB-Widgets.

## Конфигурация

Используются два файла:

```text
<корень мода MO2>\SKSE\Plugins\DragonbornPresence.json
<корень мода MO2>\SKSE\Plugins\DragonbornPresence.user.json
```

`DragonbornPresence.json` поставляется вместе с плагином и содержит только каталог изображений:

```json
{
  "schema_version": 1,
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

`DragonbornPresence.user.json` — небольшой пользовательский overlay, который атомарно сохраняет STB-Widgets:

```json
{
  "schema_version": 1,
  "discord": {
    "enabled": true
  },
  "presence": {
    "details": "{difficulty}",
    "state": "lvl-{lvl} {deaths} {stone}",
    "large_text": "{player}",
    "combat_text": "{combat}"
  },
  "reload_revision": 1
}
```

Поддерживаются параметры `{difficulty}`, `{lvl}`, `{deaths}`, `{stone}`, `{player}`, `{god}`, `{vampire}`, `{werewolf}`, `{location}` и `{combat}`. `{deaths}` добавляет оформление `💀-` во время публикации, поэтому emoji не хранится в редактируемой строке меню. `{god}` содержит выбранных богов через запятую либо `Проклятие Аэдра`. Если бог, вампир или вервольф не выбраны, соответствующий параметр заменяется пустой строкой. Неизвестные параметры сохраняются как обычный текст.

`schema_version` проверяется до чтения остальных разделов. Поддерживается версия `1`: отсутствие поля принимается как legacy schema 1 с warning, а неверный тип или неподдерживаемая версия отклоняют изменённый документ и сохраняют последнюю рабочую конфигурацию.

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
| `text` | подпись выбранного location asset; доступна как `{location}` в шаблоне `large_text` |

Оба JSON-файла отслеживаются во время игры. Корректное изменение применяется на главном потоке Skyrim без пересборки DLL и перезапуска игры; незавершённый или повреждённый JSON не заменяет последнюю рабочую конфигурацию.

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

Общий `CMakePresets.json` не содержит пути конкретного компьютера и всегда создаёт DLL, PDB и ZIP. Локальное развёртывание в MO2 настраивается в ignored `CMakeUserPresets.json` через preset `vs2026-local`; пример приведён в [главе о сборке](docs/code-build.html#local).

Локальная сборка с deploy:

```bash
cmake --preset vs2026-local
cmake --build --preset release-local
```

Она обновляет обе DLL, PDB и исходный JSON в `<корень мода MO2>\SKSE\Plugins`. Готовый ZIP с корневым каталогом `SKSE` создаётся и общим, и локальным Release preset:

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
| `DragonbornPresenceApplicationTests` | запуск runtime без Discord, динамический toggle/retry, настраиваемые Presence-строки, loading/game transitions, polling, бой и fatal errors |
| `DragonbornPresenceAdapterTests` | CP1251 → UTF-8, base/user JSON overlay, hot reload, сохранение last-known-good и schema version |

Workflow `.github/workflows/ci.yml` запускает два независимых job на каждом push в `dev`, pull request и ручном запуске: все три CTest-цели и полную Release-сборку SKSE-плагина со строгой проверкой ZIP. Runtime smoke test в игре остаётся обязательным для изменений RE/SKSE.

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

## Релиз 4.0.0

```bash
git tag -a v4.0.0 -m "DragonbornPresence 4.0.0"
git push origin v4.0.0
```

Workflow `.github/workflows/release.yml` требует соответствия тега версии из `CMakeLists.txt`, собирает и запускает тесты, создаёт Visual Studio 2026 Release, проверяет точный manifest ZIP и только затем публикует `DragonbornPresence.zip`.

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
scripts/                                  — локальные и CI-проверки Release-артефактов
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
- [Архитектурный учебник](https://stb-team.github.io/DragonbornPresence-SE/code-entry.html)
- [Карта актуальных исходников](https://stb-team.github.io/DragonbornPresence-SE/source-walkthrough.html)
- [История изменений](CHANGELOG.md)

Лицензия: [GPL-3.0](LICENSE).
