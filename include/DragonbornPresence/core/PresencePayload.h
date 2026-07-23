#pragma once

#include <string_view>

namespace DragonbornPresence::core
{

    struct PresencePayload
    {
        std::string_view details;    // первая строка Presence
        std::string_view state;      // вторая строка Presence
        std::string_view largeImage; // большая картинка
        std::string_view largeText;  // подсказка большой картинки
        std::string_view smallImage; // маленькая картинка
        std::string_view smallText;  // подсказка маленькой картинки
    };

} // namespace DragonbornPresence::core