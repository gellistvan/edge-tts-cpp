#pragma once

#include <chrono>
#include <string>

namespace edge_tts::subtitles {

struct SubtitleEntry {
    std::chrono::milliseconds start{};
    std::chrono::milliseconds end{};
    std::string text;
};

} // namespace edge_tts::subtitles
