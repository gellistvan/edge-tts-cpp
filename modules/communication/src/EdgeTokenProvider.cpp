#include "communication/EdgeTokenProvider.hpp"
#include "common/Sha256.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace edge_tts::communication {

EdgeTokenProvider::EdgeTokenProvider(EdgeServiceConfig config,
                                     const common::IClock& clock)
    : config_(std::move(config))
    , clock_(clock)
{}

common::Result<std::string> EdgeTokenProvider::sec_ms_gec() const
{
    // Step 1: Get UTC Unix timestamp (seconds as double) + accumulated skew,
    //   matching Python's DRM.get_unix_timestamp():
    //     dt.now(tz.utc).timestamp() + DRM.clock_skew_seconds
    const auto now = clock_.now();
    const double unix_seconds = std::chrono::duration<double>(
        now.time_since_epoch()).count() + clock_skew_seconds_;

    // Step 2: Add Windows file time epoch offset (drm.py: WIN_EPOCH = 11644473600)
    double ticks = unix_seconds + 11644473600.0;

    // Step 3: Round DOWN to nearest 300-second boundary
    //   Python: ticks -= ticks % 300
    ticks -= std::fmod(ticks, 300.0);

    // Step 4: Convert to 100-nanosecond intervals
    //   Python: ticks *= S_TO_NS / 100  where S_TO_NS = 1e9
    ticks *= 1e9 / 100.0;

    // Step 5: Format as integer (same rounding as Python's f"{ticks:.0f}")
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f", ticks);

    // Step 6: Concatenate with trusted client token
    const std::string str_to_hash = std::string(buf) + config_.trusted_client_token;

    // Step 7+8: SHA-256 → uppercase hex
    //   Python: hashlib.sha256(str_to_hash.encode("ascii")).hexdigest().upper()
    return common::Result<std::string>::ok(common::sha256_hex_upper(str_to_hash));
}

std::string EdgeTokenProvider::sec_ms_gec_version() const
{
    return config_.sec_ms_gec_version;
}

void EdgeTokenProvider::adjust_clock_skew(double seconds) noexcept
{
    clock_skew_seconds_ += seconds;
}

void EdgeTokenProvider::adjust_clock_skew_from_server_timestamp(
    double server_unix_seconds) noexcept
{
    const double now_sec = std::chrono::duration<double>(
        clock_.now().time_since_epoch()).count();
    adjust_clock_skew(server_unix_seconds - (now_sec + clock_skew_seconds_));
}

double EdgeTokenProvider::clock_skew_seconds() const noexcept
{
    return clock_skew_seconds_;
}

} // namespace edge_tts::communication
