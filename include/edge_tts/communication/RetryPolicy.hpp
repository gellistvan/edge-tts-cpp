#pragma once

#include "edge_tts/common/Error.hpp"

namespace edge_tts::communication {

// Per-chunk retry policy for the synthesis session.
//
// Reference: communicate.py Communicate.stream():
//
//   for self.state["partial_text"] in self.texts:
//       try:
//           async for message in self.__stream():
//               yield message
//       except aiohttp.ClientResponseError as e:
//           if e.status != 403:
//               raise
//           DRM.handle_client_response_error(e)   # adjust clock skew
//           async for message in self.__stream():  # single retry
//               yield message
//
// Exactly one retry per chunk is allowed, triggered only by a DRM token
// rejection (ErrorCode::drm_error, i.e. HTTP 403 during WebSocket upgrade).
// All other errors propagate immediately without retry.
struct RetryPolicy {
    // Maximum number of additional attempts after the first failure.
    // Reference: Python retries exactly once per chunk.
    int max_retries{1};

    // Returns true when error should trigger a retry.
    //
    // Reference: `if e.status != 403: raise` — only HTTP 403 is retried.
    // attempt is 0-based (0 = first attempt has already failed).
    [[nodiscard]] bool should_retry(const common::Error& error, int attempt) const noexcept;
};

} // namespace edge_tts::communication
