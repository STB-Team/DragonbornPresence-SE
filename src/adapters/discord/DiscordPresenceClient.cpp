#include "DragonbornPresence/adapters/discord/DiscordPresenceClient.h"

#include "DragonbornPresence/core/TextUtils.h"
#include "DragonbornPresence/adapters/discord/DiscordSdkLoader.h"

#include <SKSE/SKSE.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace DragonbornPresence::adapters::discord
{

    namespace
    {

        constexpr ::discord::ClientId kStbDiscordApplicationId =
            1527543892151373937;
        constexpr std::uint32_t kActivityCallbackTimeoutTicks = 20;
        constexpr std::chrono::milliseconds
            kActivityCallbackTimeout{10000};
        constexpr std::size_t kDiscordTextMaxBytes = 127;

        struct DiscordResultDetails
        {
            std::string_view name;
            std::string_view explanation;
        };

        [[nodiscard]] constexpr DiscordResultDetails DescribeResult(::discord::Result result) noexcept
        {
            switch (result)
            {
            case ::discord::Result::Ok:
                return {"Ok", "the operation completed successfully"};
            case ::discord::Result::ServiceUnavailable:
                return {"ServiceUnavailable", "the Discord service is temporarily unavailable"};
            case ::discord::Result::InvalidVersion:
                return {"InvalidVersion", "discord_game_sdk.dll has an incompatible API version"};
            case ::discord::Result::LockFailed:
                return {"LockFailed", "the Discord SDK could not acquire an internal lock"};
            case ::discord::Result::InternalError:
                return {"InternalError", "the Discord SDK reported an internal failure"};
            case ::discord::Result::InvalidPayload:
                return {"InvalidPayload", "the activity payload contains invalid data"};
            case ::discord::Result::InvalidCommand:
                return {"InvalidCommand", "Discord rejected an unsupported command"};
            case ::discord::Result::InvalidPermissions:
                return {"InvalidPermissions", "Discord denied the required permission"};
            case ::discord::Result::NotFetched:
                return {"NotFetched", "the requested Discord data has not been fetched"};
            case ::discord::Result::NotFound:
                return {"NotFound", "the requested Discord object was not found"};
            case ::discord::Result::Conflict:
                return {"Conflict", "the request conflicts with current Discord state"};
            case ::discord::Result::InvalidSecret:
                return {"InvalidSecret", "Discord rejected an invalid activity secret"};
            case ::discord::Result::InvalidJoinSecret:
                return {"InvalidJoinSecret", "Discord rejected an invalid join secret"};
            case ::discord::Result::NoEligibleActivity:
                return {"NoEligibleActivity", "there is no eligible Discord activity"};
            case ::discord::Result::InvalidInvite:
                return {"InvalidInvite", "the Discord invite is invalid"};
            case ::discord::Result::NotAuthenticated:
                return {"NotAuthenticated", "Discord has not authenticated this SDK session"};
            case ::discord::Result::InvalidAccessToken:
                return {"InvalidAccessToken", "the Discord access token is invalid"};
            case ::discord::Result::ApplicationMismatch:
                return {"ApplicationMismatch", "the configured Discord application ID does not match"};
            case ::discord::Result::InvalidDataUrl:
                return {"InvalidDataUrl", "Discord received an invalid data URL"};
            case ::discord::Result::InvalidBase64:
                return {"InvalidBase64", "Discord received invalid Base64 data"};
            case ::discord::Result::NotFiltered:
                return {"NotFiltered", "Discord could not apply the requested filter"};
            case ::discord::Result::LobbyFull:
                return {"LobbyFull", "the Discord lobby is full"};
            case ::discord::Result::InvalidLobbySecret:
                return {"InvalidLobbySecret", "the Discord lobby secret is invalid"};
            case ::discord::Result::InvalidFilename:
                return {"InvalidFilename", "Discord rejected an invalid filename"};
            case ::discord::Result::InvalidFileSize:
                return {"InvalidFileSize", "Discord rejected an invalid file size"};
            case ::discord::Result::InvalidEntitlement:
                return {"InvalidEntitlement", "the Discord entitlement is invalid"};
            case ::discord::Result::NotInstalled:
                return {"NotInstalled", "Discord Desktop is not installed"};
            case ::discord::Result::NotRunning:
                return {"NotRunning", "Discord Desktop is not running"};
            case ::discord::Result::InsufficientBuffer:
                return {"InsufficientBuffer", "the SDK output buffer is too small"};
            case ::discord::Result::PurchaseCanceled:
                return {"PurchaseCanceled", "the Discord purchase was canceled"};
            case ::discord::Result::InvalidGuild:
                return {"InvalidGuild", "the Discord guild identifier is invalid"};
            case ::discord::Result::InvalidEvent:
                return {"InvalidEvent", "the Discord event is invalid"};
            case ::discord::Result::InvalidChannel:
                return {"InvalidChannel", "the Discord channel identifier is invalid"};
            case ::discord::Result::InvalidOrigin:
                return {"InvalidOrigin", "Discord rejected the request origin"};
            case ::discord::Result::RateLimited:
                return {"RateLimited", "Discord is rate-limiting requests; retrying was disabled"};
            case ::discord::Result::OAuth2Error:
                return {"OAuth2Error", "Discord OAuth2 authentication failed"};
            case ::discord::Result::SelectChannelTimeout:
                return {"SelectChannelTimeout", "Discord channel selection timed out"};
            case ::discord::Result::GetGuildTimeout:
                return {"GetGuildTimeout", "Discord guild retrieval timed out"};
            case ::discord::Result::SelectVoiceForceRequired:
                return {"SelectVoiceForceRequired", "Discord requires forced voice selection"};
            case ::discord::Result::CaptureShortcutAlreadyListening:
                return {"CaptureShortcutAlreadyListening", "Discord is already capturing a shortcut"};
            case ::discord::Result::UnauthorizedForAchievement:
                return {"UnauthorizedForAchievement", "the application cannot modify achievements"};
            case ::discord::Result::InvalidGiftCode:
                return {"InvalidGiftCode", "the Discord gift code is invalid"};
            case ::discord::Result::PurchaseError:
                return {"PurchaseError", "Discord could not complete the purchase"};
            case ::discord::Result::TransactionAborted:
                return {"TransactionAborted", "Discord aborted the transaction"};
            case ::discord::Result::DrawingInitFailed:
                return {"DrawingInitFailed", "Discord overlay drawing initialization failed"};
            default:
                return {"UnknownResult", "the SDK returned an undocumented result code"};
            }
        }

        void LogSdkMessage(::discord::LogLevel level, const char *message) noexcept
        {
            try
            {
                const std::string_view text = message ? message : "empty SDK log message";
                if (level == ::discord::LogLevel::Error)
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
    } // namespace

    bool DiscordPresenceClient::Initialize()
    {
        transportHealthy_ = false;
        pendingActivitySignature_.clear();
        pendingActivityCallbackTicks_ = 0;
        core_.reset();

        std::string failureReason;
        if (!IsDiscordRunning(&failureReason))
        {
            LogFailure(
                "Discord availability check",
                failureReason.empty() ? "Discord Desktop is unavailable."
                                      : failureReason);
            return false;
        }
        if (!LoadDiscordSdk(&failureReason))
        {
            LogFailure(
                "Discord SDK load",
                failureReason.empty() ? "discord_game_sdk.dll could not be loaded."
                                      : failureReason);
            return false;
        }

        ::discord::Core *createdCore = nullptr;
        ::discord::Result result = ::discord::Result::InternalError;
        try
        {
            result = ::discord::Core::Create(
                kStbDiscordApplicationId,
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

        std::unique_ptr<::discord::Core> candidate(createdCore);
        if (result != ::discord::Result::Ok || !candidate)
        {
            LogResultFailure("Core::Create", result);
            return false;
        }

        core_ = std::move(candidate);
        transportHealthy_ = true;
        try
        {
            core_->SetLogHook(::discord::LogLevel::Warn, LogSdkMessage);
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
            kStbDiscordApplicationId,
            sessionStartTimestamp_);
        return true;
    }

    bool DiscordPresenceClient::RunCallbacks()
    {
        if (!IsActive())
            return false;

        std::string failureReason;
        if (!IsDiscordRunning(&failureReason))
        {
            Disable(
                failureReason.empty() ? "Discord Desktop stopped responding or exited"
                                      : failureReason);
            return false;
        }

        ::discord::Result result = ::discord::Result::InternalError;
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

        if (result != ::discord::Result::Ok)
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
                 kActivityCallbackTimeoutTicks)
        {
            Disable(std::format(
                "UpdateActivity did not complete within {} ms; the Discord SDK "
                "callback is considered hung",
                kActivityCallbackTimeout.count()));

            return false;
        }
        return true;
    }

    bool DiscordPresenceClient::IsActive() const noexcept
    {
        return core_ && transportHealthy_;
    }

    bool DiscordPresenceClient::UpdateActivity(const core::PresencePayload &payload)
    {
        if (!IsActive() || !pendingActivitySignature_.empty())
            return false;

        try
        {
            const std::string detailsText = core::LimitUtf8Bytes(payload.details, kDiscordTextMaxBytes);
            const std::string stateText = core::LimitUtf8Bytes(payload.state, kDiscordTextMaxBytes);
            const std::string largeHoverText =
                core::LimitUtf8Bytes(payload.largeText, kDiscordTextMaxBytes);
            const std::string smallHoverText =
                core::LimitUtf8Bytes(payload.smallText, kDiscordTextMaxBytes);
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

            ::discord::Activity activity{};
            activity.SetType(::discord::ActivityType::Playing);
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
                    ::discord::Result result) noexcept
                {
                    pendingActivitySignature_.clear();
                    pendingActivityCallbackTicks_ = 0;
                    try
                    {
                        if (result == ::discord::Result::Ok)
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

    void DiscordPresenceClient::Shutdown(std::string_view reason) noexcept
    {
        const bool hadWork = core_ || transportHealthy_;
        transportHealthy_ = false;
        pendingActivitySignature_.clear();
        pendingActivityCallbackTicks_ = 0;
        core_.reset();
        if (!hadWork)
            return;

        try
        {
            SKSE::log::info("Discord transport stopped: {}.", reason);
        }
        catch (...)
        {
        }
    }

    void DiscordPresenceClient::LogResultFailure(
        std::string_view operation,
        ::discord::Result result) noexcept
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

    void DiscordPresenceClient::LogFailure(
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

    void DiscordPresenceClient::DisableForResult(std::string_view operation, ::discord::Result result) noexcept
    {
        transportHealthy_ = false;
        pendingActivitySignature_.clear();
        pendingActivityCallbackTicks_ = 0;
        LogResultFailure(operation, result);
        core_.reset();
    }

    void DiscordPresenceClient::Disable(std::string_view reason) noexcept
    {
        const bool hadWork = core_ || transportHealthy_;
        transportHealthy_ = false;
        pendingActivitySignature_.clear();
        pendingActivityCallbackTicks_ = 0;
        if (hadWork)
            LogFailure("safe shutdown", reason);
        core_.reset();
    }

    ::discord::Timestamp DiscordPresenceClient::CurrentUnixTimestamp() noexcept
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
} // namespace DragonbornPresence::adapters::discord