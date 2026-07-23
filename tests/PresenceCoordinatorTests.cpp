#include "DragonbornPresence/application/PresenceCoordinator.h"

#include "DragonbornPresence/application/ports/IConfigProvider.h"
#include "DragonbornPresence/application/ports/IGameDataSource.h"
#include "DragonbornPresence/application/ports/ILogger.h"
#include "DragonbornPresence/application/ports/IPresenceClient.h"
#include "DragonbornPresence/core/Config.h"
#include "DragonbornPresence/core/PlayerSnapshot.h"
#include "DragonbornPresence/core/PresencePayload.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace application = DragonbornPresence::application;
namespace core = DragonbornPresence::core;
namespace ports = DragonbornPresence::application::ports;

namespace
{

    struct CapturedPayload
    {
        std::string details;
        std::string state;
        std::string largeImage;
        std::string largeText;
        std::string smallImage;
        std::string smallText;
    };

    CapturedPayload Capture(
        const core::PresencePayload &payload)
    {
        return {
            std::string(payload.details),
            std::string(payload.state),
            std::string(payload.largeImage),
            std::string(payload.largeText),
            std::string(payload.smallImage),
            std::string(payload.smallText),
        };
    }

    bool ContainsMessage(
        const std::vector<std::string> &messages,
        std::string_view fragment)
    {
        for (const auto &message : messages)
        {
            if (message.find(fragment) != std::string::npos)
                return true;
        }

        return false;
    }

    class FakeConfigProvider final
        : public ports::IConfigProvider
    {
    public:
        core::Config Load() override
        {
            ++loadCalls;
            return config;
        }

        std::optional<core::Config> ReloadIfChanged() override
        {
            ++reloadCalls;
            auto result = std::move(pendingReload);
            pendingReload.reset();
            return result;
        }

        core::Config config;
        std::optional<core::Config> pendingReload;
        int loadCalls = 0;
        int reloadCalls = 0;
    };

    class FakeGameDataSource final
        : public ports::IGameDataSource
    {
    public:
        void Initialize() override
        {
            ++initializeCalls;
        }

        core::PlayerSnapshot ReadPlayerSnapshot() override
        {
            ++readCalls;
            return snapshot;
        }

        core::PlayerSnapshot snapshot;
        int initializeCalls = 0;
        int readCalls = 0;
    };

    class FakePresenceClient final
        : public ports::IPresenceClient
    {
    public:
        bool Initialize() override
        {
            ++initializeCalls;
            active = initializeResult;
            return initializeResult;
        }

        bool RunCallbacks() override
        {
            ++callbackCalls;
            if (!callbacksResult)
                active = false;
            return callbacksResult;
        }

        bool IsActive() const noexcept override
        {
            return active;
        }

        bool UpdateActivity(
            const core::PresencePayload &payload) override
        {
            payloads.push_back(Capture(payload));

            if (deactivateOnUpdate)
                active = false;

            return updateResult;
        }

        void Shutdown(
            std::string_view reason) noexcept override
        {
            shutdownReasons.emplace_back(reason);
            active = false;
        }

        std::vector<CapturedPayload> payloads;
        std::vector<std::string> shutdownReasons;
        bool initializeResult = true;
        bool callbacksResult = true;
        bool updateResult = true;
        bool deactivateOnUpdate = false;
        bool active = false;
        int initializeCalls = 0;
        int callbackCalls = 0;
    };

    class FakeLogger final
        : public ports::ILogger
    {
    public:
        void Info(std::string_view message) noexcept override
        {
            infos.emplace_back(message);
        }

        void Warning(std::string_view message) noexcept override
        {
            warnings.emplace_back(message);
        }

        void Error(std::string_view message) noexcept override
        {
            errors.emplace_back(message);
        }

        void Critical(std::string_view message) noexcept override
        {
            criticals.emplace_back(message);
        }

        std::vector<std::string> infos;
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
        std::vector<std::string> criticals;
    };

    struct Harness
    {
        FakeConfigProvider configProvider;
        FakeGameDataSource gameDataSource;
        FakePresenceClient presenceClient;
        FakeLogger logger;
        application::PresenceCoordinator coordinator{
            configProvider,
            gameDataSource,
            presenceClient,
            logger,
        };
    };

    core::PlayerSnapshot MakeSnapshot()
    {
        core::PlayerSnapshot snapshot;
        snapshot.level = 42;
        snapshot.deaths = 7;
        snapshot.stone = "🌙-Луна";
        snapshot.difficulty = "🔴Героический";
        snapshot.playerName = "Довакин";
        snapshot.god = "Акатош, Азура";
        snapshot.vampire = "Вампир";
        snapshot.location = {
            "Tamriel",
            "WhiterunExterior01",
            {"WhiterunLocation"},
            "Skyrim: Whiterun",
        };
        snapshot.combatText = "В бою с Драугр (ур. 30)";
        return snapshot;
    }

} // namespace

TEST_CASE("Coordinator loads configuration and starts dependencies")
{
    Harness harness;
    harness.configProvider.config.largeImage = "configured_large";
    harness.configProvider.config.largeText = "Configured text";
    harness.configProvider.config.loadingImage = "configured_loading";

    harness.coordinator.LoadConfig();
    const bool started = harness.coordinator.Start();

    CHECK(started);
    CHECK(harness.configProvider.loadCalls == 1);
    CHECK(harness.presenceClient.initializeCalls == 1);
    CHECK(harness.gameDataSource.initializeCalls == 1);
    REQUIRE(harness.presenceClient.payloads.size() == 1);

    const auto &loading = harness.presenceClient.payloads.front();
    CHECK(loading.details.empty());
    CHECK(loading.state == "Загрузка");
    CHECK(loading.largeImage == "configured_large");
    CHECK(loading.largeText == "Configured text");
    CHECK(loading.smallImage == "configured_loading");
    CHECK(loading.smallText == "Загрузка");
}

TEST_CASE("Transport startup failure keeps runtime available for explicit retry")
{
    Harness harness;
    harness.presenceClient.initializeResult = false;
    harness.coordinator.LoadConfig();

    CHECK(harness.coordinator.Start());
    CHECK(harness.presenceClient.initializeCalls == 1);
    CHECK(harness.gameDataSource.initializeCalls == 1);
    CHECK(harness.presenceClient.payloads.empty());
    CHECK(harness.coordinator.Tick());
    CHECK(harness.presenceClient.initializeCalls == 1);

    harness.presenceClient.initializeResult = true;
    harness.configProvider.pendingReload = harness.configProvider.config;
    CHECK(harness.coordinator.Tick());
    CHECK(harness.presenceClient.initializeCalls == 2);
    REQUIRE(harness.presenceClient.payloads.size() == 1);
    CHECK(harness.presenceClient.payloads.back().state == "Загрузка");
}

TEST_CASE("Game-loaded refresh builds the complete gameplay payload")
{
    Harness harness;
    harness.configProvider.config.largeImage = "fallback";
    harness.configProvider.config.largeText = "Fallback text";
    harness.configProvider.config.combatImage = "combat_asset";
    harness.configProvider.config.combatTextTemplate = "{combat} / {player}";
    harness.configProvider.config.locationImageRules = {
        {"Tamriel", "WhiterunLocation", "", "whiterun", "whiterun_asset", "Whiterun text"},
    };
    harness.gameDataSource.snapshot = MakeSnapshot();
    harness.gameDataSource.snapshot.inCombat = true;

    harness.coordinator.LoadConfig();
    REQUIRE(harness.coordinator.Start());
    harness.coordinator.OnGameLoaded();

    CHECK(harness.gameDataSource.readCalls == 1);
    REQUIRE(harness.presenceClient.payloads.size() == 2);

    const auto &gameplay = harness.presenceClient.payloads.back();
    CHECK(gameplay.details == "🔴Героический");
    CHECK(gameplay.state == "lvl-42 💀-7 🌙-Луна");
    CHECK(gameplay.largeImage == "whiterun_asset");
    CHECK(gameplay.largeText == "Довакин");
    CHECK(gameplay.smallImage == "combat_asset");
    CHECK(gameplay.smallText == "В бою с Драугр (ур. 30) / Довакин");
    CHECK(ContainsMessage(harness.logger.infos, "[game-loaded]"));
}

TEST_CASE("Unknown deaths and inactive combat use stable fallbacks")
{
    Harness harness;
    harness.gameDataSource.snapshot = MakeSnapshot();
    harness.gameDataSource.snapshot.deaths = std::nullopt;
    harness.gameDataSource.snapshot.inCombat = false;

    harness.coordinator.LoadConfig();
    REQUIRE(harness.coordinator.Start());
    harness.coordinator.OnGameLoaded();

    const auto &gameplay = harness.presenceClient.payloads.back();
    CHECK(gameplay.state == "lvl-42 💀-— 🌙-Луна");
    CHECK(gameplay.smallImage.empty());
    CHECK(gameplay.smallText.empty());
}

TEST_CASE("Runtime configuration enables and disables Discord without restart")
{
    Harness harness;
    harness.configProvider.config.enabled = false;
    harness.coordinator.LoadConfig();

    REQUIRE(harness.coordinator.Start());
    CHECK(harness.gameDataSource.initializeCalls == 1);
    CHECK(harness.presenceClient.initializeCalls == 0);
    CHECK(harness.presenceClient.payloads.empty());

    harness.configProvider.config.enabled = true;
    harness.configProvider.pendingReload = harness.configProvider.config;
    REQUIRE(harness.coordinator.Tick());
    CHECK(harness.presenceClient.initializeCalls == 1);
    REQUIRE(harness.presenceClient.payloads.size() == 1);

    harness.configProvider.config.enabled = false;
    harness.configProvider.pendingReload = harness.configProvider.config;
    REQUIRE(harness.coordinator.Tick());
    REQUIRE(harness.presenceClient.shutdownReasons.size() == 1);
    CHECK_FALSE(harness.presenceClient.active);
}

TEST_CASE("Presence templates replace current payload fields and reload at runtime")
{
    Harness harness;
    harness.configProvider.config.detailsTemplate =
        "{stone} lvl-{lvl} / {stone}";
    harness.configProvider.config.stateTemplate =
        "{difficulty} {deaths} {player} {god} {vampire} {werewolf} "
        "{location} {combat} {unknown}";
    harness.gameDataSource.snapshot = MakeSnapshot();

    harness.coordinator.LoadConfig();
    REQUIRE(harness.coordinator.Start());
    harness.coordinator.OnGameLoaded();

    const auto &gameplay = harness.presenceClient.payloads.back();
    CHECK(gameplay.details == "🌙-Луна lvl-42 / 🌙-Луна");
    CHECK(
        gameplay.state ==
        "🔴Героический 💀-7 Довакин Акатош, Азура Вампир  "
        "Skyrim: Whiterun В бою с Драугр (ур. 30) {unknown}");

    harness.configProvider.config.detailsTemplate = "Уровень {lvl}";
    harness.configProvider.config.stateTemplate = "";
    harness.configProvider.pendingReload = harness.configProvider.config;
    REQUIRE(harness.coordinator.Tick());

    CHECK(harness.presenceClient.payloads.back().details == "Уровень 42");
    CHECK(harness.presenceClient.payloads.back().state.empty());

    harness.gameDataSource.snapshot.god.clear();
    harness.gameDataSource.snapshot.vampire.clear();
    harness.gameDataSource.snapshot.werewolf.clear();
    harness.configProvider.config.detailsTemplate = "{god}{vampire}{werewolf}";
    harness.configProvider.pendingReload = harness.configProvider.config;
    REQUIRE(harness.coordinator.Tick());
    CHECK(harness.presenceClient.payloads.back().details.empty());
}

TEST_CASE("Loading and main-menu transitions publish only valid states")
{
    Harness harness;
    harness.gameDataSource.snapshot = MakeSnapshot();

    harness.coordinator.LoadConfig();
    REQUIRE(harness.coordinator.Start());
    harness.coordinator.OnGameLoaded();
    REQUIRE(harness.presenceClient.payloads.size() == 2);

    harness.coordinator.OnLoadingChanged(true);
    REQUIRE(harness.presenceClient.payloads.size() == 3);
    CHECK(harness.presenceClient.payloads.back().state == "Загрузка");

    harness.coordinator.OnLoadingChanged(false);
    REQUIRE(harness.presenceClient.payloads.size() == 4);
    CHECK(harness.gameDataSource.readCalls == 2);

    harness.coordinator.OnMainMenuOpened();
    REQUIRE(harness.presenceClient.payloads.size() == 5);
    CHECK(harness.presenceClient.payloads.back().state == "Загрузка");

    harness.coordinator.OnLoadingChanged(false);
    CHECK(harness.presenceClient.payloads.size() == 5);
    CHECK(harness.gameDataSource.readCalls == 2);
}

TEST_CASE("Callback failure pauses transport until explicit configuration reload")
{
    Harness harness;
    harness.coordinator.LoadConfig();
    REQUIRE(harness.coordinator.Start());
    harness.presenceClient.callbacksResult = false;

    CHECK(harness.coordinator.Tick());
    CHECK(harness.presenceClient.callbackCalls == 1);
    CHECK(harness.coordinator.Tick());
    CHECK(harness.presenceClient.callbackCalls == 1);
    CHECK(harness.presenceClient.initializeCalls == 1);

    harness.presenceClient.callbacksResult = true;
    harness.configProvider.pendingReload = harness.configProvider.config;
    CHECK(harness.coordinator.Tick());
    CHECK(harness.presenceClient.initializeCalls == 2);
    CHECK_FALSE(harness.logger.errors.empty());
}

TEST_CASE("Combat refresh requests are coalesced into refresh reasons")
{
    Harness harness;
    harness.gameDataSource.snapshot = MakeSnapshot();
    harness.gameDataSource.snapshot.inCombat = true;

    harness.coordinator.LoadConfig();
    REQUIRE(harness.coordinator.Start());
    harness.coordinator.OnGameLoaded();

    harness.logger.infos.clear();
    harness.gameDataSource.snapshot.inCombat = false;
    harness.coordinator.RequestCombatRefresh(false, true);
    REQUIRE(harness.coordinator.Tick());
    CHECK(ContainsMessage(harness.logger.infos, "[combat]"));

    harness.logger.infos.clear();
    REQUIRE(harness.coordinator.Tick());
    CHECK(ContainsMessage(harness.logger.infos, "[poll]"));

    harness.logger.infos.clear();
    harness.coordinator.RequestCombatRefresh(true, false);
    REQUIRE(harness.coordinator.Tick());
    CHECK(ContainsMessage(harness.logger.infos, "[combat]"));
}

TEST_CASE("Duplicate presence updates do not produce refresh diagnostics")
{
    Harness harness;
    harness.gameDataSource.snapshot = MakeSnapshot();

    harness.coordinator.LoadConfig();
    REQUIRE(harness.coordinator.Start());
    harness.presenceClient.updateResult = false;
    harness.logger.infos.clear();

    harness.coordinator.OnGameLoaded();

    CHECK(ContainsMessage(harness.logger.infos, "Game loaded"));
    CHECK_FALSE(ContainsMessage(harness.logger.infos, "[game-loaded]"));
}

TEST_CASE("Inactive transport after update waits for explicit retry")
{
    Harness harness;
    harness.presenceClient.deactivateOnUpdate = true;
    harness.coordinator.LoadConfig();

    CHECK(harness.coordinator.Start());
    CHECK(harness.gameDataSource.initializeCalls == 1);
    CHECK(harness.presenceClient.initializeCalls == 1);
    CHECK(harness.coordinator.Tick());
    CHECK(harness.presenceClient.initializeCalls == 1);

    harness.presenceClient.deactivateOnUpdate = false;
    harness.configProvider.pendingReload = harness.configProvider.config;
    CHECK(harness.coordinator.Tick());
    CHECK(harness.presenceClient.initializeCalls == 2);
}

TEST_CASE("Exception handling logs context and shuts down the transport")
{
    Harness harness;
    harness.coordinator.LoadConfig();
    REQUIRE(harness.coordinator.Start());

    harness.coordinator.HandleException(
        "test callback",
        "test failure");

    REQUIRE(harness.logger.criticals.size() == 1);
    CHECK(harness.logger.criticals.front().find("test callback") != std::string::npos);
    CHECK(harness.logger.criticals.front().find("test failure") != std::string::npos);
    REQUIRE(harness.presenceClient.shutdownReasons.size() == 1);
    CHECK_FALSE(harness.coordinator.Tick());
    CHECK_FALSE(harness.coordinator.Start());
}
