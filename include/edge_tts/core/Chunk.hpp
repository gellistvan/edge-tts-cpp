#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace edge_tts::core {

// Classifies an incoming boundary event on the Edge TTS wire.
// Named BoundaryEventType to distinguish from the request-side BoundaryType
// (which controls what events are REQUESTED) defined in TtsConfig.hpp.
enum class BoundaryEventType {
    WordBoundary,
    SentenceBoundary,
};

struct AudioChunk {
    std::vector<std::byte> data;

    [[nodiscard]] bool operator==(const AudioChunk& other) const noexcept {
        return data == other.data;
    }
};

// A word or sentence boundary event received from the Edge TTS service.
// offset_ticks and duration_ticks use 100-nanosecond tick units
// (1 second = 10,000,000 ticks), matching the Edge protocol wire format.
struct BoundaryChunk {
    BoundaryEventType type{BoundaryEventType::SentenceBoundary};
    std::int64_t      offset_ticks{};
    std::int64_t      duration_ticks{};
    std::string       text;

    [[nodiscard]] bool operator==(const BoundaryChunk& other) const noexcept {
        return type           == other.type
            && offset_ticks   == other.offset_ticks
            && duration_ticks == other.duration_ticks
            && text           == other.text;
    }
};

// A single unit of output from the TTS pipeline: either raw MP3 audio data or
// a timing/boundary annotation.
using TtsChunk = std::variant<AudioChunk, BoundaryChunk>;

} // namespace edge_tts::core
