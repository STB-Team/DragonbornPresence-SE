#include "DragonbornPresence/application/PresenceCoordinator.h"

#include "DragonbornPresence/core/LocationAssetResolver.h"
#include "DragonbornPresence/core/PlayerSnapshot.h"

#include <cstdint>
#include <format>
#include <iterator>
#include <utility>
#include <string>
#include <string_view>

namespace DragonbornPresence::application
{

    namespace
    {
        constexpr std::uint8_t
            kPresencePollIntervalInCallbackTicks = 1;

        constexpr std::string_view kLoadingText =
            "Загрузка";

        constexpr std::string_view kUnknownDeathsText =
            "—";

    } // namespace

    /// Constructs the coordinator with all external dependencies.
    ///
    /// All dependencies are required and stored as non-owning references. The
    /// composition root must keep their concrete implementations alive for at
    /// least as long as this coordinator.
    PresenceCoordinator::PresenceCoordinator(
        ports::IConfigProvider &configProvider,
        ports::IGameDataSource &gameDataSource,
        ports::IPresenceClient &presenceClient,
        ports::ILogger &logger) noexcept
        : configProvider_(configProvider),
          gameDataSource_(gameDataSource),
          presenceClient_(presenceClient),
          logger_(logger)
    {
    }

    /// Stops all plugin work after an exception reaches an external game callback.
    void PresenceCoordinator::HandleException(
        std::string_view context,
        const char *details) noexcept
    {
        active_ = false;
        permanentlyStopped_ = true;

        try
        {
            logger_.Critical(std::format(
                "DragonbornPresence exception in '{}': {} The integration was "
                "stopped; Skyrim can continue normally.",
                context,
                details
                    ? details
                    : "unknown exception"));
        }
        catch (...)
        {
        }

        presenceClient_.Shutdown(
            "an internal DragonbornPresence exception occurred; see the previous "
            "critical log entry");
    }

    void PresenceCoordinator::HandleUnknownException(std::string_view context) noexcept
    {
        HandleException(context, "unknown non-standard C++ exception");
    }

    /// Replaces the active configuration with validated file contents or defaults.
    void PresenceCoordinator::LoadConfig()
    {
        config_ = configProvider_.Load();
    }

    /// Initializes game data and runtime processing independently of Discord.
    bool PresenceCoordinator::Start()
    {
        if (permanentlyStopped_)
        {
            logger_.Error(
                "DragonbornPresence registration was skipped because an earlier "
                "fatal error permanently stopped this plugin instance.");
            return false;
        }

        gameDataSource_.Initialize();
        active_ = true;
        if (config_.enabled)
            (void)StartTransport();
        else
            logger_.Info(
                "Discord presence is disabled in user configuration.");

        return true;
    }

    bool PresenceCoordinator::StartTransport()
    {
        if (!active_ || permanentlyStopped_ || !config_.enabled)
            return false;
        if (presenceClient_.IsActive())
            return true;
        if (!presenceClient_.Initialize())
        {
            logger_.Error(
                "Discord transport is unavailable. Automatic retries are disabled; "
                "change or reload user configuration to try again.");
            return false;
        }

        if (gameLoaded_ && !loading_)
            RefreshPresence(core::RefreshReason::kPoll);
        else
            SendLoadingPresence();
        return presenceClient_.IsActive();
    }

    void PresenceCoordinator::ApplyReloadedConfig(core::Config config)
    {
        config_ = std::move(config);
        if (!config_.enabled)
        {
            if (presenceClient_.IsActive())
            {
                presenceClient_.Shutdown(
                    "disabled by DragonbornPresence user configuration");
            }
            logger_.Info(
                "Discord presence is disabled in user configuration.");
            return;
        }

        if (!presenceClient_.IsActive())
        {
            (void)StartTransport();
            return;
        }

        if (gameLoaded_ && !loading_)
            RefreshPresence(core::RefreshReason::kPoll);
        else
            SendLoadingPresence();
    }

    /// Stops future application work without directly controlling runtime threads.
    void PresenceCoordinator::Stop() noexcept
    {
        active_ = false;
        permanentlyStopped_ = true;
    }

    /// Marks the game ready and immediately publishes the complete player state.
    void PresenceCoordinator::OnGameLoaded()
    {
        if (!active_)
            return;

        logger_.Info(
            "Game loaded — refreshing STB presence data.");

        gameLoaded_ = true;
        loading_ = false;
        RefreshPresence(core::RefreshReason::kGameLoaded);
    }

    /// Marks the current playable game as unavailable.
    void PresenceCoordinator::OnMainMenuOpened()
    {
        if (!active_)
            return;

        gameLoaded_ = false;
        SetLoading(true);
    }

    /// Applies a loading-menu transition without depending on Skyrim event types.
    void PresenceCoordinator::OnLoadingChanged(bool isLoading)
    {
        if (!active_)
            return;

        if (isLoading)
        {
            SetLoading(true);
        }
        else if (gameLoaded_)
        {
            SetLoading(false);
        }
    }

    /// Coalesces an engine-independent combat signal into the next tick.
    void PresenceCoordinator::RequestCombatRefresh(
        bool involvesPlayer,
        bool combatEnded)
    {
        if (!active_ || !gameLoaded_ || loading_)
            return;

        const bool mayEndCombat =
            combatEnded && lastCombatState_;

        if (involvesPlayer || mayEndCombat)
        {
            combatRefreshRequested_ = true;
        }
    }

    /// Processes transport callbacks and publishes the next Presence state.
    ///
    /// Must be invoked on Skyrim's main thread. Returns false after the
    /// application permanently stops and no further ticks should be scheduled.
    bool PresenceCoordinator::Tick()
    {
        if (!active_)
            return false;

        if (auto changedConfig = configProvider_.ReloadIfChanged())
        {
            ApplyReloadedConfig(std::move(*changedConfig));
            return active_;
        }

        if (!config_.enabled || !presenceClient_.IsActive())
            return active_;

        if (!presenceClient_.RunCallbacks())
        {
            logger_.Error(
                "Discord transport session stopped after an error. Automatic retries "
                "are disabled; change or reload user configuration to try again.");
            return active_;
        }

        if (++presencePollTicks_ >=
            kPresencePollIntervalInCallbackTicks)
        {
            presencePollTicks_ = 0;

            if (gameLoaded_ && !loading_)
            {
                const auto reason =
                    combatRefreshRequested_.exchange(false)
                        ? core::RefreshReason::kCombat
                        : core::RefreshReason::kPoll;

                RefreshPresence(reason);
            }
            else
            {
                // Retry the loading activity after an earlier in-flight update
                // completes without queuing multiple callbacks.
                SendLoadingPresence();
            }
        }

        return active_;
    }
    std::string PresenceCoordinator::RenderPresenceTemplate(
        std::string_view presenceTemplate,
        const core::PlayerSnapshot &snapshot,
        std::string_view locationText)
    {
        std::string text;
        text.reserve(presenceTemplate.size() + 32);

        for (std::size_t index = 0; index < presenceTemplate.size();)
        {
            if (presenceTemplate[index] != '{')
            {
                text.push_back(presenceTemplate[index++]);
                continue;
            }

            const std::size_t closingBrace =
                presenceTemplate.find('}', index + 1);
            if (closingBrace == std::string_view::npos)
            {
                text.append(presenceTemplate.substr(index));
                break;
            }

            const std::string_view token = presenceTemplate.substr(
                index + 1,
                closingBrace - index - 1);
            bool recognized = true;
            if (token == "difficulty")
            {
                text.append(snapshot.difficulty);
            }
            else if (token == "lvl")
            {
                std::format_to(
                    std::back_inserter(text),
                    "{}",
                    snapshot.level);
            }
            else if (token == "deaths")
            {
                text.append("💀-");
                if (snapshot.deaths)
                {
                    std::format_to(
                        std::back_inserter(text),
                        "{}",
                        *snapshot.deaths);
                }
                else
                {
                    text.append(kUnknownDeathsText);
                }
            }
            else if (token == "stone")
            {
                text.append(snapshot.stone);
            }
            else if (token == "player")
            {
                text.append(snapshot.playerName);
            }
            else if (token == "god")
            {
                text.append(snapshot.god);
            }
            else if (token == "vampire")
            {
                text.append(snapshot.vampire);
            }
            else if (token == "werewolf")
            {
                text.append(snapshot.werewolf);
            }
            else if (token == "location")
            {
                text.append(
                    locationText.empty()
                        ? std::string_view(snapshot.location.displayName)
                        : locationText);
            }
            else if (token == "combat")
            {
                text.append(snapshot.combatText);
            }
            else
            {
                recognized = false;
            }

            if (recognized)
            {
                index = closingBrace + 1;
            }
            else
            {
                text.push_back(presenceTemplate[index++]);
            }
        }
        return text;
    }


    /// Sends the stable loading activity used before a playable save is ready.
    void PresenceCoordinator::SendLoadingPresence()
    {
        if (!presenceClient_.IsActive())
            return;

        presenceClient_.UpdateActivity({
            {},
            kLoadingText,
            config_.largeImage,
            config_.largeText,
            config_.loadingImage,
            kLoadingText,
        });
    }

    /// Builds and submits one complete activity from the latest player snapshot.
    void PresenceCoordinator::RefreshPresence(core::RefreshReason reason)
    {
        if (!active_ || !presenceClient_.IsActive())
            return;
        if (loading_ || !gameLoaded_)
        {
            SendLoadingPresence();
            return;
        }

        const core::PlayerSnapshot snapshot = gameDataSource_.ReadPlayerSnapshot();
        const std::string detailsText =
            RenderPresenceTemplate(config_.detailsTemplate, snapshot);
        const std::string stateText =
            RenderPresenceTemplate(config_.stateTemplate, snapshot);
        const core::LocationAssetResolver assetResolver(config_);
        const auto largeAsset = assetResolver.Resolve(snapshot.location);
        const std::string largeText =
            RenderPresenceTemplate(
                config_.largeTextTemplate,
                snapshot,
                largeAsset.text);
        const std::string smallText = snapshot.inCombat
                                          ? RenderPresenceTemplate(
                                                config_.combatTextTemplate,
                                                snapshot)
                                          : std::string{};
        const std::string_view smallImage = snapshot.inCombat
                                                ? std::string_view(config_.combatImage)
                                                : std::string_view{};

        lastCombatState_ = snapshot.inCombat;
        const bool activityChanged = presenceClient_.UpdateActivity({
            detailsText,
            stateText,
            largeAsset.image,
            largeText,
            smallImage,
            smallText,
        });
        if (!presenceClient_.IsActive() || !activityChanged)
            return;

        const std::string deathsText = snapshot.deaths
                                           ? std::to_string(*snapshot.deaths)
                                           : std::string(kUnknownDeathsText);

        logger_.Info(std::format(
            "[{}] level={} deaths={} stone='{}' difficulty='{}' location='{}' "
            "large='{}' combat='{}'.",
            core::ToLogLabel(reason),
            snapshot.level,
            deathsText,
            snapshot.stone,
            snapshot.difficulty,
            snapshot.location.displayName,
            largeAsset.image,
            snapshot.combatText));
    }

    /// Updates the loading state and publishes the corresponding presence.
    void PresenceCoordinator::SetLoading(bool isLoading)
    {
        loading_ = isLoading;
        if (isLoading)
        {
            SendLoadingPresence();
        }
        else if (gameLoaded_)
        {
            RefreshPresence(core::RefreshReason::kLoadingFinished);
        }
    }

} // namespace DragonbornPresence::application