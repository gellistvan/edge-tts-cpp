#include "edge_tts/subtitles/SubMaker.hpp"

namespace edge_tts::subtitles {

void SubMaker::add_boundary(const core::BoundaryChunk& boundary) {
    constexpr auto ticks_per_millisecond = 10'000;
    const auto start = std::chrono::milliseconds(boundary.offset_ticks / ticks_per_millisecond);
    const auto end = std::chrono::milliseconds((boundary.offset_ticks + boundary.duration_ticks) / ticks_per_millisecond);
    entries_.push_back(SubtitleEntry{start, end, boundary.text});
}

std::vector<SubtitleEntry> SubMaker::entries() const {
    return entries_;
}

} // namespace edge_tts::subtitles
