#include "communication/EdgeRequestHeaders.hpp"

#include <algorithm>
#include <cctype>

namespace edge_tts::communication {

// Generates the MUID cookie value: 32 uppercase hex chars prefixed as "muid=...;"
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
    return {
        {"User-Agent",      config.user_agent},
        {"Accept-Encoding", "gzip, deflate, br, zstd"},
        {"Accept-Language", "en-US,en;q=0.9"},
        {"Accept",          "*/*"},
        {"Cookie",          make_muid_cookie(ids)},
    };
}

} // namespace edge_tts::communication
