#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace edge_tts::core {

// Classifies an incoming boundary event on the Edge TTS wire.
// Named BoundaryEventType to distinguish from the request-side BoundaryType
// (which controls which events are REQUESTED, defined in TtsConfig.hpp).
//
// Python reference: communicate.py yields {"type": "WordBoundary"} or
// {"type": "SentenceBoundary"}; submaker.py feeds them to build SRT cues.
enum class BoundaryEventType {
    WordBoundary,
    SentenceBoundary,
};

// Raw MP3 audio data received from the service.
// Python reference: {"type": "audio", "data": <bytes>}
struct AudioChunk {
    std::vector<std::byte> data;

    [[nodiscard]] bool operator==(const AudioChunk& other) const noexcept {
        return data == other.data;
    }
    [[nodiscard]] bool operator!=(const AudioChunk& other) const noexcept {
        return !(*this == other);
    }
};

// A word or sentence boundary event received from the Edge TTS service.
//
// Python reference fields: {"type": "WordBoundary"|"SentenceBoundary",
//                           "offset": <float>, "duration": <float>, "text": <str>}
//
// offset_ticks and duration_ticks use 100-nanosecond tick units:
//   1 second = 10,000,000 ticks  (TICKS_PER_SECOND in constants.py)
// To convert to microseconds (as Python's SubMaker does):
//   microseconds = ticks / 10
struct BoundaryChunk {
    BoundaryEventType type{BoundaryEventType::SentenceBoundary};
    std::string       text;
    std::int64_t      offset_ticks{};
    std::int64_t      duration_ticks{};

    [[nodiscard]] bool operator==(const BoundaryChunk& other) const noexcept {
        return type           == other.type
            && text           == other.text
            && offset_ticks   == other.offset_ticks
            && duration_ticks == other.duration_ticks;
    }
    [[nodiscard]] bool operator!=(const BoundaryChunk& other) const noexcept {
        return !(*this == other);
    }
};

// A single unit of output from the TTS pipeline: either raw MP3 audio or a
// timing/boundary annotation.  Corresponds to one yielded item from
// Communicate.stream() in the Python reference.
using TtsChunk = std::variant<AudioChunk, BoundaryChunk>;

// Returns true when chunk holds an AudioChunk.
[[nodiscard]] bool is_audio(const TtsChunk& chunk) noexcept;

// Returns true when chunk holds a BoundaryChunk.
[[nodiscard]] bool is_boundary(const TtsChunk& chunk) noexcept;

} // namespace edge_tts::core
