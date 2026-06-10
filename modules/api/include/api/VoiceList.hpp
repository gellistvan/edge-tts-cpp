#pragma once

#include "api/SynthesisOptions.hpp"
#include "common/Result.hpp"
#include "core/Voice.hpp"

#include <vector>

namespace edge_tts::api {

// Fetch the list of voices available from the Edge TTS service.
//
// Makes a single HTTP GET request to the Edge TTS voice-list endpoint.
//
// Thread safety:
//   Not thread-safe.  Each call constructs its own internal state; concurrent
//   calls from different call sites are safe as long as they do not share a
//   single SynthesisOptions object while it is being modified.  The default
//   (no-arg) form is safe to call concurrently.
//
// Ownership:
//   The returned vector is owned by the caller.
//
// Error model:
//   Never throws for recoverable failures.  Error codes:
//     ErrorCode::network_error  — HTTP connection failed
//     ErrorCode::timeout        — HTTP request exceeded options.http_timeout
//     ErrorCode::service_error  — non-200 HTTP status from the service
//     ErrorCode::parse_error    — malformed JSON response body
//     ErrorCode::unsupported    — options.proxy is set (proxy not supported)
[[nodiscard]] common::Result<std::vector<core::Voice>>
list_voices(SynthesisOptions options = {});

} // namespace edge_tts::api
