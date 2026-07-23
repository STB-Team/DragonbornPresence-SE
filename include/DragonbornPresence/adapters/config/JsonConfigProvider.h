#pragma once

#include "DragonbornPresence/application/ports/IConfigProvider.h"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace DragonbornPresence::adapters::config
{

    /// Infrastructure adapter that loads the runtime JSON configuration.
    ///
    /// Filesystem access, JSON parsing, and SKSE diagnostics belong to this
    /// adapter. Callers receive only the core-owned Config model and therefore
    /// do not need to depend on nlohmann::json or know the configuration path.
    class JsonConfigProvider final
        : public ::DragonbornPresence::application::ports::IConfigProvider
    {
    public:
        /// Reads the base asset catalog and optional user settings overlay.
        ///
        /// Missing files use core defaults. Malformed initial input is logged and
        /// also falls back to defaults so plugin startup remains safe.
        [[nodiscard]] core::Config Load() override;

        /// Reloads both JSON files only after their filesystem state changes.
        ///
        /// Malformed changed input returns no value and preserves the last config
        /// already owned by the application coordinator.
        [[nodiscard]] std::optional<core::Config>
        ReloadIfChanged() override;

    private:
        struct FileState
        {
            bool exists = false;
            std::filesystem::file_time_type writeTime{};
            std::uintmax_t size = 0;

            [[nodiscard]] bool operator==(
                const FileState &) const noexcept = default;
        };

        [[nodiscard]] static FileState Inspect(
            const std::filesystem::path &path) noexcept;

        FileState baseState_;
        FileState userState_;
        bool hasObservedFiles_ = false;
    };

} // namespace DragonbornPresence::adapters::config