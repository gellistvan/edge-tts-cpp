#pragma once

#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/Result.hpp"
#include "edge_tts/communication/ConnectionMetadata.hpp"
#include "edge_tts/communication/IncomingMessage.hpp"
#include "edge_tts/communication/WebSocketMessage.hpp"
#include "edge_tts/core/TtsConfig.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace edge_tts::communication {

// Builds outgoing WebSocket text frames and parses incoming frames for the
// Edge TTS protocol.
//
// The IClock is held by reference — callers must ensure it outlives this object.
// Use common::FixedClock in tests for deterministic timestamps.
class EdgeProtocol final {
public:
    explicit EdgeProtocol(const common::IClock& clock) noexcept;

    // Builds the speech.config WebSocket frame.
    //
    // Frame structure:
    //   X-Timestamp:{js_date_string}\r\n
    //   Content-Type:application/json; charset=utf-8\r\n
    //   Path:speech.config\r\n
    //   \r\n
    //   {"context":{"synthesis":{"audio":{"metadataoptions":{...},"outputFormat":"..."}}}}
    //
    // Notes:
    //   - No X-RequestId header in the speech.config frame.
    //   - Timestamp has no trailing 'Z' (the SSML frame has it).
    //   - sentenceBoundaryEnabled / wordBoundaryEnabled are JSON strings
    //     ("true"/"false"), not JSON booleans.
    [[nodiscard]] common::Result<std::string> build_speech_config_frame(
        const core::TtsConfig&     config,
        const ConnectionMetadata&  metadata) const;

    // Builds the SSML WebSocket frame for one text chunk.
    //
    // Frame structure:
    //   X-RequestId:{metadata.request_id}\r\n
    //   Content-Type:application/ssml+xml\r\n
    //   X-Timestamp:{js_date_string}Z\r\n
    //   Path:ssml\r\n
    //   \r\n
    //   {ssml_body}
    //
    // Notes:
    //   - X-RequestId uses metadata.request_id (32-char hex, no hyphens).
    //   - Timestamp has a trailing 'Z' (service requirement).
    //   - text_chunk MUST be XML-escaped (e.g. output of serialization::TextChunker).
    //     SsmlBuilder::build_from_escaped_text embeds it verbatim — no second escape.
    //   - Do NOT pass raw (unescaped) text — special characters will produce malformed SSML.
    //   - Propagates any error from SsmlBuilder (invalid config).
    [[nodiscard]] common::Result<std::string> build_ssml_frame(
        const core::TtsConfig&     config,
        std::string_view           text_chunk,
        const ConnectionMetadata&  metadata) const;

    // Parses an incoming WebSocket message into zero or more typed events.
    //
    // Text frame dispatch (by Path header):
    //   "audio.metadata" → one IncomingMessage per BoundaryChunk
    //   "turn.end"       → single IncomingMessage{turn_end}
    //   "response"       → single IncomingMessage{ignored}   (silently ignored)
    //   "turn.start"     → single IncomingMessage{ignored}   (silently ignored)
    //   anything else    → protocol_error
    //
    // Binary frame dispatch:
    //   - First 2 bytes: big-endian uint16 = header_length (includes these 2 bytes)
    //   - Header content at bytes [2 .. header_length)
    //   - Separator \r\n at bytes [header_length .. header_length+2)
    //   - Body at bytes [header_length+2 ..)
    //   - Path must be "audio"
    //   - Content-Type must be "audio/mpeg" or absent
    //   - Absent Content-Type + empty body → IncomingMessage{ignored}
    //   - Absent Content-Type + non-empty body → protocol_error
    //   - "audio/mpeg" + empty body → protocol_error
    //   - "audio/mpeg" + non-empty body → IncomingMessage{audio, AudioChunk}
    //
    // Error cases (all return protocol_error):
    //   - Text frame: missing \r\n\r\n separator or malformed header
    //   - Text frame Path:audio.metadata: malformed JSON or unknown type
    //   - Text frame Path:audio.metadata: empty metadata (all SessionEnd entries)
    //   - Binary frame: fewer than 2 bytes
    //   - Binary frame: header_length > message size
    //   - Binary frame: Path != "audio"
    //   - Binary frame: unexpected Content-Type
    [[nodiscard]] common::Result<std::vector<IncomingMessage>> parse_incoming(
        const WebSocketMessage& message) const;

private:
    const common::IClock& clock_;

    // Formats a time point as a JavaScript-style UTC date string.
    // Format: "Mon Jan 01 2024 00:00:00 GMT+0000 (Coordinated Universal Time)"
    [[nodiscard]] static std::string format_js_timestamp(
        std::chrono::system_clock::time_point tp);
};

} // namespace edge_tts::communication
