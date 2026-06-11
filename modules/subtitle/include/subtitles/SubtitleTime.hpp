#pragma once

#include "common/Result.hpp"

#include <cstdint>
#include <string>

namespace edge_tts::subtitles {

// Represents an SRT subtitle timestamp derived from Edge TTS 100 ns ticks.
//
// Conversion: milliseconds = ticks / 10_000 (integer truncation).
//   A 1 ms rounding difference is only possible when ticks % 10_000 >= 9_995.
//
// SRT timestamp format: "HH:MM:SS,mmm" (comma separator, zero-padded).
//
// Negative ticks are rejected (negative start times are always skipped in SRT output).
class SubtitleTime {
    std::int64_t millis_; // total milliseconds (non-negative)

    explicit constexpr SubtitleTime(std::int64_t ms) noexcept : millis_(ms) {}

public:
    // Default-constructs to time zero (0 ms), allowing SubtitleCue to be
    // aggregate-initialized and stored in containers without a factory call.
    constexpr SubtitleTime() noexcept : millis_(0) {}
    // Constructs from Edge TTS 100 ns ticks.
    // Returns ErrorCode::invalid_argument for negative ticks.
    [[nodiscard]] static common::Result<SubtitleTime>
    from_edge_ticks(std::int64_t ticks);

    // Returns the SRT timestamp string: "HH:MM:SS,mmm".
    // Hours field is not capped — values ≥ 100 hours produce ≥ 3-digit hours.
    [[nodiscard]] std::string to_srt_timestamp() const;

    // Returns total milliseconds.
    [[nodiscard]] constexpr std::int64_t milliseconds() const noexcept { return millis_; }
};

} // namespace edge_tts::subtitles
