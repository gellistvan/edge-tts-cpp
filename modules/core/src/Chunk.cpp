#include "core/Chunk.hpp"

namespace edge_tts::core {

bool is_audio(const TtsChunk& chunk) noexcept {
    return std::holds_alternative<AudioChunk>(chunk);
}

bool is_boundary(const TtsChunk& chunk) noexcept {
    return std::holds_alternative<BoundaryChunk>(chunk);
}

} // namespace edge_tts::core
