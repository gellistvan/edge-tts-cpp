#include "edge_tts/core/TextChunker.hpp"
#include "edge_tts/common/Utf8.hpp"

#include <stdexcept>

namespace edge_tts::core {

TextChunker::TextChunker(std::size_t max_bytes) : max_bytes_(max_bytes) {
    if (max_bytes_ == 0) {
        throw std::invalid_argument{"max_bytes must be greater than zero"};
    }
}

std::vector<std::string> TextChunker::split(const std::string& text) const {
    if (text.empty()) return {};

    std::vector<std::string> chunks;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = pos + max_bytes_;
        if (end < text.size()) {
            // Step back to a UTF-8 code-point boundary so we never slice a
            // multi-byte sequence in two.
            const std::size_t boundary = common::utf8::safe_boundary(text, end);
            end = (boundary > pos) ? boundary : end;
        } else {
            end = text.size();
        }
        chunks.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return chunks;
}

} // namespace edge_tts::core
