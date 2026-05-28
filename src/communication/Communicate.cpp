#include "edge_tts/communication/Communicate.hpp"
#include "edge_tts/common/Errors.hpp"

#include <fstream>
#include <utility>

namespace edge_tts::communication {

Communicate::Communicate(std::string text, core::TtsConfig config)
    : text_(std::move(text)), config_(std::move(config)) {
    config_.validate();
}

const std::string& Communicate::text() const noexcept { return text_; }
const core::TtsConfig& Communicate::config() const noexcept { return config_; }

std::vector<core::TtsChunk> Communicate::stream_sync() const {
    // Placeholder: real implementation will coordinate core, serialization, and communication modules.
    return {};
}

void Communicate::save(const std::filesystem::path& media_path,
                       const std::optional<std::filesystem::path>& subtitles_path) const {
    std::ofstream media(media_path, std::ios::binary);
    if (!media) {
        throw common::Error{"Failed to open media output file"};
    }
    media << "";

    if (subtitles_path.has_value()) {
        std::ofstream subtitles(*subtitles_path);
        if (!subtitles) {
            throw common::Error{"Failed to open subtitle output file"};
        }
        subtitles << "";
    }
}

} // namespace edge_tts::communication
