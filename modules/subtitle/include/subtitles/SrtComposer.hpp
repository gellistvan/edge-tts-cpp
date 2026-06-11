#pragma once

#include "common/Result.hpp"
#include "subtitles/SubtitleCue.hpp"

#include <span>
#include <string>

namespace edge_tts::subtitles {

// Composes a sequence of SubtitleCue values into an SRT-formatted string.
//
// Algorithm:
//   1. Sort cues by (start, end) ascending.
//   2. Filter: skip cues where start >= end, or text is all-whitespace.
//      Negative ticks cannot reach here: SubtitleTime::from_edge_ticks rejects them.
//   3. Number remaining cues from 1 (reindex after filtering).
//   4. For each kept cue: clean the text (strip leading/trailing '\n',
//      collapse consecutive blank lines to one '\n').
//   5. Emit blocks: "{idx}\n{start} --> {end}\n{content}\n\n"
//
// Output properties:
//   - LF ('\n') only — no CRLF.
//   - Each block ends with exactly "\n\n".
//   - Empty input returns "".
//   - Filtered cues produce no output and do not consume an index.
//   - compose() never fails — Result<> is used for API consistency.
class SrtComposer {
public:
    [[nodiscard]] common::Result<std::string>
    compose(std::span<const SubtitleCue> cues) const;
};

} // namespace edge_tts::subtitles
