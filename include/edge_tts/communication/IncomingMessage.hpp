#pragma once

#include "edge_tts/core/Chunk.hpp"

#include <optional>

namespace edge_tts::communication {

// Classifies a parsed incoming Edge TTS WebSocket message.
//
// Reference: communicate.py __stream() yield paths
//
//   audio     → binary frame with Path:audio, Content-Type:audio/mpeg
//   boundary  → text frame with Path:audio.metadata (word/sentence boundary)
//   turn_end  → text frame with Path:turn.end
//   ignored   → text frame with Path:response or Path:turn.start, or
//               binary frame with no Content-Type and empty body
enum class IncomingMessageKind {
    audio,
    boundary,
    turn_end,
    ignored,
};

// A single parsed event from the Edge TTS WebSocket stream.
//
// chunk is populated for audio and boundary kinds only:
//   audio    → chunk = AudioChunk{data}
//   boundary → chunk = BoundaryChunk{type, text, offset_ticks, duration_ticks}
//   turn_end → chunk = nullopt
//   ignored  → chunk = nullopt
struct IncomingMessage {
    IncomingMessageKind            kind;
    std::optional<core::TtsChunk>  chunk;
};

} // namespace edge_tts::communication
