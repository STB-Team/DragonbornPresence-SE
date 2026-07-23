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
        constexpr std::string_view kActiveGodsListEditorId =
            "aaMZfl_ActiveGodsList";
        constexpr std::string_view kVampireBloodEditorId =
            "aaMZgv_VampireBlood";
        constexpr std::string_view kWerewolfBloodEditorId =
            "aaMZgv_WerewolfBlood";
        constexpr std::string_view kAedraCurseSpellEditorId =
            "aaMZs_AedraCurseDUPLICATE001";

        constexpr std::string_view kNoStoneText = "не выбран";
        constexpr std::string_view kCombatText = "В бою";
        constexpr std::string_view kAedraCurseText = "Проклятие Аэдра";
        constexpr std::string_view kVampireText = "Вампир";
        constexpr std::string_view kWerewolfText = "Вервольф";

        struct GodDefinition
        {
            std::string_view activeFormEditorId;
            std::string_view name;
        };

        /// Forms inserted into aaMZfl_ActiveGodsList by STB altar scripts.
        constexpr std::array kGodDefinitions{
            GodDefinition{"aaMZf_AkatoshFaction", "Акатош"},
            GodDefinition{"aaMZf_ArkayFaction", "Аркей"},
            GodDefinition{"aaMZf_JulianosFaction", "Джулианос"},
            GodDefinition{"aaMZf_KynarethFaction", "Кинарет"},
            GodDefinition{"aaMZf_ZenitharFaction", "Зенитар"},
            GodDefinition{"aaMZf_StendarrFaction", "Стендарр"},
            GodDefinition{"aaMZf_TalosFaction", "Талос"},
            GodDefinition{"aaMZf_DibellaFaction", "Дибелла"},
            GodDefinition{"aaMZf_MaraFaction", "Мара"},
            GodDefinition{"aaMZgv_AzuraFaithPoints", "Азура"},
            GodDefinition{"aaMZgv_BoetiyaFaithPoints", "Боэтия"},
            GodDefinition{"aaMZgv_ClavikusVailFaithPoints", "Клавикус Вайл"},
            GodDefinition{"aaMZgv_HermeusMoraFaithPoints", "Хермеус Мора"},
            GodDefinition{"aaMZgv_HirsinFaithPoints", "Хирсин"},
            GodDefinition{"aaMZgv_MalakatFaithPoints", "Малакат"},
            GodDefinition{"aaMZgv_MehrunesDagonFaithPoints", "Мерунес Дагон"},
            GodDefinition{"aaMZgv_MephalaFaithPoints", "Мефала"},
            GodDefinition{"aaMZgv_MeridiaFaithPoints", "Меридия"},
            GodDefinition{"aaMZgv_MolagBaalFaithPoints", "Молаг Бал"},
            GodDefinition{"aaMZgv_NamiraFaithPoints", "Намира"},
            GodDefinition{"aaMZgv_NoktyrnalFaithPoints", "Ноктюрнал"},
            GodDefinition{"aaMZgv_PeriaytFaithPoints", "Периайт"},
            GodDefinition{"aaMZgv_SangvinFaithPoints", "Сангвин"},
            GodDefinition{"aaMZgv_SheogotatFaithPoints", "Шеогорат"},
            GodDefinition{"aaMZgv_VerminaFaithPoints", "Вермина"},
        };

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

        runtimeData_.activeGods =
            RE::TESForm::LookupByEditorID<RE::BGSListForm>(
                kActiveGodsListEditorId.data());
        runtimeData_.vampireBlood =
            RE::TESForm::LookupByEditorID<RE::TESGlobal>(
                kVampireBloodEditorId.data());
        runtimeData_.werewolfBlood =
            RE::TESForm::LookupByEditorID<RE::TESGlobal>(
                kWerewolfBloodEditorId.data());
        runtimeData_.aedraCurse =
            RE::TESForm::LookupByEditorID<RE::SpellItem>(
                kAedraCurseSpellEditorId.data());

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
            "STB integration: deaths={} difficulty={} stones={}/{} "
            "gods={} vampire={} werewolf={} curse={}.",
            runtimeData_.deaths != nullptr,
            runtimeData_.difficultyQuest != nullptr,
            std::ranges::count_if(
                runtimeData_.stones,
                [](const auto &stone)
                {
                    return stone.descriptionSpell != nullptr;
                }),
            runtimeData_.stones.size(),
            runtimeData_.activeGods != nullptr,
            runtimeData_.vampireBlood != nullptr,
            runtimeData_.werewolfBlood != nullptr,
            runtimeData_.aedraCurse != nullptr);
    }

    core::PlayerSnapshot StbGameDataSource::ReadPlayerSnapshot()
    {
        core::PlayerSnapshot snapshot;

        auto *player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return snapshot;

        snapshot.level = player->GetLevel();
        snapshot.playerName = FromGameString(player->GetName());

        if (runtimeData_.deaths)
        {
            snapshot.deaths =
                static_cast<int>(runtimeData_.deaths->value);
        }

        snapshot.stone = ReadSelectedStone(player);
        snapshot.difficulty = ReadSelectedDifficulty();
        snapshot.god = ReadSelectedGod(player);
        snapshot.vampire =
            runtimeData_.vampireBlood &&
                    runtimeData_.vampireBlood->value > 0.0F
                ? std::string(kVampireText)
                : std::string{};
        snapshot.werewolf =
            runtimeData_.werewolfBlood &&
                    runtimeData_.werewolfBlood->value > 0.0F
                ? std::string(kWerewolfText)
                : std::string{};
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

    std::string StbGameDataSource::ReadSelectedGod(
        RE::PlayerCharacter *player) const
    {
        if (!player)
            return {};

        if (runtimeData_.aedraCurse &&
            player->HasSpell(runtimeData_.aedraCurse))
        {
            return std::string(kAedraCurseText);
        }

        if (!runtimeData_.activeGods)
            return {};

        std::string selectedGods;
        for (const auto *activeForm : runtimeData_.activeGods->forms)
        {
            const std::string editorId = FormEditorId(activeForm);
            const auto definition = std::ranges::find_if(
                kGodDefinitions,
                [&editorId](const auto &candidate)
                {
                    return candidate.activeFormEditorId == editorId;
                });
            if (definition == kGodDefinitions.end())
                continue;

            if (!selectedGods.empty())
                selectedGods.append(", ");
            selectedGods.append(definition->name);
        }

        return selectedGods;
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