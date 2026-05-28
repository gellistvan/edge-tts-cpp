#pragma once

#include "edge_tts/subtitles/Subtitle.hpp"

#include <string>
#include <vector>

namespace edge_tts::subtitles {

class SrtComposer final {
public:
    [[nodiscard]] static std::string compose(const std::vector<SubtitleEntry>& entries);
};

} // namespace edge_tts::subtitles
