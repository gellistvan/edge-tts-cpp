#pragma once

#include "edge_tts/common/Error.hpp"

namespace edge_tts::communication {

// Per-chunk retry policy for the synthesis session.
//
// Exactly one retry per chunk is allowed, triggered only by a DRM token
// rejection (ErrorCode::drm_error, i.e. HTTP 403 during WebSocket upgrade).
// All other errors propagate immediately without retry.
struct RetryPolicy {
    // Maximum number of additional attempts after the first failure.
    int max_retries{1};

    // Returns true when the error should trigger a retry.
    // attempt is 0-based (0 = first attempt has already failed).
    // Only drm_error (HTTP 403) is retried; all other errors return false.
    [[nodiscard]] bool should_retry(const common::Error& error, int attempt) const noexcept;
};

} // namespace edge_tts::communication
