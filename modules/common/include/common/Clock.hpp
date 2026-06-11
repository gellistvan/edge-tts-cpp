#pragma once

#include <chrono>

namespace edge_tts::common {

// Injectable time source.  All time points are in UTC (std::chrono::system_clock
// epoch = 1970-01-01 00:00:00 UTC).
//
// Protocol-specific conversions (Unix → Windows file time epoch, 100 ns ticks,
// JavaScript date string formatting) are performed at the call site in the
// serialization / communication layer — NOT here.
class IClock {
public:
    virtual ~IClock();

    // Returns the current UTC time point.
    [[nodiscard]] virtual std::chrono::system_clock::time_point now() const = 0;
};

// Production clock backed by std::chrono::system_clock.
class SystemClock final : public IClock {
public:
    [[nodiscard]] std::chrono::system_clock::time_point now() const override;
};

// Test-double clock that returns a fixed (or manually updated) time point.
// Useful for deterministic token generation and timestamp tests.
class FixedClock final : public IClock {
public:
    explicit FixedClock(std::chrono::system_clock::time_point value) noexcept;

    [[nodiscard]] std::chrono::system_clock::time_point now() const override;

    // Advance or rewind the fixed value, e.g. to simulate clock skew correction.
    void set(std::chrono::system_clock::time_point value) noexcept;

private:
    std::chrono::system_clock::time_point value_;
};

} // namespace edge_tts::common
