#pragma once

#include "DragonbornPresence/application/ports/IPresenceClient.h"

#include "discord.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

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
        /// Creates the Discord SDK core when Presence is enabled and available.
        ///
        /// Returns true only when the SDK core was created and the transport is
        /// ready to process callbacks and activity updates.
        [[nodiscard]] bool Initialize(
            const core::Config &config) override;

        /// Processes pending Discord SDK callbacks.
        ///
        /// Returns false after a transport error, callback timeout, or Discord
        /// process shutdown.
        [[nodiscard]] bool RunCallbacks() override;

        /// Reports whether the Discord SDK core and transport are healthy.
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
        const ::discord::Timestamp sessionStartTimestamp_ =
            CurrentUnixTimestamp();
        std::uint32_t pendingActivityCallbackTicks_ = 0;
        std::string lastActivitySignature_;
        std::string pendingActivitySignature_;
    };

} // namespace DragonbornPresence::adapters::discord