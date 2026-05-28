#pragma once

#include "edge_tts/core/TtsConfig.hpp"

#include <string>

namespace edge_tts::serialization {

class EdgeProtocol final {
public:
    [[nodiscard]] static std::string build_ssml(const std::string& text, const core::TtsConfig& config);
    [[nodiscard]] static std::string build_speech_config();
};

} // namespace edge_tts::serialization
