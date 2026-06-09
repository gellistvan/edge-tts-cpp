#include "serialization/XmlEscaper.hpp"

namespace edge_tts::serialization {

std::string xml_escape(std::string_view raw) {
    std::string out;
    out.reserve(raw.size() + raw.size() / 8);  // rough pre-alloc
    for (const char c : raw) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            default:  out += c;       break;
        }
    }
    return out;
}

std::string xml_unescape(std::string_view escaped) {
    std::string out;
    out.reserve(escaped.size());
    std::size_t i = 0;
    while (i < escaped.size()) {
        if (escaped[i] != '&') {
            out += escaped[i++];
            continue;
        }
        // Scan for the closing semicolon within a reasonable window.
        const std::size_t semi = escaped.find(';', i + 1);
        if (semi == std::string_view::npos || semi - i > 8) {
            // No closing ';' found nearby — treat '&' as literal.
            out += escaped[i++];
            continue;
        }
        const auto entity = escaped.substr(i, semi - i + 1);  // includes & and ;
        if      (entity == "&amp;")  { out += '&'; }
        else if (entity == "&lt;")   { out += '<'; }
        else if (entity == "&gt;")   { out += '>'; }
        else if (entity == "&quot;") { out += '"'; }
        else if (entity == "&apos;") { out += '\''; }
        else {
            // Unknown entity — emit unchanged.
            out += entity;
        }
        i = semi + 1;
    }
    return out;
}

} // namespace edge_tts::serialization
