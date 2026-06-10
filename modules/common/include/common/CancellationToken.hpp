#pragma once

#include <atomic>
#include <memory>

namespace edge_tts::common {

// A lightweight, thread-safe cancellation signal.
//
// CancellationToken is a value type backed by a shared atomic flag.  Copies
// of the same token observe the same flag — so a token can be shared between
// the owner (which calls cancel()) and one or more ongoing operations (which
// call is_cancelled()).
//
// Typical use:
//   edge_tts::api::SpeechSynthesizer synthesizer("...");
//   // on another thread:
//   synthesizer.cancel();          // set the flag
//   // synthesis returns ErrorCode::cancelled on the next checkpoint
//
// Thread safety:
//   cancel() and is_cancelled() are safe to call concurrently from multiple
//   threads without external synchronization.
//
// No global state.  Each default-constructed token creates its own flag.
class CancellationToken {
public:
    CancellationToken()
        : flag_(std::make_shared<std::atomic<bool>>(false)) {}

    // Signal cancellation.  Idempotent — safe to call multiple times.
    void cancel() noexcept {
        flag_->store(true, std::memory_order_relaxed);
    }

    // Returns true if cancel() has been called at least once.
    [[nodiscard]] bool is_cancelled() const noexcept {
        return flag_->load(std::memory_order_relaxed);
    }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

} // namespace edge_tts::common
