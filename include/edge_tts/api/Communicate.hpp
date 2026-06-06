#pragma once

#include "edge_tts/api/CommunicateOptions.hpp"
#include "edge_tts/common/Result.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace edge_tts::api {

// Synthesizer function type: runs synthesis for the given TtsConfig and
// pre-chunked, XML-escaped text strings (output of serialization::TextChunker);
// returns all TtsChunk events.
//
// CONTRACT: the strings in the span are already XML-escaped.  The synthesizer
// (and the EdgeProtocol layer below it) must embed them verbatim — no second
// XML-escaping.  Inject a custom function in tests instead of a real
// SynthesisSession.
using SynthesizerFn = std::function<
    common::Result<std::vector<core::TtsChunk>>(
        const core::TtsConfig&,
        std::span<const std::string>)>;

// Public facade for the Edge TTS text-to-speech service.
//
// Reference: communicate.py Communicate class, __init__.py public exports
//
// Communicate orchestrates:
//   - config validation   (core::validate_tts_config)
//   - text chunking       (serialization::TextChunker — normalize, escape, split)
//   - synthesis session   (communication::SynthesisSession via SynthesizerFn)
//   - subtitle generation (subtitles::SubMaker — optional, for save())
//   - file writing        (api::FileWriter)
//
// stream_sync() and save() are each single-use (reference: Communicate.stream()
// raises RuntimeError on a second call). Calling either a second time returns
// ErrorCode::invalid_state.
//
// No protocol parsing, no WebSocket logic, no ffmpeg logic here.
class Communicate final {
public:
    // Production constructor: uses default transport options.
    // Real synthesis requires a functional WebSocket transport; until that is
    // wired, stream_sync() and save() return ErrorCode::network_error.
    explicit Communicate(std::string text, core::TtsConfig config = {});

    // Production constructor with explicit transport options (proxy, timeouts).
    // Speech configuration and transport options are kept separate:
    //   config  — what to say and how (voice, rate, volume, pitch)
    //   options — how to reach the service (proxy URL, connection timeouts)
    Communicate(std::string text, core::TtsConfig config, CommunicateOptions options);

    // Test / dependency-injection constructor: synthesizer is called in place
    // of a real SynthesisSession.  Receives the validated TtsConfig and the
    // pre-chunked, XML-escaped text strings produced by TextChunker.
    // Uses default CommunicateOptions.
    Communicate(std::string text, core::TtsConfig config, SynthesizerFn synthesizer);

    // Test constructor with both explicit options and an injected synthesizer.
    // Use this to verify that options flow correctly into the synthesis path
    // (e.g. check that proxy/timeouts are accessible from the synthesizer seam).
    Communicate(std::string text, core::TtsConfig config,
                CommunicateOptions options, SynthesizerFn synthesizer);

    [[nodiscard]] const std::string&        text()    const noexcept;
    [[nodiscard]] const core::TtsConfig&    config()  const noexcept;
    [[nodiscard]] const CommunicateOptions& options() const noexcept;

    // Synthesize and return all TtsChunk events (AudioChunk + BoundaryChunk).
    // Reference: Communicate.stream() — single-use; returns invalid_state on repeat.
    [[nodiscard]] common::Result<std::vector<core::TtsChunk>> stream_sync();

    // Synthesize, write audio to media_path, and optionally write SRT subtitles.
    // Reference: Communicate.save() — single-use; returns invalid_state on repeat.
    [[nodiscard]] common::Result<void> save(
        const std::filesystem::path& media_path,
        std::optional<std::filesystem::path> subtitles_path = std::nullopt);

private:
    std::string        text_;
    core::TtsConfig    config_;
    CommunicateOptions options_;
    SynthesizerFn      synthesizer_;
    bool               stream_called_{false};

    // Shared synthesis pipeline: validate → chunk → synthesize.
    // Called by both stream_sync() and save() after the single-use guard.
    [[nodiscard]] common::Result<std::vector<core::TtsChunk>> run_synthesis();
};

} // namespace edge_tts::api
