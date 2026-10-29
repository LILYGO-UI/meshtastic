#ifndef LILYGO_UI_MESHTASTIC_UTF8_HPP
#define LILYGO_UI_MESHTASTIC_UTF8_HPP

#include <algorithm>
#include <cstddef>
#include <string>

inline std::string utf8_prefix_bytes(const std::string &text, std::size_t max_bytes)
{
    auto end = std::min(max_bytes, text.size());
    while (end < text.size() && end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0U) == 0x80U) --end;
    return text.substr(0, end);
}

inline std::string utf8_prefix_characters(const std::string &text, std::size_t max_characters)
{
    std::size_t end = 0;
    for (std::size_t characters = 0; end < text.size() && characters < max_characters; ++characters) {
        ++end;
        while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0U) == 0x80U) ++end;
    }
    return text.substr(0, end);
}

#endif
