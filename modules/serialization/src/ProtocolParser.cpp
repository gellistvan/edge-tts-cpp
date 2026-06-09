#include "serialization/ProtocolParser.hpp"
#include "common/Error.hpp"

#include <string>
#include <string_view>

namespace edge_tts::serialization {

static constexpr std::string_view k_separator = "\r\n\r\n";
static constexpr std::string_view k_crlf      = "\r\n";
static constexpr char             k_colon      = ':';

common::Result<ProtocolMessage> ProtocolParser::parse(std::string_view frame) const
{
    // Locate the header/body separator.
    const auto sep_pos = frame.find(k_separator);
    if (sep_pos == std::string_view::npos)
        return common::Result<ProtocolMessage>::fail(
            {common::ErrorCode::parse_error,
             "protocol frame missing \\r\\n\\r\\n header/body separator"});

    const std::string_view header_section = frame.substr(0, sep_pos);
    const std::string_view body           = frame.substr(sep_pos + k_separator.size());

    ProtocolMessage msg;
    msg.body = std::string{body};

    // Parse header lines, splitting on \r\n.
    std::string_view remaining = header_section;
    while (!remaining.empty()) {
        std::string_view line;
        const auto crlf_pos = remaining.find(k_crlf);
        if (crlf_pos == std::string_view::npos) {
            line      = remaining;
            remaining = {};
        } else {
            line      = remaining.substr(0, crlf_pos);
            remaining = remaining.substr(crlf_pos + k_crlf.size());
        }

        if (line.empty()) continue; // skip any spurious blank lines

        const auto colon_pos = line.find(k_colon);
        if (colon_pos == std::string_view::npos)
            return common::Result<ProtocolMessage>::fail(
                {common::ErrorCode::parse_error,
                 "malformed protocol header line — missing ':'",
                 std::string{line}});

        std::string name{line.substr(0, colon_pos)};
        std::string value{line.substr(colon_pos + 1)};
        msg.headers.emplace_back(std::move(name), std::move(value));
    }

    return common::Result<ProtocolMessage>::ok(std::move(msg));
}

} // namespace edge_tts::serialization
