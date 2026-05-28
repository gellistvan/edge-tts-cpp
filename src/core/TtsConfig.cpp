#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/common/Errors.hpp"

namespace edge_tts::core {

void TtsConfig::validate() const {
    if (voice.empty()) {
        throw common::ConfigurationError{"voice must not be empty"};
    }
    if (rate.empty() || volume.empty() || pitch.empty()) {
        throw common::ConfigurationError{"rate, volume, and pitch must not be empty"};
    }
}

} // namespace edge_tts::core
