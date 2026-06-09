#pragma once

#include "api/SynthesisOptions.hpp"
#include "common/Result.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace edge_tts::api {

// Callable type for the synthesis backend.
//
// Receives a validated TtsConfig and pre-chunked, XML-escaped text strings
// (output of serialization::TextChunker); returns all TtsChunk events.
//
// CONTRACT: strings in the span are already XML-escaped. The backend must embed
// them verbatim — no second XML-escaping. Inject a custom function in tests
// instead of a real SynthesisSession.
using SynthesizerFn = std::function<
    common::Result<std::vector<core::TtsChunk>>(
        const core::TtsConfig&,
        std::span<const std::string>)>;

// Public facade for the Edge TTS text-to-speech service.
//
// SpeechSynthesizer orchestrates:
//   - config validation   (core::validate_tts_config)
//   - text chunking       (serialization::TextChunker — normalize, escape, split)
//   - synthesis session   (communication::SynthesisSession via SynthesizerFn)
//   - subtitle generation (subtitles::SubMaker — optional, for save())
//   - file writing        (api::FileWriter)
//
// synthesize() and save() are each single-use. Calling either a second time
// returns ErrorCode::invalid_state.
//
// No protocol parsing, no WebSocket logic, no ffmpeg logic here.
class SpeechSynthesizer final {
public:
    // Production constructor: uses default transport options.
    // Builds the full networking stack at construction time but performs NO
    // network I/O.  All network work is deferred to synthesize() / save().
    explicit SpeechSynthesizer(std::string text, core::TtsConfig config = {});

    // Production constructor with explicit transport options (proxy, timeouts).
    // Speech configuration and transport options are kept separate:
    //   config  — what to say and how (voice, rate, volume, pitch)
    //   options — how to reach the service (proxy URL, connection timeouts)
    // Same lazy-networking guarantee as the 2-arg constructor above.
    SpeechSynthesizer(std::string text, core::TtsConfig config, SynthesisOptions options);

    // Test / dependency-injection constructor: synthesizer is called in place
    // of a real SynthesisSession.  Receives the validated TtsConfig and the
    // pre-chunked, XML-escaped text strings produced by serialization::TextChunker.
    // Uses default SynthesisOptions.
    SpeechSynthesizer(std::string text, core::TtsConfig config, SynthesizerFn synthesizer);

    // Test constructor with both explicit options and an injected synthesizer.
    // Use this to verify that options flow correctly into the synthesis path
    // (e.g. check that proxy/timeouts are accessible from the synthesizer seam).
    SpeechSynthesizer(std::string text, core::TtsConfig config,
                      SynthesisOptions options, SynthesizerFn synthesizer);

    [[nodiscard]] const std::string&       text()    const noexcept;
    [[nodiscard]] const core::TtsConfig&   config()  const noexcept;
    [[nodiscard]] const SynthesisOptions&  options() const noexcept;

    // Synthesize and return all TtsChunk events (AudioChunk + BoundaryChunk).
    // Single-use: returns ErrorCode::invalid_state if called more than once.
    [[nodiscard]] common::Result<std::vector<core::TtsChunk>> synthesize();

    // Synthesize, write audio to media_path, and optionally write SRT subtitles.
    // Single-use: returns ErrorCode::invalid_state if called more than once.
    [[nodiscard]] common::Result<void> save(
        const std::filesystem::path& media_path,
        std::optional<std::filesystem::path> subtitles_path = std::nullopt);

private:
    std::string       text_;
    core::TtsConfig   config_;
    SynthesisOptions  options_;
    SynthesizerFn     synthesizer_;
    bool              called_{false};

    // Shared synthesis pipeline: validate → chunk → synthesize.
    // Called by both synthesize() and save() after the single-use guard.
    [[nodiscard]] common::Result<std::vector<core::TtsChunk>> run_pipeline();
};

} // namespace edge_tts::api
