#pragma once

#include "subtitles/SubtitleTime.hpp"

#include <string>

namespace edge_tts::subtitles {

// One SRT subtitle cue: a time range and display text.
//
// SrtComposer::compose() skips cues where:
//   - text is empty or all-whitespace.
//   - start >= end (zero-duration is also skipped).
// Negative start cannot occur: SubtitleTime::from_edge_ticks rejects negative ticks.
struct SubtitleCue {
    SubtitleTime start;  // inclusive start time
    SubtitleTime end;    // exclusive end time
    std::string  text;   // display content (not XML-escaped; XML entities already resolved)
};

} // namespace edge_tts::subtitles
