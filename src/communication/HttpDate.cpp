#include "edge_tts/communication/HttpDate.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace edge_tts::communication {

namespace {

// Days since Unix epoch (1970-01-01 = 0) for the given Gregorian civil date.
// Howard Hinnant's public-domain algorithm; valid for y >= 1970.
std::int64_t days_from_civil(int y, int m, int d) noexcept
{
    y -= (m <= 2);
    const std::int64_t era = static_cast<std::int64_t>(y) / 400;
    const int yoe = y - static_cast<int>(era * 400);
    const int doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

} // namespace

std::optional<std::int64_t> parse_http_date(std::string_view date) noexcept
{
    if (date.empty())
        return std::nullopt;

    // Copy to a null-terminated buffer for sscanf.
    const std::string s{date};

    // Format: "Wkd, DD Mon YYYY HH:MM:SS GMT"
    // %*[^,]  — skip weekday (anything before the comma, no store)
    // ", "    — literal ", " separator
    // %d      — day-of-month
    // %3s     — 3-char month abbreviation
    // %d      — 4-digit year
    // %d:%d:%d — hour, minute, second
    // %3s     — timezone (accepted but not validated)
    char  mon[8]  = {};
    char  tz[8]   = {};
    int   day = 0, year = 0, hour = 0, min = 0, sec = 0;

    const int n = std::sscanf(s.c_str(),
        "%*[^,], %d %7s %d %d:%d:%d %7s",
        &day, mon, &year, &hour, &min, &sec, tz);

    if (n != 7)
        return std::nullopt;

    // Month name → 1-indexed number.
    static constexpr const char* kMonths[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int month = 0;
    for (int i = 0; i < 12; ++i) {
        if (std::strcmp(mon, kMonths[i]) == 0) {
            month = i + 1;
            break;
        }
    }
    if (month == 0)
        return std::nullopt;

    // Basic sanity checks (not exhaustive — we trust well-formed server dates).
    if (day  < 1 || day  > 31) return std::nullopt;
    if (hour < 0 || hour > 23) return std::nullopt;
    if (min  < 0 || min  > 59) return std::nullopt;
    if (sec  < 0 || sec  > 60) return std::nullopt; // 60: leap second

    const std::int64_t days  = days_from_civil(year, month, day);
    const std::int64_t ts    = days * 86400LL
                             + static_cast<std::int64_t>(hour) * 3600LL
                             + static_cast<std::int64_t>(min)  * 60LL
                             + static_cast<std::int64_t>(sec);
    return ts;
}

} // namespace edge_tts::communication
