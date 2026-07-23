#pragma once

#include <string>

namespace DragonbornPresence::adapters::SkyrimTrueBeliever
{

    /// Converts a nullable CP1251 string into owned UTF-8 text.
    ///
    /// Returns an empty string for null, empty, or unconvertible input.
    [[nodiscard]] std::string Cp1251ToUtf8(
        const char *value);

} // namespace DragonbornPresence::adapters::SkyrimTrueBeliever