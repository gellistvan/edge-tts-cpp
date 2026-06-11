#pragma once

#include "common/Error.hpp"
#include "core/Chunk.hpp"

#include <functional>
#include <span>

namespace edge_tts::api {

// Callbacks for progressive synthesis via SpeechSynthesizer::synthesize_stream().
//
// All callbacks are optional.  A null std::function is silently skipped.
// All callbacks are invoked synchronously on the thread that calls synthesize_stream().
//
// ── Ordering ─────────────────────────────────────────────────────────────────
//   on_audio and on_boundary are called in the order chunks arrive from the
//   service.  Exactly one of on_complete or on_error fires last.
//
// ── Cancellation ─────────────────────────────────────────────────────────────
//   Call SpeechSynthesizer::cancel() at any time — including from inside a
//   callback — to halt delivery.  Dispatch stops before the next chunk and
//   on_error is called with ErrorCode::cancelled instead of on_complete.
//
// ── Lifetime ─────────────────────────────────────────────────────────────────
//   The StreamCallbacks object (and any objects it captures by reference) must
//   remain valid for the entire duration of synthesize_stream().
//
// ── Audio span validity ───────────────────────────────────────────────────────
//   The span passed to on_audio is valid only for the duration of that callback.
//   Copy the bytes if they must outlive the call.
struct StreamCallbacks {
    // Called for each audio chunk in order.  The span is valid during the call only.
    std::function<void(std::span<const std::byte> data)> on_audio;

    // Called for each word or sentence boundary event in order.
    std::function<void(const core::BoundaryChunk& boundary)> on_boundary;

    // Called exactly once on successful completion.  Mutually exclusive with on_error.
    std::function<void()> on_complete;

    // Called exactly once on error or cancellation.  Mutually exclusive with on_complete.
    std::function<void(const common::Error& error)> on_error;
};

} // namespace edge_tts::api
