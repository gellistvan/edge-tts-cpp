#include "edge_tts/common/Clock.hpp"

namespace edge_tts::common {

// Anchor IClock's vtable in this translation unit.
IClock::~IClock() = default;

// ---------------------------------------------------------------------------
// SystemClock
// ---------------------------------------------------------------------------

std::chrono::system_clock::time_point SystemClock::now() const {
    return std::chrono::system_clock::now();
}

// ---------------------------------------------------------------------------
// FixedClock
// ---------------------------------------------------------------------------

FixedClock::FixedClock(std::chrono::system_clock::time_point value) noexcept
    : value_(value) {}

std::chrono::system_clock::time_point FixedClock::now() const {
    return value_;
}

void FixedClock::set(std::chrono::system_clock::time_point value) noexcept {
    value_ = value;
}

} // namespace edge_tts::common
