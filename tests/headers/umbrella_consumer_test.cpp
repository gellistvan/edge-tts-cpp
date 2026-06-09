// Verifies that <edge_tts/edge_tts.hpp> is the only include needed for basic
// TTS library usage.  This file must not include any other edge_tts header
// directly — all symbols must come through the umbrella.
#include <edge_tts/edge_tts.hpp>

#include "vendor/minigtest/minigtest.hpp"

TEST(UmbrellaHeader, TtsConfigConstructsWithoutNetworkIO) {
    edge_tts::core::TtsConfig cfg;
    cfg.voice = "en-US-EmmaMultilingualNeural";
    EXPECT_EQ(cfg.voice, "en-US-EmmaMultilingualNeural");
}

TEST(UmbrellaHeader, CommunicateOptionsDefaultsAvailable) {
    edge_tts::api::SynthesisOptions opts;
    // Default proxy is absent.
    EXPECT_FALSE(opts.proxy.has_value());
}

TEST(UmbrellaHeader, CommunicateConstructsWithoutNetworkIO) {
    edge_tts::core::TtsConfig cfg;
    cfg.voice = "en-US-EmmaMultilingualNeural";
    edge_tts::api::SynthesisOptions opts;
    // Construction must not perform any I/O.
    edge_tts::api::SpeechSynthesizer c("Hello", std::move(cfg), std::move(opts));
    (void)c;
    EXPECT_TRUE(true);
}

TEST(UmbrellaHeader, ErrorCodeAccessible) {
    // ErrorCode must be reachable from the umbrella header.
    auto code = edge_tts::common::ErrorCode::network_error;
    EXPECT_TRUE(code != edge_tts::common::ErrorCode::none);
}

TEST(UmbrellaHeader, VoiceTypeAccessible) {
    edge_tts::core::Voice v;
    (void)v;
    EXPECT_TRUE(true);
}
