# AGENTS.md

Эти инструкции действуют для всего репозитория DragonbornPresence-SE.

## Назначение проекта

DragonbornPresence — нативный SKSE-плагин только для текущей сборки Skyrim True Believer. Это не универсальная интеграция для произвольных сборок Skyrim.

Основные ограничения:

- игровой adapter всегда называется `SkyrimTrueBeliever`, не `skyrim`;
- STB Form IDs, Editor IDs, Papyrus scripts, камни и fallback-тексты считаются частью контракта текущего `STB.esp`;
- плагин не скачивает, не устанавливает, не восстанавливает и не запускает Discord;
- ошибка Presence должна остановить интеграцию, но не Skyrim.

Автор оригинала: Fozar. Адаптация STB: Frem.

## Источники истины

При расхождении информации использовать следующий приоритет:

1. текущий код и тесты;
2. `CMakeLists.txt` и `CMakePresets.json`;
3. `README.md`;
4. `docs/architecture.html`;
5. `CLEAN_ARCHITECTURE_MIGRATION.md` как историю уже завершённого переноса.

Версия проекта задаётся только строкой `project(... VERSION ...)` в `CMakeLists.txt`. Перед релизом синхронно обновляются CHANGELOG, README и страницы документации, содержащие номер версии.

## Структура репозитория

```text
include/DragonbornPresence/core/          чистые модели и правила
src/core/                                 реализации core
include/DragonbornPresence/application/   coordinator и application ports
src/application/                          application orchestration
include/DragonbornPresence/adapters/      контракты concrete adapters
src/adapters/SkyrimTrueBeliever/          RE/SKSE/STB integration
src/adapters/discord/                     Discord Game SDK integration
src/adapters/config/                      JSON configuration adapter
src/DragonbornPresence.cpp                composition root и публичный facade
src/main.cpp                              SKSE entry point
tests/                                    Catch2 tests
config/DragonbornPresence.json            shipped runtime configuration
docs/                                     GitHub Pages documentation
.github/workflows/                        CI и release workflows
```

## Направление зависимостей

Зависимости направлены внутрь:

```text
adapters -> application ports -> core
```

Обязательные правила:

- `core` не включает RE, SKSE, WinAPI, Discord SDK, JSON или spdlog;
- `application` не включает RE, SKSE, WinAPI или Discord SDK;
- application-код работает только с core-моделями и интерфейсами из `application/ports`;
- concrete adapters реализуют порты и переводят внешние типы в принадлежащие приложению значения;
- `src/DragonbornPresence.cpp` выбирает concrete implementations, но не содержит JSON parsing, STB form lookup, Discord Activity mapping, event sinks или scheduler implementation;
- `src/main.cpp` остаётся узким entry point и не получает application/business logic.

Запрещено передавать через application boundary:

```cpp
RE::Actor*
RE::TESForm*
RE::PlayerCharacter*
RE::BSFixedString
discord::Activity
SKSE::TaskInterface*
```

Через границу проходят только owned строки, числа, флаги, enum и core-модели.

## Основной поток данных

```text
Skyrim/STB
  -> StbGameDataSource
  -> core::PlayerSnapshot
  -> application::PresenceCoordinator
  -> core::PresencePayload
  -> application::ports::IPresenceClient
  -> DiscordPresenceClient
  -> discord::Activity
```

События проходят отдельно:

```text
RE event
  -> StbRuntimeAdapter event sink
  -> engine-independent application signal
  -> PresenceCoordinator
  -> ближайший bounded Tick
```

Конфигурация:

```text
DragonbornPresence.json
  -> JsonConfigProvider
  -> core::Config
  -> PresenceCoordinator / adapters
```

## Ответственность ключевых компонентов

### `core::PlayerSnapshot`

Содержит полную owned-копию нужного состояния игрока. Новые игровые параметры обычно сначала добавляются сюда.

Использовать `std::optional<T>`, если Skyrim/STB может не предоставить значение. Не заменять неизвестное значение фиктивным нулём без явного продуктового решения.

### `StbGameDataSource`

Единственное место чтения игрового состояния для snapshot.

- дорогие и стабильные form lookups выполняются в `Initialize()`;
- текущее состояние читается в `ReadPlayerSnapshot()`;
- указатели на Skyrim objects не выходят из adapter;
- строки копируются и приводятся к UTF-8 существующими helpers;
- отсутствующая STB-форма приводит к fallback, а не к исключению или dereference null.

### `PresenceCoordinator`

Единственное место application-решений:

- loading/game state transitions;
- приоритеты состояний;
- формат `details` и `state`;
- выбор small image/text;
- запуск location resolver;
- coalescing refresh requests;
- реакция на inactive transport;
- диагностический log успешного изменения payload.

Не помещать форматирование Presence в game или Discord adapters.

### `StbRuntimeAdapter`

Владеет Skyrim event sinks, SKSE services и scheduler thread.

Каждый новый event sink:

1. является полем `StbRuntimeAdapter`, а не временным объектом;
2. регистрируется только после проверки нужного event source;
3. переводит RE-event в простые application-значения;
4. не передаёт RE-указатели coordinator;
5. имеет `try/catch` внутри `ProcessEvent`;
6. всегда возвращает `RE::BSEventNotifyControl::kContinue`;
7. не создаёт отдельный background thread;
8. по возможности coalesce-ится в ближайший общий `Tick()`.

Не все события приходят из `RE::ScriptEventSourceHolder`; использовать фактический source выбранного CommonLib event.

### `DiscordPresenceClient`

Единственное место Discord SDK mapping.

При добавлении нового поля `core::PresencePayload` обязательно:

- сопоставить поле с `discord::Activity`;
- включить поле в activity signature;
- сохранить duplicate suppression;
- сохранить pending callback guard;
- ограничить текстовые поля 127 байтами через `core::LimitUtf8Bytes`;
- не хранить входные `string_view` после `UpdateActivity()`;
- permanently disable transport после SDK error или timeout.

Если меняется только содержимое существующих `details`, `state`, `largeText` или `smallText`, Discord adapter обычно менять не нужно.

### `JsonConfigProvider`

Новое JSON-поле добавляется в таком порядке:

1. безопасный default в `core::Config`;
2. type-checked чтение в `JsonConfigProvider.cpp`;
3. сохранение default при отсутствии или неверном типе;
4. shipped значение в `config/DragonbornPresence.json`;
5. valid/missing/invalid tests в `JsonConfigProviderTests.cpp`.

Config adapter использует общий spdlog backend, а не SKSE headers. Не возвращать зависимость test target от CommonLibSSE только ради логирования.

## Добавление нового выводимого параметра

Обычный порядок:

1. определить Discord-поле, fallback и приоритет;
2. добавить owned значение в `core::PlayerSnapshot`;
3. прочитать значение в `StbGameDataSource`;
4. сформировать строку в `PresenceCoordinator::RefreshPresence`;
5. добавить параметр в диагностический log;
6. расширить `PresenceCoordinatorTests.cpp`;
7. при необходимости добавить Config/JSON;
8. менять `PresencePayload` и `DiscordPresenceClient` только для нового SDK-поля.

Polling уже перечитывает snapshot каждые 500 мс. Не добавлять Skyrim event для значения, которое корректно и достаточно быстро обнаруживается polling-циклом.

## Добавление нового события

Сначала определить, что требуется:

- длительное состояние: метод вида `OnDialogueChanged(bool)` и поле состояния coordinator;
- одноразовый refresh: метод вида `RequestDeathRefresh(...)` и pending flag;
- transient payload, который нельзя перечитать: adapter копирует значение и передаёт owned data.

Coordinator не должен принимать `RE::SomeEvent`.

Если появляется отдельная причина лога, расширить `core::RefreshReason` и `ToLogLabel()`.

Для нескольких новых refresh-событий предпочесть атомарную битовую маску с явным приоритетом вместо множества независимых задач SKSE. Очередь должна оставаться ограниченной одной pending main-thread task.

## Потоки и lifetime

Инварианты:

- Skyrim forms и Discord core обрабатываются на Skyrim main thread;
- scheduler thread только спит и вызывает `SKSE::TaskInterface::AddTask`;
- одновременно существует не более одной pending callback task;
- event storm не создаёт неограниченную очередь;
- `StbRuntimeAdapter` объявлен последним в composition root и уничтожается первым;
- его `std::jthread` останавливается и присоединяется до уничтожения coordinator и остальных adapters;
- Discord core не уничтожается из scheduler thread;
- `PresencePayload` string views действительны только на время `IPresenceClient::UpdateActivity`.

Не менять порядок глобальных объектов в `src/DragonbornPresence.cpp` без проверки lifetime.

## Ошибки

Политика проекта: fail closed для Presence, fail safe для Skyrim.

- исключения не выходят из `SKSEPluginLoad`;
- исключения не выходят из публичных функций `DragonbornPresence.h`;
- исключения не выходят из event sinks и SKSE tasks;
- неизвестное состояние Discord transport приводит к окончательной остановке интеграции в текущем процессе;
- отсутствующие optional данные используют fallback;
- отсутствие обязательного Skyrim/SKSE service предотвращает частичную регистрацию runtime.

Не добавлять скрытые retries, reconnect, автоматический запуск Discord или fake fallback implementation.

## Presence-инварианты

Текущий контракт:

- `details` — сложность STB;
- `state` — уровень, смерти и камень;
- large asset — первое совпавшее правило локации;
- small asset — loading или combat;
- session timestamp создаётся один раз на запуск;
- одинаковый payload повторно не отправляется;
- первое matching location rule побеждает;
- loading Presence не смешивается с gameplay Presence.

При добавлении новых состояний задавать явный приоритет в coordinator и проверять вход и выход из состояния.

## CMake

Sources и headers перечисляются явно. При создании нового файла добавить его в правильную цель:

- чистая логика — `DragonbornPresenceCore`;
- application orchestration — `DragonbornPresenceApplication`;
- infrastructure/RE/SKSE/Discord — plugin source lists;
- новый тест — соответствующий Catch2 executable.

Не добавлять RE/SKSE/Discord dependencies к core или application test targets.

Не добавлять generated build artifacts, DLL, PDB или ZIP в Git.

## Сборка

Production configure:

```bash
cmake --preset vs2026
```

Release build:

```bash
cmake --build --preset release
```

Release build должен создать:

```text
build/vs2026/Release/DragonbornPresence.dll
build/vs2026/Release/DragonbornPresence.pdb
build/vs2026/DragonbornPresence.zip
```

Preset также развёртывает DLL, PDB, Discord SDK и JSON в настроенный каталог MO2.

Debug не считается доказательством готовности релиза.

## Автоматические тесты

Configure:

```bash
cmake --preset vs2026-tests
```

Build:

```bash
cmake --build --preset tests
```

Run:

```bash
ctest --preset tests
```

Цели:

- `DragonbornPresenceCoreTests` — UTF-8, difficulty и location rules;
- `DragonbornPresenceApplicationTests` — coordinator lifecycle, payload, transitions, coalescing и failures;
- `DragonbornPresenceAdapterTests` — JSON и CP1251/UTF-8.

Тестировать observable contract, а не текст исходников или детали реализации.

Минимальные проверки новой возможности:

- нормальное значение;
- отсутствующее значение;
- неверный внешний input;
- fallback;
- приоритет состояния;
- несколько одинаковых событий;
- выход из состояния;
- transport failure;
- отсутствие duplicate update.

## Runtime-проверка

Изменения RE/SKSE, event registration, game data, Discord mapping, scheduler или lifetime требуют проверки в настоящей сборке STB.

Проверить:

1. загрузку плагина через SKSE;
2. регистрацию event sinks;
3. loading и возврат в игру;
4. состояние до, во время и после нового события;
5. подробный snapshot в `DragonbornPresence.log`;
6. фактический Discord Presence;
7. отсутствие повторных updates для одинакового payload;
8. неизменность `session_start`;
9. отсутствие необработанных исключений и роста очереди.

Успешная компиляция не доказывает runtime-поведение.

## Документация

При изменении пользовательского контракта обновить:

- `README.md`;
- `CHANGELOG.md`;
- `docs/index.html`;
- `docs/architecture.html`;
- `config/DragonbornPresence.json`, если менялась схема или default.

Документация должна описывать фактические файлы и namespace. Не возвращать старые имена `game::StbDataProvider`, `integration::DiscordPresenceClient`, `discord_loader.cpp`, `AdditionalFunctions` или `ScriptUtils`.

## Git и ветки

- обычная разработка выполняется в `dev`;
- `master` является release-веткой;
- merge в `master`, push тега и GitHub Release выполняются только по прямому запросу пользователя;
- не переписывать опубликованную историю;
- один commit должен содержать одно логическое изменение;
- не включать случайные пользовательские изменения;
- перед commit проверять `git diff --check`;
- после push подтверждать CI и чистый worktree.

CI workflow проверяет test preset. Release workflow запускается тегом `v*`, собирает конфигурацию Release и публикует `DragonbornPresence.zip`.

## Definition of Done

Изменение завершено только когда:

- соблюдены границы core/application/adapters;
- обновлены все callsites и explicit CMake lists;
- отсутствуют старые namespace и compatibility shims;
- добавлены необходимые contract tests;
- `ctest --preset tests` проходит;
- `cmake --build --preset release` проходит для production-изменений;
- изменённый runtime-путь проверен в игре, если он зависит от RE/SKSE/Discord;
- shipped JSON, документация и CHANGELOG синхронизированы;
- рабочее дерево не содержит generated или случайных файлов.
