#pragma once
// In-memory test double for IProcessRunner — no child process, no POSIX deps.
//
// Usage: inject a FakeProcessRunner instead of ProcessRunner in unit tests so
// tests remain fast, deterministic, and free of side effects.
//
// Example:
//   FakeProcessRunner runner;
//   runner.set_result({0, "output", ""});
//   FfmpegAudioConverter conv("/path/to/ffmpeg", runner);
//   auto result = conv.convert(input, output);

#include "media/ProcessRunner.hpp"

namespace edge_tts::media {

class FakeProcessRunner final : public IProcessRunner {
public:
    // Configure the result returned by the next run() call.
    // Default: ProcessResult{0, "", ""} (success, no output).
    void set_result(ProcessResult result) noexcept;

    // Configure run() to return an error (transport-level failure).
    // Clears any configured result override.
    void set_error(common::Error error) noexcept;

    // Clear a configured error, reverting to the configured result.
    void clear_error() noexcept;

    // The most recent command passed to run(), or nullopt if never called.
    [[nodiscard]] const std::optional<ProcessCommand>& last_command() const noexcept;

    // Total number of times run() has been called.
    [[nodiscard]] int run_count() const noexcept;

    // IProcessRunner
    [[nodiscard]] common::Result<ProcessResult>
    run(const ProcessCommand& cmd) override;

private:
    ProcessResult                  result_{};
    std::optional<common::Error>   error_;
    std::optional<ProcessCommand>  last_command_;
    int                            run_count_{0};
};

} // namespace edge_tts::media
