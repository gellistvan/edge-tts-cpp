#pragma once

#include <string>
#include <string_view>

namespace edge_tts::common {

// Computes the SHA-256 digest of data and returns it as a 64-character lowercase hex string.
// Reference algorithm: FIPS 180-4 (SHA-256).
// Used by EdgeTokenProvider to generate the Sec-MS-GEC token.
[[nodiscard]] std::string sha256_hex(std::string_view data);

// Same as sha256_hex but returns uppercase hex — matches Python's
//   hashlib.sha256(s.encode("ascii")).hexdigest().upper()
// used in drm.py DRM.generate_sec_ms_gec().
[[nodiscard]] std::string sha256_hex_upper(std::string_view data);

} // namespace edge_tts::common
