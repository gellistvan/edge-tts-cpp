#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"
#include "edge_tts/core/TtsConfig.hpp"

int main() {
    // Construct a TtsConfig and CommunicateOptions — no network I/O.
    edge_tts::core::TtsConfig cfg;
    cfg.voice = "en-US-EmmaMultilingualNeural";

    edge_tts::api::CommunicateOptions opts;

    // Construct Communicate — the public API entry point.
    // No network call happens at construction time.
    edge_tts::api::Communicate c("Hello, world!", std::move(cfg), std::move(opts));
    (void)c;
    return 0;
}
