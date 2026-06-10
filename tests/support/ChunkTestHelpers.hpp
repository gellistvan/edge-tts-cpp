#pragma once
// Shared TtsChunk builders for test files that need AudioChunk and BoundaryChunk
// values without connecting to the real service.
#include "core/Chunk.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace edge_tts::test {

inline core::AudioChunk make_audio(std::string_view s) {
    core::AudioChunk ac;
    ac.data.reserve(s.size());
    for (char c : s) ac.data.push_back(static_cast<std::byte>(c));
    return ac;
}

inline core::BoundaryChunk make_boundary(std::string text,
                                         std::int64_t offset_ticks   = 0,
                                         std::int64_t duration_ticks = 10'000'000) {
    core::BoundaryChunk bc;
    bc.type           = core::BoundaryEventType::SentenceBoundary;
    bc.text           = std::move(text);
    bc.offset_ticks   = offset_ticks;
    bc.duration_ticks = duration_ticks;
    return bc;
}

} // namespace edge_tts::test
