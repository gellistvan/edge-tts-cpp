#pragma once

#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/Result.hpp"
#include "edge_tts/communication/ConnectionMetadata.hpp"
#include "edge_tts/core/TtsConfig.hpp"

#include <chrono>
#include <string>

namespace edge_tts::communication {

// Builds outgoing WebSocket text frames for the Edge TTS protocol.
//
// Reference: communicate.py send_command_request(), ssml_headers_plus_data()
//
// The IClock is held by reference — callers must ensure it outlives this object.
// Use common::FixedClock in tests for deterministic timestamps.
class EdgeProtocol final {
public:
    explicit EdgeProtocol(const common::IClock& clock) noexcept;

    // Builds the speech.config WebSocket frame.
    //
    // Reference: communicate.py send_command_request()
    //
    // Frame structure:
    //   X-Timestamp:{js_date_string}\r\n
    //   Content-Type:application/json; charset=utf-8\r\n
    //   Path:speech.config\r\n
    //   \r\n
    //   {"context":{"synthesis":{"audio":{"metadataoptions":{...},"outputFormat":"..."}}}}
    //
    // Notes:
    //   - NO X-RequestId header (metadata.request_id is accepted for API
    //     consistency but unused — the Python reference omits it here).
    //   - Timestamp has NO trailing 'Z' (only the SSML frame has it).
    //   - sentenceBoundaryEnabled / wordBoundaryEnabled are JSON strings
    //     ("true"/"false"), not JSON booleans — matches the Python reference.
    [[nodiscard]] common::Result<std::string> build_speech_config_frame(
        const core::TtsConfig&     config,
        const ConnectionMetadata&  metadata) const;

    // Builds the SSML WebSocket frame for one text chunk.
    //
    // Reference: communicate.py ssml_headers_plus_data() + send_ssml_request()
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
    //   - Timestamp has a trailing 'Z' — documented as a Microsoft Edge bug.
    //   - text_chunk is passed raw; SsmlBuilder normalizes and XML-escapes it.
    //   - Do NOT pre-escape text_chunk — it will be double-escaped.
    //   - Propagates any error from SsmlBuilder (invalid config, bad UTF-8).
    [[nodiscard]] common::Result<std::string> build_ssml_frame(
        const core::TtsConfig&     config,
        std::string_view           text_chunk,
        const ConnectionMetadata&  metadata) const;

private:
    const common::IClock& clock_;

    // Formats a time point as a JavaScript-style UTC date string.
    // Reference: communicate.py date_to_string()
    // Format: "Mon Jan 01 2024 00:00:00 GMT+0000 (Coordinated Universal Time)"
    [[nodiscard]] static std::string format_js_timestamp(
        std::chrono::system_clock::time_point tp);
};

} // namespace edge_tts::communication
