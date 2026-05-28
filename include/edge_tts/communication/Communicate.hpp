#pragma once

#include "edge_tts/core/TtsChunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace edge_tts::communication {

class Communicate final {
public:
    Communicate(std::string text, core::TtsConfig config = {});

    [[nodiscard]] const std::string& text() const noexcept;
    [[nodiscard]] const core::TtsConfig& config() const noexcept;

    [[nodiscard]] std::vector<core::TtsChunk> stream_sync() const;

    void save(const std::filesystem::path& media_path,
              const std::optional<std::filesystem::path>& subtitles_path = std::nullopt) const;

private:
    std::string text_;
    core::TtsConfig config_;
};

} // namespace edge_tts::communication
