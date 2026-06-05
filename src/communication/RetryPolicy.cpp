#include "edge_tts/communication/RetryPolicy.hpp"
#include "edge_tts/common/Error.hpp"

namespace edge_tts::communication {

bool RetryPolicy::should_retry(const common::Error& error, int attempt) const noexcept
{
    // Reference: communicate.py `if e.status != 403: raise`
    // Only DRM errors (HTTP 403 during WebSocket upgrade) are retried.
    return error.code() == common::ErrorCode::drm_error
        && attempt < max_retries;
}

} // namespace edge_tts::communication
