# Перенос DragonbornPresence на чистую архитектуру

Этот файл — источник контекста для пошагового переноса. Если история диалога потерялась, сначала прочитать этот файл, затем проверить `git status -sb` и последние коммиты ветки `dev`.

## Формат совместной работы

1. Ассистент называет ровно один активный блок, объясняет его границы и показывает полный код либо точечные изменения.
2. Пользователь переносит код вручную и сообщает, что блок готов к проверке.
3. Ассистент читает все затронутые файлы, проверяет зависимости и остаточные ссылки, затем выполняет полную Release-сборку проекта.
4. При ошибке ассистент ничего не коммитит и показывает конкретное исправление. После исправления проверка повторяется полностью.
5. При успехе ассистент обновляет статус этого файла, добавляет в commit только файлы текущего блока, создаёт один тематический commit и отправляет его командой `git push origin dev`.
6. Только после успешного push начинается следующий блок. Незавершённый код следующего блока в commit не включается.

Ассистент никогда не отправляет изменения в `upstream`. Рабочая ветка — `dev`, целевой remote — `origin` (`STB-Team/DragonbornPresence-SE`).

## Неподвижные архитектурные решения

- Интеграция поддерживает только сборку Skyrim True Believer. Игровые адаптеры находятся в `adapters/SkyrimTrueBeliever`, а не в универсальном `adapters/skyrim`.
- `core` содержит только модели и чистые функции. В нём запрещены типы и заголовки RE, SKSE, Discord, Windows, JSON и файловой системы.
- `application` содержит сценарии и порты. В нём запрещены типы и заголовки RE, SKSE, Discord, Windows и JSON.
- `adapters/config` знает о JSON и runtime-пути конфигурации.
- `adapters/SkyrimTrueBeliever` знает о RE, SKSE, `STB.esp`, формах STB, игровых событиях и очереди главного потока Skyrim.
- `adapters/discord` знает о Discord Game SDK, загрузке `discord_game_sdk.dll` и преобразовании `PresencePayload` в `discord::Activity`.
- `src/DragonbornPresence.cpp` после переноса остаётся composition root: создаёт конкретные адаптеры, соединяет их с application-слоем и реализует узкий публичный фасад.
- `src/main.cpp` остаётся входным SKSE-адаптером и не получает бизнес-логику.
- Поведение Presence, тексты, порядок location rules, таймер сессии, интервал 500 мс, объединение callback-задач и политика окончательного отключения при ошибке не меняются во время архитектурного переноса.
- Чистый перенос не совмещается с переименованием пользовательских полей, изменением формата Presence или добавлением новых возможностей.
- Старые aliases, compatibility wrappers и параллельные реализации после успешного cutover не сохраняются.

## Целевое направление зависимостей

```text
main.cpp / DragonbornPresence.cpp (composition root)
                    |
                    +-------------------------------+
                    v                               v
adapters/SkyrimTrueBeliever                  adapters/discord
  StbRuntimeAdapter                           DiscordPresenceClient
  StbGameDataSource                           DiscordSdkLoader
                    \                         /
                     \                       /
                      v                     v
                         application
                    PresenceCoordinator
                   ports: config, game,
                    presence, logging
                             |
                             v
                            core
```

Зависимости направлены только внутрь. `core` ничего не знает о внешних слоях; `application` знает `core` и собственные порты; адаптеры реализуют порты и зависят от application/core.

## Целевая структура

```text
include/DragonbornPresence/
  application/
    PresenceCoordinator.h
    ports/
      IConfigProvider.h
      IGameDataSource.h
      IPresenceClient.h
      ILogger.h
  adapters/
    config/
      JsonConfigProvider.h
    discord/
      DiscordPresenceClient.h
      DiscordSdkLoader.h
    SkyrimTrueBeliever/
      StbGameDataSource.h
      StbRuntimeAdapter.h
  core/
    Config.h
    Difficulty.h
    LocationAssetResolver.h
    LocationAssets.h
    LocationContext.h
    PlayerSnapshot.h
    PresencePayload.h
    RefreshReason.h
    TextUtils.h

src/
  application/
    PresenceCoordinator.cpp
  adapters/
    config/
      JsonConfigProvider.cpp
    discord/
      DiscordPresenceClient.cpp
      DiscordSdkLoader.cpp
    SkyrimTrueBeliever/
      StbGameDataSource.cpp
      StbRuntimeAdapter.cpp
  core/
    Difficulty.cpp
    LocationAssetResolver.cpp
    TextUtils.cpp
  DragonbornPresence.cpp
  main.cpp
```

Имена могут уточняться только до начала соответствующего блока. После появления публичного символа его имя меняется через LSP, если сервер доступен, с проверкой всех references.

## Текущая точка восстановления

Текущее состояние:

- рабочая ветка — `dev`, публикация выполняется только в `origin/dev`;
- блоки B1–B10 завершены;
- `PresenceCoordinator` отвечает только за application orchestration и простые state transitions;
- Skyrim events, SKSE services и scheduler принадлежат временному `StbRuntimeAdapter` внутри composition unit;
- единственный исполняемый gate на время переноса — полная Release-сборка без ошибок.

Текущий активный блок: **B11 — извлечь application PresenceCoordinator**.

## Маршрут миграции

### B1. Чистые core-модели и resolver — завершён

Commit: `aca9bdf refactor: extract core models and location resolver`.

Результат: модели конфигурации, снимка игрока, локации, payload и чистый resolver вынесены из монолита.

### B2. Чистые текстовые функции и тесты — завершён

Commit: `f886119 перенос функции проверки текста на новою архитектуру и тесты`.

Результат: UTF-8-операции вынесены в `core`, для них существует отдельная проверка.

### B3. JSON config adapter — завершён

Commit: `33607f8 refactor: extract JSON config adapter`.

Результат: чтение файла и nlohmann/json изолированы в `adapters/config/JsonConfigProvider`.

### B4. Порты конфигурации и игровых данных — завершён

Commits:

- `568b1dc refactor: inject configuration provider port`;
- `3dbb9bb refactor: inject game data source port`.

Результат: координатор получает `IConfigProvider` и `IGameDataSource` через constructor injection, но конкретный игровой provider в опубликованном HEAD всё ещё находился в монолите.

### B5. Извлечь `StbGameDataSource` — завершён

Переносится только чтение состояния Skyrim/STB:

- константы форм `STB.esp`;
- определения камней;
- `FromGameString` для игровых строк;
- кэш `StbRuntimeData`;
- `Initialize`;
- `ReadPlayerSnapshot`;
- чтение сложности и камня;
- построение `LocationContext`;
- кэш последней боевой цели.

Целевые файлы:

- `include/DragonbornPresence/adapters/SkyrimTrueBeliever/StbGameDataSource.h`;
- `src/adapters/SkyrimTrueBeliever/StbGameDataSource.cpp`;
- include и composition wiring в `src/DragonbornPresence.cpp`;
- пути этих двух файлов в `CMakeLists.txt`.

Не входит в B5: `IPresenceClient`, Discord SDK, event sinks, scheduler и перенос координатора.

Критерии успеха:

- namespace строго `DragonbornPresence::adapters::SkyrimTrueBeliever`;
- в `DragonbornPresence.cpp` нет `StbDataProvider`, STB form IDs, stone definitions и прямого построения `PlayerSnapshot` из RE-объектов;
- `StbGameDataSource` реализует `IGameDataSource`;
- CMake использует каталог `SkyrimTrueBeliever`, а не `skyrim`;
- Release-сборка успешна.

Commit: `refactor: extract Skyrim True Believer game adapter`.

### B6. Внедрить output port Presence — завершён

Создать и подключить `application::ports::IPresenceClient`.

Изменения:

- `IPresenceClient` объявляет `Initialize`, `RunCallbacks`, `IsActive`, `UpdateActivity`, `Shutdown` без Discord-типов;
- временно оставшийся в монолите `DiscordPresenceClient` реализует этот интерфейс;
- `PresenceCoordinator` принимает `IPresenceClient&` через constructor injection и не создаёт transport самостоятельно;
- composition root владеет конкретным Discord-клиентом и передаёт ссылку координатору.

Критерии успеха:

- координатор обращается только к `IPresenceClient`;
- публичный порт не включает `discord.h` и `discord_loader.h`;
- нет изменения payload, deduplication, callback timeout и session timestamp;
- Release-сборка успешна.

Commit: `refactor: inject presence client port`.

### B7. Извлечь Discord Presence adapter — завершён

Создать:

- `include/DragonbornPresence/adapters/discord/DiscordPresenceClient.h`;
- `src/adapters/discord/DiscordPresenceClient.cpp`.

Перенести из `DragonbornPresence.cpp`:

- `DiscordResultDetails` и `DescribeResult`;
- SDK log hook;
- создание и владение `discord::Core`;
- callback processing и timeout;
- построение `discord::Activity`;
- UTF-8 limits полей Discord;
- signature deduplication;
- безопасное отключение транспорта;
- единый session start timestamp.

Критерии успеха:

- `DiscordPresenceClient final : public IPresenceClient`;
- в `DragonbornPresence.cpp` нет `discord::Core`, `discord::Activity` и `discord::Result`;
- только Discord adapter включает `discord.h`;
- поведение callback, deduplication и fail-closed сохранено;
- Release-сборка успешна.

Commit: `refactor: extract Discord presence adapter`.

### B8. Переместить Discord SDK loader в инфраструктуру адаптера — завершён

Перенести и переименовать существующие корневые файлы:

- `include/discord_loader.h` -> `include/DragonbornPresence/adapters/discord/DiscordSdkLoader.h`;
- `src/discord_loader.cpp` -> `src/adapters/discord/DiscordSdkLoader.cpp`.

Loader остаётся внутренней инфраструктурой Discord adapter. Application и core не получают доступа к нему.

Критерии успеха:

- проверки Windows protocol/process/DLL находятся только в `adapters/discord`;
- старые корневые loader-файлы и старые include-пути отсутствуют;
- delay-load и упаковка DLL в CMake сохранены;
- диагностическая логика запуска с Discord и без него сохранена;
- Release-сборка успешна.

Commit: `refactor: move Discord SDK loader into adapter`.

### B9. Изолировать application logging — завершён

Добавить application port `ILogger` и SKSE-реализацию во внешнем слое. Через порт проходят только сообщения orchestration/use-case слоя. STB и Discord adapters продолжают самостоятельно логировать свои технические ошибки.

Критерии успеха:

- будущий `PresenceCoordinator` не включает SKSE только ради логирования;
- logger port оперирует стандартными строками и не содержит fmt/SKSE типов;
- logging adapter гарантирует `noexcept` на внешней границе;
- тексты критических сообщений и политика безопасного отключения сохранены;
- Release-сборка успешна.

Commit: `refactor: inject application logger port`.

### B10. Разделить coordinator и Skyrim runtime внутри монолита — завершён

Перед физическим переносом файлов разделить ответственности, сохраняя их временно в `DragonbornPresence.cpp`:

Application coordinator должен отвечать за:

- загрузку `core::Config`;
- инициализацию game/presence ports;
- loading/game state;
- построение строк Presence;
- выбор location assets;
- вызов `RunCallbacks` и `UpdateActivity`;
- окончательную остановку use case.

STB runtime adapter должен отвечать за:

- `RE::MenuOpenCloseEvent` и `RE::TESCombatEvent`;
- получение UI, ScriptEventSourceHolder и SKSE TaskInterface;
- регистрацию event sinks;
- `std::jthread` scheduler;
- coalescing единственной main-thread задачи;
- перевод Skyrim events в простые application-вызовы.

Application API не принимает RE-типы. Runtime adapter передаёт только простые сигналы: game loaded, loading changed, combat refresh requested, periodic tick.

Критерии успеха:

- `PresenceCoordinator` не хранит event sinks или `SKSE::TaskInterface`;
- scheduler никогда не читает RE-объекты вне game-thread task;
- очередь по-прежнему ограничена одной pending-задачей;
- Release-сборка успешна.

Commit: `refactor: separate application and STB runtime responsibilities`.

### B11. Извлечь application `PresenceCoordinator`

Создать:

- `include/DragonbornPresence/application/PresenceCoordinator.h`;
- `src/application/PresenceCoordinator.cpp`.

Координатор зависит только от core и application ports. Добавить отдельную CMake-цель `DragonbornPresenceApplication`, которая линкуется только с `DragonbornPresenceCore`; plugin target линкуется с обеими внутренними целями.

Критерии успеха:

- application target компилируется без RE, SKSE, Discord, Windows и nlohmann/json headers;
- все зависимости координатора передаются через constructor injection;
- Release-сборка успешна.

Планируемый commit: `refactor: extract presence application service`.

### B12. Извлечь runtime adapter Skyrim True Believer

Создать:

- `include/DragonbornPresence/adapters/SkyrimTrueBeliever/StbRuntimeAdapter.h`;
- `src/adapters/SkyrimTrueBeliever/StbRuntimeAdapter.cpp`.

Перенести event sinks, регистрацию игровых сервисов, scheduler и main-thread task dispatch из временного runtime-блока монолита.

Критерии успеха:

- все RE/SKSE event-типы находятся во внешнем адаптере;
- adapter вызывает только engine-independent public API координатора;
- исключения не выходят через Skyrim callbacks;
- остановка корректно завершает `std::jthread` и не уничтожает Discord core в scheduler thread;
- Release-сборка успешна.

Планируемый commit: `refactor: extract Skyrim True Believer runtime adapter`.

### B13. Свести `DragonbornPresence.cpp` к composition root

Оставить в файле только:

- concrete instances в безопасном порядке владения;
- связывание config/game/presence/logger adapters с coordinator;
- связывание coordinator со STB runtime adapter;
- реализации `LoadConfig`, `RegisterGameEventHandlers`, `OnGameLoaded`;
- exception guards публичного фасада.

Порядок объектов должен гарантировать, что runtime и coordinator уничтожаются раньше зависимостей, на которые они хранят ссылки.

Критерии успеха:

- в composition root нет JSON parsing, STB form lookup, Discord Activity building, event sink классов и scheduler implementation;
- `main.cpp` использует прежний узкий фасад;
- Release-сборка и упаковка ZIP успешны.

Планируемый commit: `refactor: reduce plugin module to composition root`.

## Проверка каждого блока

Пока продолжается архитектурный перенос, единственный исполняемый gate перед commit — полная Release-сборка:

```text
cmake --build --preset release
```

Автоматические тесты, `ctest` и runtime smoke tests во время переноса не запускаются. Они будут возвращены только после завершения всех архитектурных блоков по отдельному решению пользователя. Успех промежуточного блока означает, что проект полностью собирается без ошибок; это не является утверждением о runtime-поведении.

Дополнительно ассистент обязан:

- прочитать полный diff текущего блока;
- проверить все usages изменённых публичных символов;
- найти старые namespace, include-пути и дубли перенесённой реализации;
- убедиться, что CMake перечисляет реальные пути и правильные target dependencies;
- не считать узкую компиляцию одного файла доказательством успешного plugin build.

## Git-протокол блока

После успешной проверки ассистент:

1. обновляет в этом файле статус завершённого и следующего активного блока;
2. добавляет только файлы и hunks текущего блока;
3. проверяет staged diff;
4. создаёт один commit с указанным в блоке сообщением;
5. выполняет `git push origin dev`;
6. сверяет, что локальная `dev` и `origin/dev` указывают на опубликованный commit;
7. сообщает hash, перечень проверок и следующий активный блок.

Нельзя включать в commit:

- незавершённую заготовку следующего блока;
- случайные пользовательские изменения;
- generated build artifacts;
- несвязанные форматирование и документацию.

## Восстановление потерянного контекста

Новый сеанс начинает работу так:

1. прочитать `CLEAN_ARCHITECTURE_MIGRATION.md`;
2. прочитать последние commits ветки `dev` и определить последний опубликованный блок;
3. проверить worktree и не считать незакоммиченные файлы завершёнными;
4. открыть секцию текущего активного блока;
5. сравнить его критерии с фактическими файлами;
6. продолжить с проверки уже написанного пользователем кода либо показать следующий ещё не перенесённый блок.

Главное правило продолжения: **один блок -> проверка -> commit -> `git push origin dev` -> следующий блок**.
