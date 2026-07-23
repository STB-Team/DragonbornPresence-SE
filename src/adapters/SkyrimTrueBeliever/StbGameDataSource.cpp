#include "DragonbornPresence/adapters/SkyrimTrueBeliever/StbGameDataSource.h"

#include "DragonbornPresence/adapters/SkyrimTrueBeliever/PapyrusValueReader.h"
#include "DragonbornPresence/adapters/SkyrimTrueBeliever/StbStringEncoding.h"
#include "DragonbornPresence/core/Difficulty.h"
#include "DragonbornPresence/core/TextUtils.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace DragonbornPresence::adapters::SkyrimTrueBeliever
{

    namespace
    {

        /// Identifiers of forms and scripts belonging specifically to STB.
        constexpr std::uint32_t kDifficultyQuestFormId = 0x1417C4;
        constexpr std::string_view kStbPluginName = "STB.esp";
        constexpr std::string_view kDifficultyScriptName =
            "aamz_mcmdatastorage";
        constexpr std::string_view kDifficultyPropertyName =
            "aaMZ_SelectedLevel_OfDifficulty";
        constexpr std::string_view kDeathsGlobalEditorId =
            "aaMZgv_NowDeath";

        constexpr std::string_view kNoStoneText = "не выбран";
        constexpr std::string_view kCombatText = "В бою";

        /// Static description of one STB standing stone.
        ///
        /// Editor IDs are string literals and therefore remain valid for the
        /// entire plugin lifetime. The fallback is used when a spell or its
        /// description JSON cannot be resolved.
        struct StoneDefinition
        {
            std::string_view descriptionSpellEditorId;
            std::string_view fallbackName;
        };

        constexpr std::array kStoneDefinitions{
            StoneDefinition{"aaMZs_DoomstoneWarriorDesc", "🪓-Воин"},
            StoneDefinition{"aaMZs_DoomstoneSteedDesc", "🐴-Конь"},
            StoneDefinition{"aaMZs_DoomstoneAtronachDesc", "🎭-Атронах"},
            StoneDefinition{"aaMZs_DoomstoneApprenticeDesc", "📜-Ученик"},
            StoneDefinition{"aaMZs_DoomstoneLordDesc", "👑-Лорд"},
            StoneDefinition{"aaMZs_DoomstoneThiefDesc", "🧤-Вор"},
            StoneDefinition{"aaMZs_DoomstoneMageDesc", "🧙‍-Маг"},
            StoneDefinition{"aaMZs_DoomstoneRitualDesc", "👻-Ритуал"},
            StoneDefinition{"aaMZs_DoomstoneSnakeDesc", "🐍-Змей"},
            StoneDefinition{"aaMZs_DoomstoneLadyDesc", "👠-Леди"},
            StoneDefinition{"aaMZs_DoomstoneLoverDesc", "💖-Любовник"},
            StoneDefinition{"aaMZs_DoomstoneShadowDesc", "🌙-Тень"},
            StoneDefinition{"aaMZs_DoomstoneTowerDesc", "🛡️-Башня"},
            StoneDefinition{"aaMZs_DoomstoneBeastDesc", "🦧-Зверь"},
            StoneDefinition{"aaMZs_DoomstoneWindDesc", "🌪️-Ветер"},
            StoneDefinition{"aaMZs_DoomstoneWaterDesc", "🌊-Вода"},
            StoneDefinition{"aaMZs_DoomstoneTreeDesc", "🌲-Дерево"},
            StoneDefinition{"aaMZs_DoomstoneSunDesc", "☀️-Солнце"},
            StoneDefinition{"aaMZs_DoomstoneEarthDesc", "⛰️-Земля"},
        };

        /// Converts a nullable Skyrim string to valid UTF-8.
        ///
        /// Strings that are already UTF-8 are copied without conversion.
        /// Legacy CP1251 strings are converted before crossing the adapter boundary.
        [[nodiscard]] std::string FromGameString(const char *value)
        {
            if (!value || *value == '\0')
                return {};

            return core::IsValidUtf8(value)
                       ? std::string(value)
                       : Cp1251ToUtf8(value);
        }

    } // namespace

    void StbGameDataSource::Initialize()
    {
        auto *dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler)
        {
            SKSE::log::error(
                "STB integration: TESDataHandler is unavailable.");
            return;
        }

        runtimeData_.deaths =
            RE::TESForm::LookupByEditorID<RE::TESGlobal>(
                kDeathsGlobalEditorId.data());

        runtimeData_.difficultyQuest =
            dataHandler->LookupForm<RE::TESQuest>(
                kDifficultyQuestFormId,
                kStbPluginName.data());

        runtimeData_.stones.clear();
        runtimeData_.stones.reserve(kStoneDefinitions.size());

        for (const auto &definition : kStoneDefinitions)
        {
            auto *descriptionSpell =
                RE::TESForm::LookupByEditorID<RE::SpellItem>(
                    definition.descriptionSpellEditorId.data());

            runtimeData_.stones.push_back({
                descriptionSpell,
                ParseStoneName(
                    descriptionSpell,
                    definition.fallbackName),
            });
        }

        SKSE::log::info(
            "STB integration: deaths={} difficulty={} stones={}/{}.",
            runtimeData_.deaths != nullptr,
            runtimeData_.difficultyQuest != nullptr,
            std::ranges::count_if(
                runtimeData_.stones,
                [](const auto &stone)
                {
                    return stone.descriptionSpell != nullptr;
                }),
            runtimeData_.stones.size());
    }

    core::PlayerSnapshot StbGameDataSource::ReadPlayerSnapshot()
    {
        core::PlayerSnapshot snapshot;

        auto *player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return snapshot;

        snapshot.level = player->GetLevel();

        if (runtimeData_.deaths)
        {
            snapshot.deaths =
                static_cast<int>(runtimeData_.deaths->value);
        }

        snapshot.stone = ReadSelectedStone(player);
        snapshot.difficulty = ReadSelectedDifficulty();
        snapshot.location = BuildLocationContext(player);
        snapshot.inCombat = player->IsInCombat();

        if (snapshot.inCombat)
        {
            if (const auto target =
                    player->GetActorRuntimeData()
                        .currentCombatTarget.get())
            {
                std::string targetName =
                    FromGameString(target->GetName());

                if (!targetName.empty())
                {
                    lastCombatTarget_ = CombatTargetData{
                        std::move(targetName),
                        target->GetLevel(),
                    };
                }
            }

            snapshot.combatText =
                lastCombatTarget_
                    ? std::format(
                          "{} с {} (ур. {})",
                          kCombatText,
                          lastCombatTarget_->name,
                          lastCombatTarget_->level)
                    : std::string(kCombatText);
        }
        else
        {
            lastCombatTarget_.reset();
        }

        return snapshot;
    }

    std::string StbGameDataSource::ParseStoneName(
        RE::SpellItem *descriptionSpell,
        std::string_view fallbackName)
    {
        if (!descriptionSpell)
            return std::string(fallbackName);

        RE::BSString description;
        descriptionSpell->GetDescription(
            description,
            descriptionSpell);

        if (description.empty())
            return std::string(fallbackName);

        try
        {
            const auto root =
                nlohmann::json::parse(description.c_str());

            const auto name = root.find("name");
            if (name != root.end() && name->is_string())
            {
                const auto parsedName = name->get<std::string>();
                if (!parsedName.empty())
                    return parsedName;
            }
        }
        catch (const nlohmann::json::exception &error)
        {
            SKSE::log::warn(
                "STB stone '{}': invalid description JSON: {}",
                descriptionSpell->GetFormEditorID(),
                error.what());
        }

        return std::string(fallbackName);
    }

    std::string StbGameDataSource::ReadSelectedDifficulty() const
    {
        const auto difficulty =
            PapyrusValueReader::GetFirstAliasScriptPropertyOrVariable<int>(
                runtimeData_.difficultyQuest,
                kDifficultyScriptName.data(),
                kDifficultyPropertyName.data());

        return std::string(core::DifficultyName(difficulty));
    }

    std::string StbGameDataSource::ReadSelectedStone(
        RE::PlayerCharacter *player) const
    {
        if (!player)
            return std::string(kNoStoneText);

        const auto selectedStone = std::ranges::find_if(
            runtimeData_.stones,
            [player](const auto &stone)
            {
                return stone.descriptionSpell &&
                       player->HasSpell(stone.descriptionSpell);
            });

        return selectedStone != runtimeData_.stones.end()
                   ? selectedStone->name
                   : std::string(kNoStoneText);
    }

    std::string StbGameDataSource::FormEditorId(
        const RE::TESForm *form)
    {
        if (!form)
            return {};

        const char *editorId = form->GetFormEditorID();

        return editorId
                   ? std::string(editorId)
                   : std::string{};
    }

    core::LocationContext StbGameDataSource::BuildLocationContext(
        RE::PlayerCharacter *player)
    {
        core::LocationContext context;

        if (!player)
            return context;

        auto *worldspace = player->GetWorldspace();
        auto *location = player->GetCurrentLocation();
        auto *cell = player->GetParentCell();

        context.worldspaceEditorId = FormEditorId(worldspace);
        context.cellEditorId = FormEditorId(cell);

        std::string locationName;

        for (auto *current = location;
             current;
             current = current->parentLoc)
        {
            std::string editorId = FormEditorId(current);

            if (!editorId.empty())
            {
                context.locationEditorIds.push_back(
                    std::move(editorId));
            }

            if (locationName.empty())
            {
                locationName =
                    FromGameString(current->GetName());
            }
        }

        const std::string worldspaceName =
            worldspace
                ? FromGameString(worldspace->GetName())
                : "";

        const std::string cellName =
            cell
                ? FromGameString(cell->GetName())
                : "";

        if (!worldspaceName.empty())
        {
            context.displayName =
                !locationName.empty() &&
                        locationName != worldspaceName
                    ? std::format(
                          "{}: {}",
                          worldspaceName,
                          locationName)
                    : worldspaceName;
        }
        else
        {
            context.displayName =
                !locationName.empty()
                    ? locationName
                    : cellName;
        }

        return context;
    }

} // namespace DragonbornPresence::adapters::SkyrimTrueBeliever