#pragma once

#include "api/StreamCallbacks.hpp"
#include "api/SynthesisOptions.hpp"
#include "common/CancellationToken.hpp"
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
// ── Object lifetime ───────────────────────────────────────────────────────────
//   Construction is cheap: the full networking stack is assembled but no
//   connection is opened.  All network I/O is deferred to synthesize()/save().
//   The object owns its text, config, options, and internal networking stack.
//   The synthesizer may be destroyed at any point after synthesize()/save()
//   returns (success or failure).  Do NOT destroy a synthesizer while
//   synthesize() or save() is running on another thread (see Thread Safety).
//
// ── Single-use ────────────────────────────────────────────────────────────────
//   synthesize() and save() each consume the object. A second call to either
//   returns ErrorCode::invalid_state regardless of which was called first.
//   To synthesize the same text again, construct a new SpeechSynthesizer.
//
// ── Thread safety ─────────────────────────────────────────────────────────────
//   Not thread-safe. Accessing or mutating a single SpeechSynthesizer from
//   multiple threads simultaneously produces undefined behavior.
//   Constructing independent SpeechSynthesizer objects in different threads is
//   safe — there is no shared global state.
//
// ── Error model ───────────────────────────────────────────────────────────────
//   No exceptions are thrown for recoverable failures. All errors are returned
//   as Result<T>::fail(Error{...}).  The only exceptions that may propagate are
//   std::bad_alloc (out of memory) and logic errors from the standard library.
//   Use result.error().code() to branch on error categories (ErrorCode enum).
//
// ── Chunk ownership ───────────────────────────────────────────────────────────
//   synthesize() returns Result<vector<TtsChunk>>; the caller owns the vector.
//   The vector has no references back into the SpeechSynthesizer and outlives
//   the synthesizer object safely.
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

    // Request cancellation of any in-progress synthesize() or save() call.
    //
    // cancel() is idempotent and thread-safe.  It may be called from any
    // thread at any time, including before synthesis begins.
    //
    // The ongoing operation checks the cancellation flag before each
    // WebSocket receive() call and before each text-chunk iteration.
    // On detection, synthesis stops and returns ErrorCode::cancelled.
    //
    // Cancellation after synthesize()/save() has already returned is a no-op.
    // Cancellation before synthesize()/save() is called causes the first call
    // to return ErrorCode::cancelled immediately (pre-synthesis check).
    void cancel() noexcept;

    // Synthesize and return all TtsChunk events (AudioChunk + BoundaryChunk).
    // Single-use: returns ErrorCode::invalid_state if called more than once.
    [[nodiscard]] common::Result<std::vector<core::TtsChunk>> synthesize();

    // Synthesize, write audio to media_path, and optionally write SRT subtitles.
    // Single-use: returns ErrorCode::invalid_state if called more than once.
    [[nodiscard]] common::Result<void> save(
        const std::filesystem::path& media_path,
        std::optional<std::filesystem::path> subtitles_path = std::nullopt);

    // Synthesize and deliver chunks progressively via callbacks.
    //
    // Single-use: returns ErrorCode::invalid_state if called after synthesize(),
    // save(), or synthesize_stream() has already been called.
    //
    // on_audio and on_boundary are invoked in arrival order on the calling
    // thread.  Exactly one of on_complete or on_error fires last.
    // All fields of callbacks are optional — null std::function is skipped.
    //
    // The return value mirrors the completion status (success → ok(); error or
    // cancellation → fail(...)) for callers who prefer a Result-based flow.
    // on_complete / on_error carry the same outcome for callers who prefer a
    // callback-based flow.
    //
    // Cancellation: call cancel() at any time, including from inside a callback.
    // Dispatch stops before the next chunk; on_error fires with
    // ErrorCode::cancelled.
    [[nodiscard]] common::Result<void> synthesize_stream(StreamCallbacks callbacks);

private:
    std::string                 text_;
    core::TtsConfig             config_;
    SynthesisOptions            options_;
    // cancel_token_ must be declared before synthesizer_ so it is initialized
    // first; make_production_synthesizer() receives a reference to it.
    common::CancellationToken   cancel_token_;
    SynthesizerFn               synthesizer_;
    bool                        called_{false};

    // Shared synthesis pipeline: validate → chunk → synthesize.
    // Called by both synthesize() and save() after the single-use guard.
    [[nodiscard]] common::Result<std::vector<core::TtsChunk>> run_pipeline();
};

} // namespace edge_tts::api
