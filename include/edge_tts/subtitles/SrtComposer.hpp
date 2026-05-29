#pragma once

#include "edge_tts/common/Result.hpp"
#include "edge_tts/subtitles/SubtitleCue.hpp"

#include <span>
#include <string>

namespace edge_tts::subtitles {

// Composes a sequence of SubtitleCue values into an SRT-formatted string.
//
// Reference: srt_composer.py compose(), sort_and_reindex(), to_srt(), make_legal_content()
//
// Algorithm:
//   1. Sort cues by (start, end) ascending — matching Python's Subtitle.__lt__.
//   2. Filter: skip cues where start >= end, or text is all-whitespace after
//      stripping (reference SUBTITLE_SKIP_CONDITIONS[0] and [2]).
//      The start-<-0 condition (reference [1]) cannot occur because
//      SubtitleTime::from_edge_ticks rejects negative ticks.
//   3. Number remaining cues from 1 (reindex after filtering).
//   4. For each kept cue: clean the text via make_legal_content equivalent
//      (strip leading/trailing '\n', collapse consecutive blank lines to one '\n').
//   5. Emit blocks: "{idx}\n{start} --> {end}\n{content}\n\n"
//
// Output properties:
//   - LF ('\n') only — no CRLF.
//   - Each block ends with exactly "\n\n".
//   - Empty input returns "".
//   - Cues that are entirely filtered produce no output and do not consume an index.
//   - compose() never fails — Result<> is used for API consistency.
class SrtComposer {
public:
    [[nodiscard]] common::Result<std::string>
    compose(std::span<const SubtitleCue> cues) const;
};

} // namespace edge_tts::subtitles
