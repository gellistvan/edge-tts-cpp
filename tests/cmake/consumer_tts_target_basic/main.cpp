#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "core/TtsConfig.hpp"

int main() {
    // Construct a TtsConfig and SynthesisOptions — no network I/O.
    edge_tts::core::TtsConfig cfg;
    cfg.voice = "en-US-EmmaMultilingualNeural";

    edge_tts::api::SynthesisOptions opts;

    // Construct SpeechSynthesizer — the public API entry point.
    // No network call happens at construction time.
    edge_tts::api::SpeechSynthesizer c("Hello, world!", std::move(cfg), std::move(opts));
    (void)c;
    return 0;
}
