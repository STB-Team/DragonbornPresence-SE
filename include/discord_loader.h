#pragma once

#include <string>

namespace DragonbornPresence::detail {

[[nodiscard]] bool IsDiscordRunning(std::string* failureReason = nullptr) noexcept;
[[nodiscard]] bool LoadDiscordSdk(std::string* failureReason = nullptr) noexcept;

}  // namespace DragonbornPresence::detail
