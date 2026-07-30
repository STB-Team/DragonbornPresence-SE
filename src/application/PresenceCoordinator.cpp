#include "DragonbornPresence/application/PresenceCoordinator.h"

#include "DragonbornPresence/core/LocationAssetResolver.h"
#include "DragonbornPresence/core/PlayerSnapshot.h"

#include <cstdint>
#include <format>
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

    /// Initializes game data and the Presence transport.
    ///
    /// Returns true only when the application is ready to receive game signals
    /// and periodic ticks.
    bool PresenceCoordinator::Start()
    {
        if (permanentlyStopped_)
        {
            logger_.Error(
                "DragonbornPresence registration was skipped because an earlier "
                "fatal error permanently stopped this plugin instance.");

            return false;
        }

        if (!presenceClient_.Initialize(config_))
            return false;

        active_ = true;
        gameDataSource_.Initialize();
        SendLoadingPresence();

        return active_;
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

        if (!presenceClient_.RunCallbacks())
        {
            Stop();
            return false;
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

    /// Sends the stable loading activity used before a playable save is ready.
    void PresenceCoordinator::SendLoadingPresence()
    {
        presenceClient_.UpdateActivity({
            {},
            kLoadingText,
            config_.largeImage,
            config_.largeText,
            config_.loadingImage,
            kLoadingText,
        });
        if (!presenceClient_.IsActive())
            Stop();
    }

    /// Builds and submits one complete activity from the latest player snapshot.
    void PresenceCoordinator::RefreshPresence(core::RefreshReason reason)
    {
        if (!active_)
            return;
        if (loading_ || !gameLoaded_)
        {
            SendLoadingPresence();
            return;
        }

        const core::PlayerSnapshot snapshot = gameDataSource_.ReadPlayerSnapshot();
        const std::string deathsText = snapshot.deaths
                                           ? std::to_string(*snapshot.deaths)
                                           : std::string(kUnknownDeathsText);
        const std::string detailsText = snapshot.difficulty;
        const std::string stateText = std::format(
            "lvl-{} 💀-{} {}",
            snapshot.level,
            deathsText,
            snapshot.stone);
        const std::string_view smallImage = snapshot.inCombat
                                                ? std::string_view(config_.combatImage)
                                                : std::string_view{};
        const std::string_view smallText = snapshot.inCombat
                                               ? std::string_view(snapshot.combatText)
                                               : std::string_view{};
        const core::LocationAssetResolver assetResolver(config_);
        const auto largeAsset = assetResolver.Resolve(snapshot.location);

        lastCombatState_ = snapshot.inCombat;
        const bool activityChanged = presenceClient_.UpdateActivity({
            detailsText,
            stateText,
            largeAsset.image,
            largeAsset.text,
            smallImage,
            smallText,
        });
        if (!presenceClient_.IsActive())
        {
            Stop();
            return;
        }
        if (!activityChanged)
            return;

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