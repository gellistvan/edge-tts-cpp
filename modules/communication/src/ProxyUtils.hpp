#pragma once

// Internal utility — only include from .cpp files in the communication module.
// Do NOT include from public headers or from outside the communication module.

#include <string>
#include <string_view>

namespace edge_tts::communication::internal {

// Replace "user:password@" credentials in a proxy URL with "[credentials]"
// so the URL is safe to embed in error messages and log output.
// Returns the URL unchanged when no credentials are present.
//
// Examples:
//   "http://user:pass@host:8080"  →  "http://[credentials]@host:8080"
//   "http://host:8080"            →  "http://host:8080"  (no-op)
//   "badurl"                      →  "badurl"            (no scheme → no-op)
inline std::string sanitize_proxy_url(std::string_view url)
{
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) return std::string(url);
    const auto auth_start = scheme_end + 3;
    const auto at_pos     = url.find('@', auth_start);
    if (at_pos == std::string_view::npos) return std::string(url);
    std::string out;
    out.reserve(url.size());
    out.append(url.data(), auth_start);
    out.append("[credentials]");
    out.append(url.data() + at_pos, url.size() - at_pos);
    return out;
}

} // namespace edge_tts::communication::internal
