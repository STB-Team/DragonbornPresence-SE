#pragma once

#include "DragonbornPresence/application/ports/IPresenceClient.h"

#include "discord.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace DragonbornPresence::adapters::discord
{

    /// Discord Game SDK implementation of the Presence output port.
    ///
    /// The adapter owns the Discord SDK core, translates core Presence payloads
    /// into Discord activities, processes asynchronous SDK callbacks, suppresses
    /// duplicate updates, and permanently disables itself after transport errors.
    class DiscordPresenceClient final
        : public ::DragonbornPresence::application::ports::IPresenceClient
    {
    public:
        /// Starts waiting for Discord Desktop when Presence is enabled.
        ///
        /// A background thread checks every few seconds whether Discord runs;
        /// the SDK core is created later by RunCallbacks on the main thread.
        /// Returns false only when Presence is disabled or cannot start.
        [[nodiscard]] bool Initialize(
            const core::Config &config) override;

        /// Connects after Discord was found, or processes pending SDK callbacks.
        ///
        /// Discord exiting returns the client to waiting. Returns false only
        /// after an unrecoverable SDK error, exception, or callback timeout.
        [[nodiscard]] bool RunCallbacks() override;

        /// Reports whether the client is still usable, connected or waiting.
        [[nodiscard]] bool IsActive() const noexcept override;

        /// Submits a new Discord activity when its payload differs from the last
        /// successfully published activity.
        ///
        /// Returns true only when a new asynchronous update was queued.
        bool UpdateActivity(
            const core::PresencePayload &payload) override;

        /// Releases the Discord SDK core and prevents future transport calls.
        void Shutdown(
            std::string_view reason) noexcept override;

    private:
        /// Logs a decoded Discord SDK result without allowing exceptions to escape.
        static void LogResultFailure(
            std::string_view operation,
            ::discord::Result result) noexcept;

        /// Logs a transport failure without allowing exceptions to escape.
        static void LogFailure(
            std::string_view operation,
            std::string_view explanation) noexcept;

        /// Starts the background thread that looks for a running Discord Desktop.
        void StartAvailabilityWatcher();

        /// Creates the SDK core on Skyrim's main thread after Discord was found.
        void Connect() noexcept;

        /// Releases a core whose Discord client exited and waits for Discord again.
        void DropConnection() noexcept;

        /// Disables the transport after a Discord SDK result failure.
        void DisableForResult(
            std::string_view operation,
            ::discord::Result result) noexcept;

        /// Permanently disconnects the client after an internal transport failure.
        void Disable(
            std::string_view reason) noexcept;

        /// Returns the current Unix time in seconds for Discord elapsed-time display.
        [[nodiscard]] static ::discord::Timestamp
        CurrentUnixTimestamp() noexcept;

        std::unique_ptr<::discord::Core> core_;
        bool transportHealthy_ = false;
        bool permanentlyDisabled_ = true;
        bool activityDiscordExited_ = false;
        core::ApplicationId applicationId_ = core::kDefaultApplicationId;
        std::uint32_t failedConnectAttempts_ = 0;
        const ::discord::Timestamp sessionStartTimestamp_ =
            CurrentUnixTimestamp();
        std::uint32_t pendingActivityCallbackTicks_ = 0;
        std::string lastActivitySignature_;
        std::string pendingActivitySignature_;

        // Shared with the availability watcher thread.
        std::atomic<bool> waitingForDiscord_{false};
        std::atomic<bool> discordDetected_{false};

        // Declared last: stops and joins before the state it reads is destroyed.
        std::jthread availabilityThread_;
    };

} // namespace DragonbornPresence::adapters::discord