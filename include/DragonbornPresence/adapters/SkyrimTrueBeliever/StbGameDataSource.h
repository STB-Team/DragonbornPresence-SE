#pragma once

#include "DragonbornPresence/application/ports/IGameDataSource.h"

#include <RE/Skyrim.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace DragonbornPresence::adapters::SkyrimTrueBeliever
{

    /// Reads STB and Skyrim state through the IGameDataSource application port.
    ///
    /// Skyrim owns every RE::* object referenced by this adapter. The adapter keeps
    /// non-owning form pointers only for the active game session and never deletes
    /// them. Data crossing the application boundary is copied into PlayerSnapshot,
    /// so core and application code never retain pointers to engine objects.
    class StbGameDataSource final
        : public ::DragonbornPresence::application::ports::IGameDataSource
    {
    public:
        /// Resolves the STB forms that can be reused by later snapshot reads.
        ///
        /// This method must run only after Skyrim has made TESDataHandler and plugin
        /// forms available. Missing forms remain null and produce fallback values.
        void Initialize() override;

        /// Copies the current player state into an engine-independent snapshot.
        ///
        /// Temporary player, location, cell, worldspace, and combat-target pointers
        /// are inspected only during this call and never stored in the snapshot.
        [[nodiscard]] core::PlayerSnapshot ReadPlayerSnapshot() override;

    private:
        /// Associates a Skyrim-owned description spell with an adapter-owned name.
        struct StoneRuntimeData
        {
            RE::SpellItem *descriptionSpell = nullptr;
            std::string name;
        };

        /// Caches non-owning STB forms to avoid repeated Editor ID lookups.
        ///
        /// Skyrim owns the pointed-to objects. This structure owns only the vector
        /// and copied stone display names.
        struct StbRuntimeData
        {
            RE::TESGlobal *deaths = nullptr;
            RE::TESQuest *difficultyQuest = nullptr;
            std::vector<StoneRuntimeData> stones;
            RE::BGSListForm *activeGods = nullptr;
            RE::TESGlobal *vampireBlood = nullptr;
            RE::TESGlobal *werewolfBlood = nullptr;
            RE::SpellItem *aedraCurse = nullptr;
        };

        /// Keeps the last copied combat target while Skyrim temporarily loses its handle.
        ///
        /// Only plain values are cached; the target actor pointer is never retained.
        struct CombatTargetData
        {
            std::string name;
            std::uint16_t level = 0;
        };

        /// Extracts a localized stone name from the spell description JSON.
        [[nodiscard]] static std::string ParseStoneName(
            RE::SpellItem *descriptionSpell,
            std::string_view fallbackName);

        /// Reads the selected STB difficulty through the quest's Papyrus alias.
        [[nodiscard]] std::string ReadSelectedDifficulty() const;

        /// Returns the first resolved standing stone currently affecting the player.
        [[nodiscard]] std::string ReadSelectedStone(
            RE::PlayerCharacter *player) const;

        /// Returns selected Aedra or Daedra names, or the active Aedra curse.
        [[nodiscard]] std::string ReadSelectedGod(
            RE::PlayerCharacter *player) const;

        /// Copies a nullable Skyrim form Editor ID into an owned string.
        [[nodiscard]] static std::string FormEditorId(
            const RE::TESForm *form);

        /// Copies the current Skyrim location hierarchy into a stable core model.
        [[nodiscard]] static core::LocationContext BuildLocationContext(
            RE::PlayerCharacter *player);

        StbRuntimeData runtimeData_;
        std::optional<CombatTargetData> lastCombatTarget_;
    };

} // namespace DragonbornPresence::adapters::SkyrimTrueBeliever