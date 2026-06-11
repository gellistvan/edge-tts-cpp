#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace edge_tts::communication {

// Parse an RFC 2616 / RFC 1123 HTTP-date string into a UTC Unix timestamp.
//
// Accepted format: "Wkd, DD Mon YYYY HH:MM:SS GMT"
// Examples: "Mon, 15 Jan 2024 08:31:15 GMT"
//           "Thu, 01 Jan 1970 00:00:00 GMT"
//
// Returns std::nullopt if the string is empty, structurally malformed, or
// contains an unknown month abbreviation.  The weekday and timezone fields
// are accepted but not validated.
//
[[nodiscard]] std::optional<std::int64_t>
parse_http_date(std::string_view date) noexcept;

} // namespace edge_tts::communication
