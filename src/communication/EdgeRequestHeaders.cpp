#include "edge_tts/communication/EdgeRequestHeaders.hpp"

// All string literals in this file are derived verbatim from the Python reference.
// Reference files:
//   constants.py  — BASE_HEADERS, WSS_HEADERS, VOICE_HEADERS
//   drm.py        — DRM.generate_muid() = secrets.token_hex(16).upper()
//                   DRM.headers_with_muid() adds Cookie: muid=<muid>;

#include <algorithm>
#include <cctype>

namespace edge_tts::communication {

// Generates the MUID cookie value matching Python DRM.generate_muid() +
// DRM.headers_with_muid() cookie format:
//   muid = secrets.token_hex(16).upper()   → 32 uppercase hex chars
//   Cookie: f"muid={muid};"
static std::string make_muid_cookie(common::IdGenerator& ids)
{
    std::string hex = ids.random_32_hex();
    std::transform(hex.begin(), hex.end(), hex.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return "muid=" + hex + ";";
}

std::vector<std::pair<std::string, std::string>>
build_websocket_headers(const EdgeServiceConfig& config, common::IdGenerator& ids)
{
    // Reference: communicate.py __stream()
    //   headers = DRM.headers_with_muid(WSS_HEADERS)
    //
    // WSS_HEADERS = {Pragma, Cache-Control, Origin} merged with BASE_HEADERS
    // BASE_HEADERS = {User-Agent, Accept-Encoding, Accept-Language}
    // headers_with_muid adds Cookie.
    return {
        {"Pragma",          "no-cache"},
        {"Cache-Control",   "no-cache"},
        {"Origin",          config.origin},
        {"User-Agent",      config.user_agent},
        {"Accept-Encoding", "gzip, deflate, br, zstd"},
        {"Accept-Language", "en-US,en;q=0.9"},
        {"Cookie",          make_muid_cookie(ids)},
    };
}

std::map<std::string, std::string>
build_voice_list_headers(const EdgeServiceConfig& config, common::IdGenerator& ids)
{
    // Reference: voices.py __list_voices()
    //   headers = DRM.headers_with_muid(VOICE_HEADERS)
    //
    // VOICE_HEADERS = {Accept} merged with BASE_HEADERS
    // BASE_HEADERS = {User-Agent, Accept-Encoding, Accept-Language}
    // headers_with_muid adds Cookie.
    return {
        {"User-Agent",      config.user_agent},
        {"Accept-Encoding", "gzip, deflate, br, zstd"},
        {"Accept-Language", "en-US,en;q=0.9"},
        {"Accept",          "*/*"},
        {"Cookie",          make_muid_cookie(ids)},
    };
}

} // namespace edge_tts::communication
