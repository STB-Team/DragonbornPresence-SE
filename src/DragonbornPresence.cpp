#include "DragonbornPresence.h"
#include "DragonbornPresence/core/TextUtils.h"
#include "DragonbornPresence/core/Difficulty.h"
#include "DragonbornPresence/core/RefreshReason.h"
#include "DragonbornPresence/core/PlayerSnapshot.h"
#include "DragonbornPresence/core/PresencePayload.h"
#include "DragonbornPresence/core/LocationAssets.h"
#include "DragonbornPresence/core/Config.h"
#include "DragonbornPresence/core/LocationContext.h"
#include "DragonbornPresence/core/LocationAssetResolver.h"
#include "DragonbornPresence/adapters/config/JsonConfigProvider.h"
#include "DragonbornPresence/application/ports/IConfigProvider.h"
#include "DragonbornPresence/application/ports/IGameDataSource.h"
#include "DragonbornPresence/adapters/SkyrimTrueBeliever/StbGameDataSource.h"
#include "DragonbornPresence/application/ports/IPresenceClient.h"
#include "discord_loader.h"
#include "discord.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace DragonbornPresence
{

    namespace
    {

        namespace constants
        {

            constexpr std::chrono::milliseconds kDiscordCallbackInterval{500};
            constexpr std::uint8_t kPresencePollIntervalInCallbackTicks = 1;
            constexpr std::uint32_t kPendingTaskWarningTicks = 1;
            constexpr std::uint32_t kPendingTaskWarningRepeatTicks = 10;
            constexpr std::uint32_t kActivityCallbackTimeoutTicks = 20;
            constexpr std::size_t kDiscordTextMaxBytes = 127;
            constexpr std::string_view kMainMenuName = "Main Menu";
            constexpr std::string_view kLoadingMenuName = "Loading Menu";
            constexpr std::string_view kLoadingText = "Загрузка";
            constexpr std::string_view kUnknownDeathsText = "—";

        } // namespace constants

        namespace integration
        {

            struct DiscordResultDetails
            {
                std::string_view name;
                std::string_view explanation;
            };

            /// Converts every Discord Game SDK result into a stable name and useful explanation.
            [[nodiscard]] constexpr DiscordResultDetails DescribeResult(discord::Result result) noexcept
            {
                using enum discord::Result;
                switch (result)
                {
                case Ok:
                    return {"Ok", "the operation completed successfully"};
                case ServiceUnavailable:
                    return {"ServiceUnavailable", "the Discord service is temporarily unavailable"};
                case InvalidVersion:
                    return {"InvalidVersion", "discord_game_sdk.dll has an incompatible API version"};
                case LockFailed:
                    return {"LockFailed", "the Discord SDK could not acquire an internal lock"};
                case InternalError:
                    return {"InternalError", "the Discord SDK reported an internal failure"};
                case InvalidPayload:
                    return {"InvalidPayload", "the activity payload contains invalid data"};
                case InvalidCommand:
                    return {"InvalidCommand", "Discord rejected an unsupported command"};
                case InvalidPermissions:
                    return {"InvalidPermissions", "Discord denied the required permission"};
                case NotFetched:
                    return {"NotFetched", "the requested Discord data has not been fetched"};
                case NotFound:
                    return {"NotFound", "the requested Discord object was not found"};
                case Conflict:
                    return {"Conflict", "the request conflicts with current Discord state"};
                case InvalidSecret:
                    return {"InvalidSecret", "Discord rejected an invalid activity secret"};
                case InvalidJoinSecret:
                    return {"InvalidJoinSecret", "Discord rejected an invalid join secret"};
                case NoEligibleActivity:
                    return {"NoEligibleActivity", "there is no eligible Discord activity"};
                case InvalidInvite:
                    return {"InvalidInvite", "the Discord invite is invalid"};
                case NotAuthenticated:
                    return {"NotAuthenticated", "Discord has not authenticated this SDK session"};
                case InvalidAccessToken:
                    return {"InvalidAccessToken", "the Discord access token is invalid"};
                case ApplicationMismatch:
                    return {"ApplicationMismatch", "the configured Discord application ID does not match"};
                case InvalidDataUrl:
                    return {"InvalidDataUrl", "Discord received an invalid data URL"};
                case InvalidBase64:
                    return {"InvalidBase64", "Discord received invalid Base64 data"};
                case NotFiltered:
                    return {"NotFiltered", "Discord could not apply the requested filter"};
                case LobbyFull:
                    return {"LobbyFull", "the Discord lobby is full"};
                case InvalidLobbySecret:
                    return {"InvalidLobbySecret", "the Discord lobby secret is invalid"};
                case InvalidFilename:
                    return {"InvalidFilename", "Discord rejected an invalid filename"};
                case InvalidFileSize:
                    return {"InvalidFileSize", "Discord rejected an invalid file size"};
                case InvalidEntitlement:
                    return {"InvalidEntitlement", "the Discord entitlement is invalid"};
                case NotInstalled:
                    return {"NotInstalled", "Discord Desktop is not installed"};
                case NotRunning:
                    return {"NotRunning", "Discord Desktop is not running"};
                case InsufficientBuffer:
                    return {"InsufficientBuffer", "the SDK output buffer is too small"};
                case PurchaseCanceled:
                    return {"PurchaseCanceled", "the Discord purchase was canceled"};
                case InvalidGuild:
                    return {"InvalidGuild", "the Discord guild identifier is invalid"};
                case InvalidEvent:
                    return {"InvalidEvent", "the Discord event is invalid"};
                case InvalidChannel:
                    return {"InvalidChannel", "the Discord channel identifier is invalid"};
                case InvalidOrigin:
                    return {"InvalidOrigin", "Discord rejected the request origin"};
                case RateLimited:
                    return {"RateLimited", "Discord is rate-limiting requests; retrying was disabled"};
                case OAuth2Error:
                    return {"OAuth2Error", "Discord OAuth2 authentication failed"};
                case SelectChannelTimeout:
                    return {"SelectChannelTimeout", "Discord channel selection timed out"};
                case GetGuildTimeout:
                    return {"GetGuildTimeout", "Discord guild retrieval timed out"};
                case SelectVoiceForceRequired:
                    return {"SelectVoiceForceRequired", "Discord requires forced voice selection"};
                case CaptureShortcutAlreadyListening:
                    return {"CaptureShortcutAlreadyListening", "Discord is already capturing a shortcut"};
                case UnauthorizedForAchievement:
                    return {"UnauthorizedForAchievement", "the application cannot modify achievements"};
                case InvalidGiftCode:
                    return {"InvalidGiftCode", "the Discord gift code is invalid"};
                case PurchaseError:
                    return {"PurchaseError", "Discord could not complete the purchase"};
                case TransactionAborted:
                    return {"TransactionAborted", "Discord aborted the transaction"};
                case DrawingInitFailed:
                    return {"DrawingInitFailed", "Discord overlay drawing initialization failed"};
                default:
                    return {"UnknownResult", "the SDK returned an undocumented result code"};
                }
            }

            void LogSdkMessage(discord::LogLevel level, const char *message) noexcept
            {
                try
                {
                    const std::string_view text = message ? message : "empty SDK log message";
                    if (level == discord::LogLevel::Error)
                    {
                        SKSE::log::error(
                            "Discord SDK reported an error: {} Integration will stop if the "
                            "next SDK operation fails.",
                            text);
                    }
                    else
                    {
                        SKSE::log::warn("Discord SDK warning: {}", text);
                    }
                }
                catch (...)
                {
                }
            }

            /// Owns the Discord Game SDK connection and suppresses duplicate activities.
            class DiscordPresenceClient final
                : public ::DragonbornPresence::application::ports::IPresenceClient
            {
            public:
                /// Creates the Discord SDK core when presence is enabled and available.
                [[nodiscard]] bool Initialize(const core::Config &config) override
                {
                    transportHealthy_ = false;
                    if (!config.enabled)
                    {
                        SKSE::log::info(
                            "Discord presence is disabled by configuration; the SDK was not loaded.");
                        return false;
                    }

                    std::string failureReason;
                    if (!DragonbornPresence::detail::IsDiscordRunning(&failureReason))
                    {
                        LogFailure(
                            "Discord availability check",
                            failureReason.empty() ? "Discord Desktop is unavailable."
                                                  : failureReason);
                        return false;
                    }
                    if (!DragonbornPresence::detail::LoadDiscordSdk(&failureReason))
                    {
                        LogFailure(
                            "Discord SDK load",
                            failureReason.empty() ? "discord_game_sdk.dll could not be loaded."
                                                  : failureReason);
                        return false;
                    }

                    discord::Core *createdCore = nullptr;
                    discord::Result result = discord::Result::InternalError;
                    try
                    {
                        result = discord::Core::Create(
                            static_cast<discord::ClientId>(
                                config.applicationId),
                            DiscordCreateFlags_NoRequireDiscord,
                            &createdCore);
                    }
                    catch (const std::exception &error)
                    {
                        delete createdCore;
                        LogFailure("Discord Core::Create exception", error.what());
                        return false;
                    }
                    catch (...)
                    {
                        delete createdCore;
                        LogFailure(
                            "Discord Core::Create exception",
                            "an unknown C++ exception escaped from the SDK wrapper");
                        return false;
                    }

                    std::unique_ptr<discord::Core> candidate(createdCore);
                    if (result != discord::Result::Ok || !candidate)
                    {
                        LogResultFailure("Core::Create", result);
                        return false;
                    }

                    core_ = std::move(candidate);
                    transportHealthy_ = true;
                    try
                    {
                        core_->SetLogHook(discord::LogLevel::Warn, LogSdkMessage);
                    }
                    catch (const std::exception &error)
                    {
                        Disable(std::format(
                            "installing the Discord SDK log hook raised an exception: {}",
                            error.what()));
                        return false;
                    }
                    catch (...)
                    {
                        Disable(
                            "installing the Discord SDK log hook raised an unknown exception");
                        return false;
                    }

                    SKSE::log::info(
                        "Discord Game SDK initialized for application {}; session_start={}.",
                        config.applicationId,
                        sessionStartTimestamp_);
                    return true;
                }

                /// Runs pending callbacks and disables the integration on any transport failure.
                [[nodiscard]] bool RunCallbacks() override
                {
                    if (!IsActive())
                        return false;

                    std::string failureReason;
                    if (!DragonbornPresence::detail::IsDiscordRunning(&failureReason))
                    {
                        Disable(
                            failureReason.empty() ? "Discord Desktop stopped responding or exited"
                                                  : failureReason);
                        return false;
                    }

                    discord::Result result = discord::Result::InternalError;
                    try
                    {
                        result = core_->RunCallbacks();
                    }
                    catch (const std::exception &error)
                    {
                        Disable(std::format(
                            "Core::RunCallbacks raised a C++ exception: {}", error.what()));
                        return false;
                    }
                    catch (...)
                    {
                        Disable("Core::RunCallbacks raised an unknown exception");
                        return false;
                    }

                    if (result != discord::Result::Ok)
                    {
                        DisableForResult("RunCallbacks", result);
                        return false;
                    }
                    if (!transportHealthy_)
                    {
                        Disable(
                            "an asynchronous UpdateActivity callback failed; its decoded "
                            "Discord result is logged immediately above");
                        return false;
                    }

                    if (pendingActivitySignature_.empty())
                    {
                        pendingActivityCallbackTicks_ = 0;
                    }
                    else if (++pendingActivityCallbackTicks_ >=
                             constants::kActivityCallbackTimeoutTicks)
                    {
                        Disable(std::format(
                            "UpdateActivity did not complete within {} ms; the Discord SDK "
                            "callback is considered hung",
                            constants::kActivityCallbackTimeoutTicks *
                                constants::kDiscordCallbackInterval.count()));
                        return false;
                    }
                    return true;
                }

                [[nodiscard]] bool IsActive() const noexcept override
                {
                    return core_ && transportHealthy_;
                }

                /// Submits at most one activity update at a time.
                ///
                /// Returns true when a new Discord update was queued.
                bool UpdateActivity(const core::PresencePayload &payload) override
                {
                    if (!IsActive() || !pendingActivitySignature_.empty())
                        return false;

                    try
                    {
                        const std::string detailsText = core::LimitUtf8Bytes(payload.details, constants::kDiscordTextMaxBytes);
                        const std::string stateText = core::LimitUtf8Bytes(payload.state, constants::kDiscordTextMaxBytes);
                        const std::string largeHoverText =
                            core::LimitUtf8Bytes(payload.largeText, constants::kDiscordTextMaxBytes);
                        const std::string smallHoverText =
                            core::LimitUtf8Bytes(payload.smallText, constants::kDiscordTextMaxBytes);
                        const std::array activityFields{
                            std::string_view(detailsText),
                            std::string_view(stateText),
                            payload.largeImage,
                            std::string_view(largeHoverText),
                            payload.smallImage,
                            std::string_view(smallHoverText),
                        };

                        std::size_t signatureSize = activityFields.size();
                        for (const auto field : activityFields)
                            signatureSize += field.size();

                        std::string signature;
                        signature.reserve(signatureSize);
                        for (const auto field : activityFields)
                        {
                            signature.append(field);
                            signature.push_back('\0');
                        }
                        if (signature == lastActivitySignature_)
                            return false;

                        discord::Activity activity{};
                        activity.SetType(discord::ActivityType::Playing);
                        activity.GetTimestamps().SetStart(sessionStartTimestamp_);
                        if (!detailsText.empty())
                            activity.SetDetails(detailsText.c_str());
                        if (!stateText.empty())
                            activity.SetState(stateText.c_str());
                        if (!payload.largeImage.empty())
                        {
                            activity.GetAssets().SetLargeImage(payload.largeImage.data());
                        }
                        if (!largeHoverText.empty())
                        {
                            activity.GetAssets().SetLargeText(largeHoverText.c_str());
                        }
                        if (!payload.smallImage.empty())
                        {
                            activity.GetAssets().SetSmallImage(payload.smallImage.data());
                        }
                        if (!smallHoverText.empty())
                        {
                            activity.GetAssets().SetSmallText(smallHoverText.c_str());
                        }

                        pendingActivitySignature_ = signature;
                        pendingActivityCallbackTicks_ = 0;
                        core_->ActivityManager().UpdateActivity(
                            activity,
                            [this, signature = std::move(signature)](
                                discord::Result result) noexcept
                            {
                                pendingActivitySignature_.clear();
                                pendingActivityCallbackTicks_ = 0;
                                try
                                {
                                    if (result == discord::Result::Ok)
                                    {
                                        lastActivitySignature_ = signature;
                                        SKSE::log::info(
                                            "Presence updated; session_start={}.",
                                            sessionStartTimestamp_);
                                    }
                                    else
                                    {
                                        transportHealthy_ = false;
                                        LogResultFailure("UpdateActivity callback", result);
                                    }
                                }
                                catch (const std::exception &error)
                                {
                                    transportHealthy_ = false;
                                    LogFailure(
                                        "UpdateActivity completion handling",
                                        error.what());
                                }
                                catch (...)
                                {
                                    transportHealthy_ = false;
                                    LogFailure(
                                        "UpdateActivity completion handling",
                                        "an unknown C++ exception occurred");
                                }
                            });
                        return true;
                    }
                    catch (const std::exception &error)
                    {
                        Disable(std::format(
                            "building or submitting Discord activity raised an exception: {}",
                            error.what()));
                        return false;
                    }
                    catch (...)
                    {
                        Disable(
                            "building or submitting Discord activity raised an unknown exception");
                        return false;
                    }
                }

                /// Releases the SDK and prevents all future Discord calls.
                void Shutdown(std::string_view reason) noexcept override
                {
                    Disable(reason);
                }

            private:
                static void LogResultFailure(
                    std::string_view operation,
                    discord::Result result) noexcept
                {
                    const auto details = DescribeResult(result);
                    try
                    {
                        SKSE::log::error(
                            "Discord operation '{}' failed: {} (code {}). Explanation: {}. "
                            "The integration is being disabled; Skyrim can continue normally.",
                            operation,
                            details.name,
                            static_cast<int>(result),
                            details.explanation);
                    }
                    catch (...)
                    {
                    }
                }

                static void LogFailure(
                    std::string_view operation,
                    std::string_view explanation) noexcept
                {
                    try
                    {
                        SKSE::log::error(
                            "Discord operation '{}' failed. Explanation: {} The integration "
                            "was disabled; Skyrim can continue normally.",
                            operation,
                            explanation);
                    }
                    catch (...)
                    {
                    }
                }

                void DisableForResult(std::string_view operation, discord::Result result) noexcept
                {
                    transportHealthy_ = false;
                    pendingActivitySignature_.clear();
                    pendingActivityCallbackTicks_ = 0;
                    LogResultFailure(operation, result);
                    core_.reset();
                }

                /// Permanently disconnects this client without calling a failed SDK again.
                void Disable(std::string_view reason) noexcept
                {
                    const bool hadWork = core_ || transportHealthy_;
                    transportHealthy_ = false;
                    pendingActivitySignature_.clear();
                    pendingActivityCallbackTicks_ = 0;
                    if (hadWork)
                        LogFailure("safe shutdown", reason);
                    core_.reset();
                }

                /// Returns the current Unix time in seconds for Discord elapsed-time display.
                [[nodiscard]] static discord::Timestamp CurrentUnixTimestamp() noexcept
                {
                    return std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                        .count();
                }

                std::unique_ptr<discord::Core> core_;
                bool transportHealthy_ = false;
                const discord::Timestamp sessionStartTimestamp_ = CurrentUnixTimestamp();
                std::uint32_t pendingActivityCallbackTicks_ = 0;
                std::string lastActivitySignature_;
                std::string pendingActivitySignature_;
            };

        } // namespace integration

        namespace runtime
        {

            class PresenceCoordinator;

            /// Adapts Skyrim menu events to the application-level presence coordinator.
            class MenuEventSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
            {
            public:
                /// Binds the sink to the coordinator that owns its lifecycle.
                explicit MenuEventSink(PresenceCoordinator &coordinator) noexcept
                    : coordinator_(coordinator)
                {
                }

                /// Forwards valid menu events and always allows later sinks to run.
                RE::BSEventNotifyControl ProcessEvent(
                    const RE::MenuOpenCloseEvent *event,
                    RE::BSTEventSource<RE::MenuOpenCloseEvent> *) override;

            private:
                PresenceCoordinator &coordinator_;
            };

            /// Adapts Skyrim combat events to the application-level presence coordinator.
            class CombatEventSink final : public RE::BSTEventSink<RE::TESCombatEvent>
            {
            public:
                /// Binds the sink to the coordinator that owns its lifecycle.
                explicit CombatEventSink(PresenceCoordinator &coordinator) noexcept
                    : coordinator_(coordinator)
                {
                }

                /// Forwards valid combat events and always allows later sinks to run.
                RE::BSEventNotifyControl ProcessEvent(
                    const RE::TESCombatEvent *event,
                    RE::BSTEventSource<RE::TESCombatEvent> *) override;

            private:
                PresenceCoordinator &coordinator_;
            };

            /// Coordinates configuration, game data, Discord transport, and Skyrim events.
            class PresenceCoordinator final
            {
            public:
                /// Constructs the coordinator with all external dependencies.
                ///
                /// All dependencies are required and stored as non-owning references. The
                /// composition root must keep their concrete implementations alive for at
                /// least as long as this coordinator.
                PresenceCoordinator(
                    ::DragonbornPresence::application::ports::IConfigProvider &configProvider,
                    ::DragonbornPresence::application::ports::IGameDataSource &gameDataSource,
                    ::DragonbornPresence::application::ports::IPresenceClient &presenceClient) noexcept
                    : configProvider_(configProvider),
                      gameDataSource_(gameDataSource),
                      presenceClient_(presenceClient),
                      menuEventSink_(*this),
                      combatEventSink_(*this)
                {
                }

                /// Stops all plugin work after an exception reaches an external game callback.
                void HandleException(std::string_view context, const char *details) noexcept
                {
                    active_ = false;
                    permanentlyStopped_ = true;
                    callbackThread_.request_stop();
                    try
                    {
                        SKSE::log::critical(
                            "DragonbornPresence exception in '{}': {} The integration was "
                            "stopped; Skyrim can continue normally.",
                            context,
                            details ? details : "unknown exception");
                    }
                    catch (...)
                    {
                    }
                    presenceClient_.Shutdown(
                        "an internal DragonbornPresence exception occurred; see the previous "
                        "critical log entry");
                }

                void HandleUnknownException(std::string_view context) noexcept
                {
                    HandleException(context, "unknown non-standard C++ exception");
                }

                /// Replaces the active configuration with validated file contents or defaults.
                void LoadConfig()
                {
                    config_ = configProvider_.Load();
                }

                /// Initializes integrations, publishes loading state, and registers event sinks.
                void RegisterGameEventHandlers()
                {
                    SKSE::log::info("Registering game event handlers...");
                    if (permanentlyStopped_)
                    {
                        SKSE::log::error(
                            "DragonbornPresence registration was skipped because an earlier "
                            "fatal error permanently stopped this plugin instance.");
                        return;
                    }

                    auto *ui = RE::UI::GetSingleton();
                    auto *eventSource = RE::ScriptEventSourceHolder::GetSingleton();
                    taskInterface_ = SKSE::GetTaskInterface();
                    if (!ui || !eventSource || !taskInterface_)
                    {
                        SKSE::log::critical(
                            "DragonbornPresence cannot start: UI={}, ScriptEventSourceHolder={}, "
                            "SKSE TaskInterface={}. At least one required Skyrim/SKSE service is "
                            "unavailable. No event sinks or background tasks were registered.",
                            ui != nullptr,
                            eventSource != nullptr,
                            taskInterface_ != nullptr);
                        return;
                    }

                    if (!presenceClient_.Initialize(config_))
                        return;

                    active_ = true;
                    gameDataSource_.Initialize();
                    SendLoadingPresence();
                    if (!active_)
                        return;

                    ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuEventSink_);
                    eventSource->AddEventSink<RE::TESCombatEvent>(&combatEventSink_);
                    StartCallbackThread();
                }

                /// Marks the game ready and immediately publishes the complete player state.
                void OnGameLoaded()
                {
                    if (!active_)
                        return;
                    SKSE::log::info("Game loaded — refreshing STB presence data.");
                    gameLoaded_ = true;
                    loading_ = false;
                    RefreshPresence(core::RefreshReason::kGameLoaded);
                }

                /// Applies menu transitions to the loading and game-ready state machine.
                void HandleMenuEvent(const RE::MenuOpenCloseEvent &event)
                {
                    if (!active_)
                        return;
                    if (event.menuName == constants::kMainMenuName && event.opening)
                    {
                        gameLoaded_ = false;
                        SetLoading(true);
                    }
                    else if (event.menuName == constants::kLoadingMenuName)
                    {
                        if (event.opening)
                        {
                            SetLoading(true);
                        }
                        else if (gameLoaded_)
                        {
                            SetLoading(false);
                        }
                    }
                }

                /// Coalesces combat changes into the next 500-millisecond polling task.
                void HandleCombatEvent(const RE::TESCombatEvent &event)
                {
                    if (!active_ || !gameLoaded_ || loading_)
                        return;

                    const bool involvesPlayer =
                        (event.actor && event.actor->IsPlayerRef()) ||
                        (event.targetActor && event.targetActor->IsPlayerRef());
                    const bool mayEndCombat =
                        event.newState.get() == RE::ACTOR_COMBAT_STATE::kNone && lastCombatState_;
                    if (involvesPlayer || mayEndCombat)
                    {
                        combatRefreshRequested_ = true;
                    }
                }

            private:
                /// Stops all future Presence work after a Discord failure.
                void Stop() noexcept
                {
                    active_ = false;
                    permanentlyStopped_ = true;
                    callbackThread_.request_stop();
                }

                /// Sends the stable loading activity used before a playable save is ready.
                void SendLoadingPresence()
                {
                    presenceClient_.UpdateActivity({
                        {},
                        constants::kLoadingText,
                        config_.largeImage,
                        config_.largeText,
                        config_.loadingImage,
                        constants::kLoadingText,
                    });
                    if (!presenceClient_.IsActive())
                        Stop();
                }

                /// Builds and submits one complete activity from the latest player snapshot.
                void RefreshPresence(core::RefreshReason reason)
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
                                                       : std::string(constants::kUnknownDeathsText);
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

                    SKSE::log::info(
                        "[{}] level={} deaths={} stone='{}' difficulty='{}' location='{}' "
                        "large='{}' combat='{}'.",
                        core::ToLogLabel(reason),
                        snapshot.level,
                        deathsText,
                        snapshot.stone,
                        snapshot.difficulty,
                        snapshot.location.displayName,
                        largeAsset.image,
                        snapshot.combatText);
                }

                /// Updates the loading state and publishes the corresponding presence.
                void SetLoading(bool isLoading)
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

                /// Starts the detached Discord callback and 500-millisecond polling loop once.
                void StartCallbackThread()
                {
                    if (callbackThread_.joinable())
                        return;

                    callbackThread_ = std::jthread([this](std::stop_token stopToken) noexcept
                                                   {
            try {
                while (!stopToken.stop_requested()) {
                    std::this_thread::sleep_for(constants::kDiscordCallbackInterval);
                    if (stopToken.stop_requested()) break;

                    if (callbackTaskPending_.exchange(true)) {
                        const auto pendingTicks =
                            callbackTaskPendingTicks_.fetch_add(1) + 1;
                        const bool firstWarning =
                            pendingTicks == constants::kPendingTaskWarningTicks;
                        const bool repeatedWarning =
                            pendingTicks > constants::kPendingTaskWarningTicks &&
                            (pendingTicks - constants::kPendingTaskWarningTicks) %
                                    constants::kPendingTaskWarningRepeatTicks ==
                                0;
                        if (firstWarning || repeatedWarning) {
                            SKSE::log::warn(
                                "Discord task 'RunCallbacks/RefreshPresence' has "
                                "been waiting on Skyrim's main thread for about "
                                "{} ms; {} callback request{} coalesced. Only one "
                                "task is queued, so memory use remains bounded.",
                                pendingTicks *
                                    constants::kDiscordCallbackInterval.count(),
                                pendingTicks,
                                pendingTicks == 1 ? "" : "s");
                        }
                        continue;
                    }
                    callbackTaskPendingTicks_ = 0;

                    if (!taskInterface_) {
                        HandleBackgroundException(
                            "SKSE TaskInterface became unavailable");
                        return;
                    }
                    taskInterface_->AddTask([this]() noexcept {
                        callbackTaskPendingTicks_ = 0;
                        callbackTaskPending_ = false;
                        if (!active_) return;

                        try {
                            if (!presenceClient_.RunCallbacks())
                            {
                                Stop();
                                return;
                            }
                            if (++presencePollTicks_ >=
                                constants::kPresencePollIntervalInCallbackTicks) {
                                presencePollTicks_ = 0;
                                if (gameLoaded_ && !loading_) {
                                    const auto reason =
                                        combatRefreshRequested_.exchange(false)
                                        ? core::RefreshReason::kCombat
                                        : core::RefreshReason::kPoll;
                                    RefreshPresence(reason);
                                } else {
                                    // Retry the loading activity after an earlier
                                    // in-flight update completes, without queuing
                                    // more than one Discord callback.
                                    SendLoadingPresence();
                                }
                            }
                        } catch (const std::exception& error) {
                            HandleException(
                                "Discord main-thread task",
                                error.what());
                        } catch (...) {
                            HandleUnknownException("Discord main-thread task");
                        }
                    });
                }
            } catch (const std::exception& error) {
                HandleBackgroundException(error.what());
            } catch (...) {
                HandleBackgroundException(
                    "unknown exception in the scheduler thread");
            } });
                }

                /// Stops producer work without destroying the SDK from the background thread.
                void HandleBackgroundException(const char *details) noexcept
                {
                    active_ = false;
                    callbackThread_.request_stop();
                    permanentlyStopped_ = true;
                    try
                    {
                        SKSE::log::critical(
                            "DragonbornPresence scheduler stopped: {} The Discord SDK core "
                            "will remain idle and be released after the scheduler thread joins; "
                            "Skyrim can continue normally.",
                            details ? details : "unknown scheduler error");
                    }
                    catch (...)
                    {
                    }
                }
                /// Required configuration dependency owned by the composition root.
                ::DragonbornPresence::application::ports::IConfigProvider &configProvider_;

                /// Required game-data dependency owned by the composition root.
                ///
                /// The interface prevents coordinator code from accessing RE::* objects and
                /// exposes only core-owned snapshots.
                ::DragonbornPresence::application::ports::IGameDataSource &gameDataSource_;

                /// Required Presence transport owned by the composition root.
                ///
                /// The application coordinator publishes core payloads without depending on
                /// Discord SDK types or the concrete transport implementation.
                ::DragonbornPresence::application::ports::IPresenceClient &presenceClient_;
                core::Config config_;
                MenuEventSink menuEventSink_;
                CombatEventSink combatEventSink_;
                const SKSE::TaskInterface *taskInterface_ = nullptr;
                std::atomic<bool> callbackTaskPending_{false};
                std::atomic<std::uint32_t> callbackTaskPendingTicks_{0};
                std::atomic<bool> combatRefreshRequested_{false};
                std::atomic<bool> active_{false};
                std::atomic<bool> permanentlyStopped_{false};
                std::uint8_t presencePollTicks_ = 0;
                bool loading_ = true;
                bool gameLoaded_ = false;
                bool lastCombatState_ = false;
                std::jthread callbackThread_;
            };

            /// Forwards menu events without allowing C++ exceptions to escape into Skyrim.
            RE::BSEventNotifyControl MenuEventSink::ProcessEvent(
                const RE::MenuOpenCloseEvent *event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent> *)
            {
                try
                {
                    if (event)
                        coordinator_.HandleMenuEvent(*event);
                }
                catch (const std::exception &error)
                {
                    coordinator_.HandleException("menu event handler", error.what());
                }
                catch (...)
                {
                    coordinator_.HandleUnknownException("menu event handler");
                }
                return RE::BSEventNotifyControl::kContinue;
            }

            /// Forwards combat events without allowing C++ exceptions to escape into Skyrim.
            RE::BSEventNotifyControl CombatEventSink::ProcessEvent(
                const RE::TESCombatEvent *event,
                RE::BSTEventSource<RE::TESCombatEvent> *)
            {
                try
                {
                    if (event)
                        coordinator_.HandleCombatEvent(*event);
                }
                catch (const std::exception &error)
                {
                    coordinator_.HandleException("combat event handler", error.what());
                }
                catch (...)
                {
                    coordinator_.HandleUnknownException("combat event handler");
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        } // namespace runtime

        /// Selects and owns concrete infrastructure adapters for the plugin lifetime.
        ///
        /// Dependencies are declared before PresenceCoordinator because it stores
        /// non-owning references to them. Destruction occurs in reverse order, so the
        /// coordinator is destroyed before every adapter it references.
        adapters::config::JsonConfigProvider g_configProvider;
        adapters::SkyrimTrueBeliever::StbGameDataSource g_gameDataSource;
        integration::DiscordPresenceClient g_presenceClient;

        runtime::PresenceCoordinator g_presenceCoordinator(
            g_configProvider,
            g_gameDataSource,
            g_presenceClient);

    } // namespace

    /// Loads configuration without allowing errors to escape into SKSE.
    void LoadConfig() noexcept
    {
        try
        {
            g_presenceCoordinator.LoadConfig();
        }
        catch (const std::exception &error)
        {
            g_presenceCoordinator.HandleException("configuration loading", error.what());
        }
        catch (...)
        {
            g_presenceCoordinator.HandleUnknownException("configuration loading");
        }
    }

    /// Initializes integrations without allowing errors to escape into SKSE.
    void RegisterGameEventHandlers() noexcept
    {
        try
        {
            g_presenceCoordinator.RegisterGameEventHandlers();
        }
        catch (const std::exception &error)
        {
            g_presenceCoordinator.HandleException(
                "game event registration",
                error.what());
        }
        catch (...)
        {
            g_presenceCoordinator.HandleUnknownException("game event registration");
        }
    }

    /// Publishes the first state without allowing errors to escape into SKSE.
    void OnGameLoaded() noexcept
    {
        try
        {
            g_presenceCoordinator.OnGameLoaded();
        }
        catch (const std::exception &error)
        {
            g_presenceCoordinator.HandleException("game-load handler", error.what());
        }
        catch (...)
        {
            g_presenceCoordinator.HandleUnknownException("game-load handler");
        }
    }

} // namespace DragonbornPresence
