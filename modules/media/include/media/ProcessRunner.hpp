#pragma once

#include "common/Error.hpp"
#include "common/Result.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace edge_tts::media {

// An external command to execute.
//
// Reference: playback.py _run_edge_tts() / _play_media() — both use list-form
// subprocess.Popen() so no shell quoting or concatenation is ever needed.
// The executable and each argument are passed as separate tokens to execvp().
//
// Forbidden: never join these into a shell string (no system(), no popen("…")).
struct ProcessCommand {
    std::filesystem::path    executable; // full path or name found via $PATH
    std::vector<std::string> arguments;  // each argument is a separate element
};

// Result captured from a completed subprocess.
struct ProcessResult {
    int         exit_code{0};
    std::string stdout_text;
    std::string stderr_text;
};

// Abstract process runner interface — allows test doubles.
class IProcessRunner {
public:
    virtual ~IProcessRunner() = default;

    // Run the command and return its result.
    // Returns fail() on launch failure (e.g. executable not found, fork error).
    // A non-zero exit code is returned inside ProcessResult, not as a failure.
    [[nodiscard]] virtual common::Result<ProcessResult>
    run(const ProcessCommand& cmd) = 0;
};

// ---------------------------------------------------------------------------
// ProcessRunner — real POSIX implementation (fork + execvp, no shell).
//
// Not marked final so tests can subclass and override make_pipe() to inject
// pipe-creation failures without forking a real process.
// ---------------------------------------------------------------------------

class ProcessRunner : public IProcessRunner {
public:
    // Run cmd.executable with cmd.arguments as argv[1..].
    // stdout and stderr are captured and returned in ProcessResult.
    // Returns ErrorCode::external_process_failed if fork, pipe, or exec fails.
    [[nodiscard]] common::Result<ProcessResult>
    run(const ProcessCommand& cmd) override;

protected:
    // Create a pipe.  Returns 0 on success, -1 on failure (sets errno).
    // Override in test subclasses to inject controlled failures.
    virtual int make_pipe(int fds[2]);
};

} // namespace edge_tts::media
