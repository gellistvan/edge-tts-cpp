#pragma once

#include <string>
#include <vector>

namespace edge_tts::core {

enum class VoiceGender {
    Female,
    Male,
    Unknown,
};

struct Voice {
    std::string              name;         // full "Microsoft Server Speech..." form
    std::string              short_name;   // e.g. "en-US-EmmaMultilingualNeural"
    VoiceGender              gender{VoiceGender::Unknown};
    std::string              locale;       // e.g. "en-US"
    std::vector<std::string> styles;       // speaking styles supported by this voice

    [[nodiscard]] bool operator==(const Voice& other) const noexcept {
        return name       == other.name
            && short_name == other.short_name
            && gender     == other.gender
            && locale     == other.locale
            && styles     == other.styles;
    }

    [[nodiscard]] bool operator!=(const Voice& other) const noexcept {
        return !(*this == other);
    }
};

} // namespace edge_tts::core
