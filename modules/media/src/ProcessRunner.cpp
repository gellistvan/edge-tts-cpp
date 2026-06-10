#include "media/ProcessRunner.hpp"
#include "common/Error.hpp"

// ProcessRunner uses POSIX-only system calls and is only compiled on POSIX
// platforms. FakeProcessRunner (cross-platform) lives in FakeProcessRunner.cpp.
// CMakeLists.txt guards this file with if(NOT WIN32).
#ifdef _WIN32
#  error "ProcessRunner.cpp requires POSIX (fork/execvp/pipe/waitpid). " \
         "Set EDGE_TTS_BUILD_PLAYBACK_APP=OFF to build without playback on Windows."
#endif

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
    // Give up ownership without closing — caller takes responsibility.
    void release() { fd = -1; }
    // Close immediately and disarm the destructor.
    void close_now() { if (fd >= 0) { ::close(fd); fd = -1; } }
};

} // namespace

int ProcessRunner::make_pipe(int fds[2]) {
    return ::pipe(fds);
}

common::Result<ProcessResult> ProcessRunner::run(const ProcessCommand& cmd) {
    // Build argv: executable + arguments + nullptr sentinel.
    const std::string exec_str = cmd.executable.string();
    std::vector<const char*> argv;
    argv.reserve(cmd.arguments.size() + 2);
    argv.push_back(exec_str.c_str());
    for (const auto& arg : cmd.arguments)
        argv.push_back(arg.c_str());
    argv.push_back(nullptr);

    // Create stdout pipe. Both ends are RAII-protected from here on.
    int stdout_fds[2];
    if (make_pipe(stdout_fds) < 0) {
        return common::Result<ProcessResult>::fail(
            common::Error{common::ErrorCode::external_process_failed,
                          "pipe() failed for stdout",
                          std::strerror(errno)});
    }
    FdCloser stdout_read{stdout_fds[0]};
    FdCloser stdout_write{stdout_fds[1]};

    // Create stderr pipe.
    int stderr_fds[2];
    if (make_pipe(stderr_fds) < 0) {
        return common::Result<ProcessResult>::fail(
            common::Error{common::ErrorCode::external_process_failed,
                          "pipe() failed for stderr",
                          std::strerror(errno)});
    }
    FdCloser stderr_read{stderr_fds[0]};
    FdCloser stderr_write{stderr_fds[1]};

    const pid_t pid = ::fork();
    if (pid < 0) {
        return common::Result<ProcessResult>::fail(
            common::Error{common::ErrorCode::external_process_failed,
                          "fork() failed",
                          std::strerror(errno)});
    }

    if (pid == 0) {
        // Child process — _exit() means C++ destructors do not run here.
        ::close(stdout_fds[0]);
        ::close(stderr_fds[0]);

        // Exit code 126: dup2 setup failure (distinct from 127 exec-not-found).
        if (::dup2(stdout_fds[1], STDOUT_FILENO) < 0) ::_exit(126);
        ::close(stdout_fds[1]);

        if (::dup2(stderr_fds[1], STDERR_FILENO) < 0) ::_exit(126);
        ::close(stderr_fds[1]);

        // execvp searches $PATH; arguments are passed as separate tokens
        // (no shell involvement).
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        ::execvp(argv[0], const_cast<char* const*>(argv.data()));

        // If execvp returns, the executable was not found or not executable.
        ::_exit(127);
    }

    // Parent: close write ends immediately so the pipes reach EOF when the
    // child exits.  The read ends remain open for draining below.
    stdout_write.close_now();
    stderr_write.close_now();

    // Drain stderr on a background thread so neither pipe blocks the parent
    // when both stdout and stderr produce large output simultaneously.
    std::string stderr_text;
    std::thread stderr_thread([&stderr_text, fd = stderr_fds[0]] {
        stderr_text = drain_fd(fd);
    });

    std::string stdout_text = drain_fd(stdout_fds[0]);
    stderr_thread.join();

    // Wait for the child; retry on EINTR (signal delivery during wait).
    int wstatus = 0;
    pid_t wait_result;
    do {
        wait_result = ::waitpid(pid, &wstatus, 0);
    } while (wait_result < 0 && errno == EINTR);

    if (wait_result < 0) {
        return common::Result<ProcessResult>::fail(
            common::Error{common::ErrorCode::external_process_failed,
                          "waitpid() failed",
                          std::strerror(errno)});
    }

    const int exit_code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;

    return common::Result<ProcessResult>::ok(
        ProcessResult{exit_code, std::move(stdout_text), std::move(stderr_text)});
}

} // namespace edge_tts::media
