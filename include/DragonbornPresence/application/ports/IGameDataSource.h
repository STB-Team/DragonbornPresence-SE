#pragma once

#include "DragonbornPresence/core/PlayerSnapshot.h"

namespace DragonbornPresence::application::ports
{

    /// Application port for reading the game state required by Discord presence.
    ///
    /// The application layer must not access Skyrim RE::* objects directly. A game
    /// adapter reads temporary engine objects and copies the required values into
    /// a core-owned PlayerSnapshot before returning across this boundary.
    class IGameDataSource
    {
    public:
        /// Enables safe destruction through an interface pointer or reference.
        virtual ~IGameDataSource() = default;

        /// Resolves game forms that can be cached after Skyrim data is available.
        ///
        /// The application calls this once during integration startup. Concrete
        /// adapters retain only non-owning engine pointers whose lifetime remains
        /// controlled by Skyrim.
        virtual void Initialize() = 0;

        /// Creates an owned snapshot of the player state for one presence refresh.
        ///
        /// The returned snapshot must not contain pointers or references to Skyrim
        /// objects. Strings, location identifiers, and combat data are copied before
        /// this method returns.
        [[nodiscard]] virtual core::PlayerSnapshot ReadPlayerSnapshot() = 0;
    };

} // namespace DragonbornPresence::application::ports