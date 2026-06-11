#include "subtitles/SubtitleTime.hpp"
#include "common/Error.hpp"

#include <cstdio>
#include <string>

namespace edge_tts::subtitles {

common::Result<SubtitleTime> SubtitleTime::from_edge_ticks(std::int64_t ticks)
{
    if (ticks < 0)
        return common::Result<SubtitleTime>::fail(
            {common::ErrorCode::invalid_argument,
             "edge ticks must be non-negative for a valid SRT timestamp"});

    // milliseconds = ticks / 10 (to microseconds) / 1000 (to milliseconds) = ticks / 10000
    return common::Result<SubtitleTime>::ok(SubtitleTime{ticks / 10'000});
}

std::string SubtitleTime::to_srt_timestamp() const
{
    const std::int64_t hrs  = millis_ / 3'600'000;
    const std::int64_t mins = (millis_ % 3'600'000) / 60'000;
    const std::int64_t secs = (millis_ % 60'000)    / 1'000;
    const std::int64_t ms   = millis_ % 1'000;

    // Format: "HH:MM:SS,mmm" (hours ≥ 100 expand naturally beyond two digits)
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld,%03lld",
                  static_cast<long long>(hrs),
                  static_cast<long long>(mins),
                  static_cast<long long>(secs),
                  static_cast<long long>(ms));
    return std::string{buf};
}

} // namespace edge_tts::subtitles
