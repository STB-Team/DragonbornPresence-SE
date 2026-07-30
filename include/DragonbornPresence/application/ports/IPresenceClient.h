#pragma once

#include "DragonbornPresence/core/Config.h"
#include "DragonbornPresence/core/PresencePayload.h"

#include <string_view>

namespace DragonbornPresence::application::ports
{

    /// Application output port for publishing Presence state.
    ///
    /// The application layer depends on this abstraction instead of Discord,
    /// discord::Core, discord::Activity, SDK callbacks, or DLL loading.
    /// Concrete adapters are responsible for translating PresencePayload into
    /// the format required by an external Presence service.
    class IPresenceClient
    {
    public:
        /// Enables destruction through an interface pointer or reference.
        virtual ~IPresenceClient() = default;

        /// Initializes the external Presence transport.
        ///
        /// Returns true only when the client is ready to accept updates.
        /// Failed initialization must leave the client inactive.
        [[nodiscard]] virtual bool Initialize(
            const core::Config &config) = 0;

        /// Processes callbacks required by the external Presence transport.
        ///
        /// Returns false when the transport is unavailable or has failed
        /// permanently and no further operations should be submitted.
        [[nodiscard]] virtual bool RunCallbacks() = 0;

        /// Reports whether the transport can currently accept work.
        [[nodiscard]] virtual bool IsActive() const noexcept = 0;

        /// Publishes a new Presence payload when it differs from the last one.
        ///
        /// Returns true when a new update was submitted. The string views inside
        /// PresencePayload are valid only for the duration of this call and must
        /// not be retained by the implementation.
        virtual bool UpdateActivity(
            const core::PresencePayload &payload) = 0;

        /// Permanently stops the transport and releases its resources.
        ///
        /// Implementations must not allow exceptions to escape during shutdown.
        virtual void Shutdown(
            std::string_view reason) noexcept = 0;
    };

} // namespace DragonbornPresence::application::ports