#pragma once

#include "DragonbornPresence/core/Config.h"
#include "DragonbornPresence/core/LocationAssets.h"
#include "DragonbornPresence/core/LocationContext.h"

namespace DragonbornPresence::core
{

    class LocationAssetResolver final
    {
    public:
        explicit LocationAssetResolver(
            const Config &config) noexcept;

        [[nodiscard]] LargeAssetSelection Resolve(
            const LocationContext &location) const;

    private:
        [[nodiscard]] static bool RuleMatches(
            const LocationImageRule &rule,
            const LocationContext &location);

        const Config &config_;
    };

} // namespace DragonbornPresence::core