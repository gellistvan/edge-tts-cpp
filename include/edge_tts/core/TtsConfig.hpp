#pragma once

#include "edge_tts/core/TtsChunk.hpp"

#include <string>

namespace edge_tts::core {

struct TtsConfig {
    std::string voice{"en-US-EmmaMultilingualNeural"};
    std::string rate{"+0%"};
    std::string volume{"+0%"};
    std::string pitch{"+0Hz"};
    BoundaryType boundary{BoundaryType::SentenceBoundary};

    void validate() const;
};

} // namespace edge_tts::core
