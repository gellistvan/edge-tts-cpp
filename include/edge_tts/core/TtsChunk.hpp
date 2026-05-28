#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace edge_tts::core {

enum class BoundaryType {
    WordBoundary,
    SentenceBoundary
};

struct AudioChunk {
    std::vector<std::byte> data;
};

struct BoundaryChunk {
    BoundaryType type{BoundaryType::SentenceBoundary};
    std::int64_t offset_ticks{};
    std::int64_t duration_ticks{};
    std::string text;
};

using TtsChunk = std::variant<AudioChunk, BoundaryChunk>;

} // namespace edge_tts::core
