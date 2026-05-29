#pragma once

#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/Result.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"

#include <string>

namespace edge_tts::communication {

// Generates the Sec-MS-GEC token and returns the Sec-MS-GEC-Version string.
//
// Algorithm (reference: drm.py DRM.generate_sec_ms_gec()):
//   1. Get UTC Unix timestamp (seconds, float) from the injected IClock.
//   2. Add Windows file time epoch offset: 11644473600 (WIN_EPOCH in drm.py).
//   3. Round DOWN to the nearest 300-second boundary (5 minutes).
//   4. Convert to 100-nanosecond intervals: multiply by 1e9 / 100 = 1e7.
//   5. Format as integer with "%.0f" (same as Python's f"{ticks:.0f}").
//   6. Concatenate with TRUSTED_CLIENT_TOKEN from config.
//   7. SHA-256 hash the ASCII string.
//   8. Return uppercase hex digest.
//
// sec_ms_gec_version() returns config.sec_ms_gec_version verbatim (no computation).
//
// The IClock is held by reference — callers must ensure the clock outlives
// the provider.  In tests, use common::FixedClock for deterministic output.
class EdgeTokenProvider {
public:
    EdgeTokenProvider(EdgeServiceConfig config, const common::IClock& clock);

    // Returns the Sec-MS-GEC header value for the current clock time.
    // Never fails with a FixedClock; Result<> is kept for API consistency.
    [[nodiscard]] common::Result<std::string> sec_ms_gec() const;

    // Returns the Sec-MS-GEC-Version header value from config.
    [[nodiscard]] std::string sec_ms_gec_version() const;

private:
    EdgeServiceConfig  config_;
    const common::IClock& clock_;
};

} // namespace edge_tts::communication
