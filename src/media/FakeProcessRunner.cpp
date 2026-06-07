#include "edge_tts/media/ProcessRunner.hpp"
#include "edge_tts/common/Error.hpp"

// FakeProcessRunner has no POSIX dependencies and compiles on all platforms.
// The real ProcessRunner (fork/execvp/pipe/waitpid) lives in ProcessRunner.cpp
// which is only compiled on POSIX systems.

namespace edge_tts::media {

void FakeProcessRunner::set_result(ProcessResult result) noexcept {
    result_ = std::move(result);
    error_.reset();
}

void FakeProcessRunner::set_error(common::Error error) noexcept {
    error_ = std::move(error);
}

void FakeProcessRunner::clear_error() noexcept {
    error_.reset();
}

const std::optional<ProcessCommand>& FakeProcessRunner::last_command() const noexcept {
    return last_command_;
}

int FakeProcessRunner::run_count() const noexcept {
    return run_count_;
}

common::Result<ProcessResult> FakeProcessRunner::run(const ProcessCommand& cmd) {
    ++run_count_;
    last_command_ = cmd;

    if (error_.has_value())
        return common::Result<ProcessResult>::fail(*error_);

    return common::Result<ProcessResult>::ok(result_);
}

} // namespace edge_tts::media
