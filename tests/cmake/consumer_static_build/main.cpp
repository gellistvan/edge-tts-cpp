#include <edge_tts/edge_tts.hpp>

int main() {
    edge_tts::core::TtsConfig cfg;
    cfg.voice = "en-US-EmmaMultilingualNeural";

    edge_tts::api::SynthesisOptions opts;

    edge_tts::api::SpeechSynthesizer c("Hello, world!", std::move(cfg), std::move(opts));
    (void)c;
    return 0;
}
