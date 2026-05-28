#include "edge_tts/serialization/EdgeProtocol.hpp"

namespace edge_tts::serialization {

std::string EdgeProtocol::build_ssml(const std::string& text, const core::TtsConfig& config) {
    return "<speak><voice name=\"" + config.voice + "\">" + text + "</voice></speak>";
}

std::string EdgeProtocol::build_speech_config() {
    return R"({"context":{"synthesis":{"audio":{"metadataoptions":{"sentenceBoundaryEnabled":true,"wordBoundaryEnabled":true},"outputFormat":"audio-24khz-48kbitrate-mono-mp3"}}}})";
}

} // namespace edge_tts::serialization
