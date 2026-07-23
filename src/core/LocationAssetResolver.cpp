#include "DragonbornPresence/core/LocationAssetResolver.h"

#include "DragonbornPresence/core/TextUtils.h"

#include <algorithm>
#include <string_view>

namespace DragonbornPresence::core
{

    LocationAssetResolver::LocationAssetResolver(
        const Config &config) noexcept
        : config_(config)
    {
    }

    LargeAssetSelection LocationAssetResolver::Resolve(
        const LocationContext &location) const
    {
        for (const auto &rule : config_.locationImageRules)
        {
            if (RuleMatches(rule, location))
            {
                return {
                    rule.image,
                    rule.text.empty()
                        ? std::string_view(config_.largeText)
                        : std::string_view(rule.text),
                };
            }
        }

        return {
            config_.largeImage,
            config_.largeText,
        };
    }

    bool LocationAssetResolver::RuleMatches(
        const LocationImageRule &rule,
        const LocationContext &location)
    {
        if (!rule.worldspace.empty() &&
            rule.worldspace != location.worldspaceEditorId)
        {
            return false;
        }

        if (!rule.cell.empty() &&
            rule.cell != location.cellEditorId)
        {
            return false;
        }

        if (!rule.location.empty())
        {
            const auto selectedLocation = std::ranges::find(
                location.locationEditorIds,
                rule.location);

            if (selectedLocation ==
                location.locationEditorIds.end())
            {
                return false;
            }
        }

        return rule.match.empty() ||
               ContainsAsciiInsensitive(
                   location.displayName,
                   rule.match);
    }

} // namespace DragonbornPresence::core