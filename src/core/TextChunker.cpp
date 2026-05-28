#include "edge_tts/core/TextChunker.hpp"

#include <stdexcept>

namespace edge_tts::core {

TextChunker::TextChunker(std::size_t max_bytes) : max_bytes_(max_bytes) {
    if (max_bytes_ == 0) {
        throw std::invalid_argument{"max_bytes must be greater than zero"};
    }
}

std::vector<std::string> TextChunker::split(const std::string& text) const {
    if (text.empty()) {
        return {};
    }

    std::vector<std::string> chunks;
    for (std::size_t pos = 0; pos < text.size(); pos += max_bytes_) {
        chunks.push_back(text.substr(pos, max_bytes_));
    }
    return chunks;
}

} // namespace edge_tts::core
