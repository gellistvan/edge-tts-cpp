#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace edge_tts::core {

class TextChunker final {
public:
    explicit TextChunker(std::size_t max_bytes = 4096);
    [[nodiscard]] std::vector<std::string> split(const std::string& text) const;

private:
    std::size_t max_bytes_;
};

} // namespace edge_tts::core
