#include "edge_tts/serialization/TextNormalizer.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/common/Utf8.hpp"

namespace edge_tts::serialization {

common::Result<std::string> TextNormalizer::normalize(std::string_view input) const {
    if (!common::utf8::is_valid_utf8(input)) {
        return common::Result<std::string>::fail(
            common::Error{common::ErrorCode::invalid_argument,
                          "TextNormalizer: invalid UTF-8 input"});
    }

    std::string out;
    out.reserve(input.size());

    std::size_t i = 0;
    while (i < input.size()) {
        const auto byte = static_cast<unsigned char>(input[i]);

        if (byte < 0x80u) {
            // ASCII range.
            // Replace: U+0000-U+0008, U+000B-U+000C, U+000E-U+001F
            // Preserve: U+0009 (tab), U+000A (LF), U+000D (CR), U+0020+
            if ((byte <= 8u) || (byte == 11u) || (byte == 12u) ||
                (byte >= 14u && byte <= 31u)) {
                out += ' ';
            } else {
                out += input[i];
            }
            ++i;
        } else {
            // Multi-byte UTF-8 sequence — already validated, copy intact.
            const std::size_t next = common::utf8::next_code_point_boundary(input, i);
            out.append(input.data() + i, next - i);
            i = next;
        }
    }

    return common::Result<std::string>::ok(std::move(out));
}

} // namespace edge_tts::serialization
