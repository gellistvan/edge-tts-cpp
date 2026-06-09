#include <edge_tts/edge_tts.hpp>

// Use the public API surface without making any network calls.
// This is intentionally minimal — the goal is compile + link correctness,
// not runtime behavior.
int main() {
    edge_tts::core::TtsConfig cfg;
    cfg.voice = "en-US-EmmaMultilingualNeural";

    edge_tts::api::CommunicateOptions opts;

    // Constructing Communicate does not open a network connection.
    edge_tts::api::Communicate c("Hello, world!", std::move(cfg), std::move(opts));
    (void)c;
    return 0;
}
