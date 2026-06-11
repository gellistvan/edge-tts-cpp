#pragma once

#include <string>
#include <string_view>

namespace edge_tts::common {

// Categorises the kind of failure stored in a Result<T>.
enum class ErrorCode {
    none,                   // no error (should not appear in a failure Result)
    invalid_argument,       // bad caller input (wrong type, value out of range)
    invalid_state,          // operation not allowed in the current object state
    io_error,               // local file or pipe failure
    network_error,          // transport failure (WebSocket, HTTP)
    protocol_error,         // unexpected wire message shape or unknown path
    parse_error,            // could not decode JSON or headers
    timeout,                // connect or receive timeout exceeded
    unsupported,            // operation unsupported on this platform
    external_process_failed,// ffmpeg/ffplay subprocess failure
    service_error,          // remote service refused or produced no audio data
    drm_error,              // DRM token rejected (HTTP 403 during WebSocket upgrade); triggers one retry
    cancelled,              // operation was cancelled via SpeechSynthesizer::cancel()
};

// Returns a short lowercase name for the code (e.g. "network_error").
// The returned string_view points to a string literal — no allocation.
[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

// Value-type error carrier used with Result<T>.
// Stores an ErrorCode, a human-readable message, and an optional context
// string (e.g. URL, filename, protocol path) for diagnostic use.
class Error {
    ErrorCode   code_;
    std::string message_;
    std::string context_;

public:
    Error(ErrorCode code, std::string message);
    Error(ErrorCode code, std::string message, std::string context);

    [[nodiscard]] ErrorCode      code()        const noexcept;
    [[nodiscard]] std::string_view message()   const noexcept;
    [[nodiscard]] std::string_view context()   const noexcept;
    [[nodiscard]] bool             has_context() const noexcept;

    // Returns a single formatted string: "[code] message (context: ...)"
    // Allocates — use for logging/display only, not in hot paths.
    [[nodiscard]] std::string what() const;
};

} // namespace edge_tts::common
