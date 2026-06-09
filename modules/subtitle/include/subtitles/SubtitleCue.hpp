#pragma once

#include "subtitles/SubtitleTime.hpp"

#include <string>

namespace edge_tts::subtitles {

// One SRT subtitle cue: a time range and display text.
//
// Reference mapping from Python (submaker.py):
//   Subtitle.start   → start  (from timedelta(microseconds=offset / 10))
//   Subtitle.end     → end    (from timedelta(microseconds=(offset+duration)/10))
//   Subtitle.content → text   (XML-unescaped boundary text)
//
// Skip rules applied by SrtComposer::compose() (reference srt_composer.py):
//   - text is empty or all-whitespace after stripping (SUBTITLE_SKIP_CONDITIONS[0])
//   - start >= end  (SUBTITLE_SKIP_CONDITIONS[2]; zero-duration is also skipped)
//
// The "start < 0" skip condition (SUBTITLE_SKIP_CONDITIONS[1]) cannot be
// triggered here because SubtitleTime::from_edge_ticks rejects negative ticks.
struct SubtitleCue {
    SubtitleTime start;  // inclusive start time
    SubtitleTime end;    // exclusive end time
    std::string  text;   // display content (not XML-escaped; XML entities already resolved)
};

} // namespace edge_tts::subtitles
