#pragma once

#include "edge_tts/common/Result.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/subtitles/SubtitleCue.hpp"

#include <optional>
#include <string>
#include <vector>

namespace edge_tts::subtitles {

// Accumulates BoundaryChunk events into SubtitleCue values and produces SRT text.
//
// Reference: submaker.py SubMaker
//
// Behaviour:
//   - feed() accepts WordBoundary and SentenceBoundary chunks.
//   - The first call to feed() locks the boundary type; subsequent calls must
//     match it.  A type mismatch returns ErrorCode::invalid_argument.
//   - The SubtitleCue time range is [offset_ticks, offset_ticks + duration_ticks)
//     converted via SubtitleTime::from_edge_ticks().
//   - The cue text is stored verbatim — MetadataJsonParser has already
//     XML-unescaped it (reference: unescape() in communicate.py).
//   - to_srt() delegates to SrtComposer::compose(); it does NOT reset state.
//     Calling feed() after to_srt() continues to accumulate new cues (reference
//     behaviour: get_srt() is idempotent with respect to the cue list).
//   - clear() resets the cue list and unlocks the type — allowing a fresh feed
//     sequence with a different boundary type if desired.
class SubMaker {
public:
    // Append one boundary event to the cue list.
    // Returns:
    //   - ok(void)  on success.
    //   - fail(invalid_argument)  if boundary type conflicts with a prior feed().
    //   - fail(invalid_argument)  if offset_ticks or (offset+duration)_ticks < 0.
    [[nodiscard]] common::Result<void> feed(const core::BoundaryChunk& boundary);

    // Returns a copy of all accumulated cues in feed order.
    [[nodiscard]] std::vector<SubtitleCue> cues() const;

    // Compose accumulated cues into an SRT string using SrtComposer.
    // Cues are sorted and filtered as per the reference (start>=end skipped,
    // blank text skipped).  Does not clear state.
    [[nodiscard]] common::Result<std::string> to_srt() const;

    // Reset cue list and boundary-type lock.  Equivalent to constructing a
    // fresh SubMaker.
    void clear() noexcept;

private:
    std::vector<SubtitleCue>                 cues_;
    std::optional<core::BoundaryEventType>   type_; // locked after first feed()
};

} // namespace edge_tts::subtitles
