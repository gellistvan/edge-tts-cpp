#pragma once

#include "edge_tts/core/TtsChunk.hpp"
#include "edge_tts/subtitles/Subtitle.hpp"

#include <vector>

namespace edge_tts::subtitles {

class SubMaker final {
public:
    void add_boundary(const core::BoundaryChunk& boundary);
    [[nodiscard]] std::vector<SubtitleEntry> entries() const;

private:
    std::vector<SubtitleEntry> entries_;
};

} // namespace edge_tts::subtitles
