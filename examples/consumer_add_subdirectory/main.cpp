// consumer_add_subdirectory example — minimal edge-tts-cpp consumer.
//
// This file demonstrates the public API surface available after linking
// edge_tts::tts.  Include the umbrella header; it exposes Communicate,
// TtsConfig, CommunicateOptions, Voice, Result<T>, and ErrorCode.
//
// Build instructions: see CMakeLists.txt in this directory.

#include <edge_tts/edge_tts.hpp>

#include <iostream>

int main() {
    // ── Configure voice and speech parameters ─────────────────────────────
    edge_tts::core::TtsConfig cfg;
    cfg.voice  = "en-US-EmmaMultilingualNeural"; // required: short voice name
    cfg.rate   = "+0%";   // speech rate delta  (e.g. "+25%", "-10%")
    cfg.volume = "+0%";   // volume delta        (e.g. "+50%")
    cfg.pitch  = "+0Hz";  // pitch delta         (e.g. "+5Hz")

    // ── Transport options ─────────────────────────────────────────────────
    // NOTE: proxy is not supported by the ixwebsocket backend.
    // Setting opts.proxy will cause save()/stream_sync() to return
    // ErrorCode::unsupported.
    edge_tts::api::CommunicateOptions opts;
    // opts.connect_timeout = std::chrono::seconds{15};
    // opts.receive_timeout = std::chrono::seconds{60};

    // ── Create the TTS session ────────────────────────────────────────────
    // Construction is cheap — no network connection is opened here.
    edge_tts::api::Communicate tts("Hello, world!", std::move(cfg), std::move(opts));

    // ── Synthesize speech (requires internet access) ──────────────────────
    // Uncomment the block below to actually produce audio.  The call connects
    // to speech.platform.bing.com over TLS and streams back MP3 data.
    //
    // auto result = tts.save("hello.mp3", "hello.srt");
    // if (!result) {
    //     std::cerr << "Synthesis failed: " << result.error().what() << '\n';
    //     return 1;
    // }
    // std::cout << "Saved hello.mp3 (and hello.srt if subtitles were requested)\n";
    //
    // Alternatively, stream chunks directly:
    //
    // auto chunks = tts.stream_sync();
    // if (!chunks) {
    //     std::cerr << "Stream failed: " << chunks.error().what() << '\n';
    //     return 1;
    // }
    // for (const auto& chunk : *chunks) {
    //     if (edge_tts::core::is_audio(chunk)) {
    //         const auto& audio = std::get<edge_tts::core::AudioChunk>(chunk);
    //         // write audio.data to a file or audio device
    //     }
    // }

    std::cout << "edge-tts-cpp consumer_add_subdirectory example: OK\n";
    return 0;
}
