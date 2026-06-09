#pragma once

// edge_tts/edge_tts.hpp — recommended public include for TTS consumers.
//
// This header exposes the complete stable public API needed for basic TTS usage:
//   - SpeechSynthesizer — synthesize text to audio
//   - TtsConfig         — voice, rate, volume, pitch
//   - Voice             — voice listing type
//   - Result<T>         — error propagation
//   - ErrorCode         — error categories
//
// Usage:
//   #include <edge_tts/edge_tts.hpp>
//   ...
//   edge_tts::core::TtsConfig cfg;
//   cfg.voice = "en-US-EmmaMultilingualNeural";
//   edge_tts::api::SpeechSynthesizer s("Hello, world!", std::move(cfg));
//   auto result = s.save("out.mp3");
//
// Link against edge_tts::tts in your CMakeLists.txt.
//
// NOT included here (available via direct includes if needed):
//   - CLI headers (edge_tts/cli/)
//   - Media/playback headers (edge_tts/media/)
//   - Internal protocol/transport headers (edge_tts/communication/, edge_tts/serialization/)
//   - Test utilities (edge_tts_test_support)

#include "edge_tts/version.hpp"
#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "api/FileWriter.hpp"
#include "common/Error.hpp"
#include "common/Result.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "core/Voice.hpp"
