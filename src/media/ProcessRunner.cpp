#include "edge_tts/media/ProcessRunner.hpp"
#include "edge_tts/common/Error.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// POSIX headers for fork/exec/pipe.
#include <sys/wait.h>
#include <unistd.h>

namespace edge_tts::media {

// ---------------------------------------------------------------------------
// FakeProcessRunner
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// ProcessRunner — fork + execvp, no shell
// ---------------------------------------------------------------------------

namespace {

// Drain a file descriptor into a string.  Runs on a background thread
// for stderr so both pipes can be drained concurrently (avoids deadlock).
std::string drain_fd(int fd) {
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<std::size_t>(n));
    return out;
}

// RAII closer for a raw file descriptor.
struct FdCloser {
    int fd{-1};
    ~FdCloser() { if (fd >= 0) ::close(fd); }
    void release() { fd = -1; }
};

} // namespace

common::Result<ProcessResult> ProcessRunner::run(const ProcessCommand& cmd) {
    // Build argv: executable + arguments + nullptr sentinel.
    const std::string exec_str = cmd.executable.string();
    std::vector<const char*> argv;
    argv.reserve(cmd.arguments.size() + 2);
    argv.push_back(exec_str.c_str());
    for (const auto& arg : cmd.arguments)
        argv.push_back(arg.c_str());
    argv.push_back(nullptr);

    // Create stdout and stderr pipes.
    int stdout_fds[2], stderr_fds[2];
    if (::pipe(stdout_fds) < 0 || ::pipe(stderr_fds) < 0) {
        return common::Result<ProcessResult>::fail(
            common::Error{common::ErrorCode::external_process_failed,
                          "pipe() failed",
                          std::strerror(errno)});
    }

    // RAII for the parent's read ends (write ends transferred to child).
    FdCloser stdout_read{stdout_fds[0]};
    FdCloser stderr_read{stderr_fds[0]};

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(stdout_fds[1]);
        ::close(stderr_fds[1]);
        return common::Result<ProcessResult>::fail(
            common::Error{common::ErrorCode::external_process_failed,
                          "fork() failed",
                          std::strerror(errno)});
    }

    if (pid == 0) {
        // Child process.
        // Redirect stdout and stderr to the write ends of the pipes.
        ::close(stdout_fds[0]);
        ::dup2(stdout_fds[1], STDOUT_FILENO);
        ::close(stdout_fds[1]);

        ::close(stderr_fds[0]);
        ::dup2(stderr_fds[1], STDERR_FILENO);
        ::close(stderr_fds[1]);

        // execvp searches $PATH; arguments are passed as separate tokens
        // (no shell involvement).
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        ::execvp(argv[0], const_cast<char* const*>(argv.data()));

        // If execvp returns, the executable was not found or not executable.
        ::_exit(127);
    }

    // Parent: close write ends so reads will see EOF when the child exits.
    ::close(stdout_fds[1]);
    ::close(stderr_fds[1]);

    // Drain stderr on a background thread so neither pipe blocks the parent.
    std::string stderr_text;
    std::thread stderr_thread([&stderr_text, fd = stderr_fds[0]] {
        stderr_text = drain_fd(fd);
    });

    std::string stdout_text = drain_fd(stdout_fds[0]);
    stderr_thread.join();

    // Wait for the child and collect its exit status.
    int wstatus = 0;
    ::waitpid(pid, &wstatus, 0);
    const int exit_code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;

    return common::Result<ProcessResult>::ok(
        ProcessResult{exit_code, std::move(stdout_text), std::move(stderr_text)});
}

} // namespace edge_tts::media
