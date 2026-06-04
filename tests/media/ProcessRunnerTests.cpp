#include "edge_tts/media/ProcessRunner.hpp"
#include "edge_tts/common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>
#include <vector>

using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::media::FakeProcessRunner;
using edge_tts::media::ProcessCommand;
using edge_tts::media::ProcessResult;
using edge_tts::media::ProcessRunner;

// ---------------------------------------------------------------------------
// FakeProcessRunner
// ---------------------------------------------------------------------------

TEST(FakeProcessRunner, DefaultResultIsSuccess) {
    FakeProcessRunner fake;
    ProcessCommand cmd{"/bin/echo", {"hello"}};
    auto r = fake.run(cmd);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->exit_code, 0);
    EXPECT_TRUE(r->stdout_text.empty());
    EXPECT_TRUE(r->stderr_text.empty());
}

TEST(FakeProcessRunner, CapturesLastCommand) {
    FakeProcessRunner fake;
    ProcessCommand cmd{"/usr/bin/mpv", {"--msg-level=all=error", "file.mp3"}};
    (void)fake.run(cmd);
    EXPECT_TRUE(fake.last_command().has_value());
    EXPECT_EQ(fake.last_command()->executable, "/usr/bin/mpv");
    EXPECT_EQ(fake.last_command()->arguments.size(), 2u);
    EXPECT_EQ(fake.last_command()->arguments[0], "--msg-level=all=error");
    EXPECT_EQ(fake.last_command()->arguments[1], "file.mp3");
}

TEST(FakeProcessRunner, CountsRunCalls) {
    FakeProcessRunner fake;
    ProcessCommand cmd{"/bin/true", {}};
    (void)fake.run(cmd);
    (void)fake.run(cmd);
    EXPECT_EQ(fake.run_count(), 2);
}

TEST(FakeProcessRunner, NoCommandBeforeFirstCall) {
    FakeProcessRunner fake;
    EXPECT_FALSE(fake.last_command().has_value());
    EXPECT_EQ(fake.run_count(), 0);
}

TEST(FakeProcessRunner, ReturnsConfiguredResult) {
    FakeProcessRunner fake;
    ProcessResult expected{42, "out text", "err text"};
    fake.set_result(expected);

    ProcessCommand cmd{"/bin/whatever", {}};
    auto r = fake.run(cmd);

    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->exit_code,    42);
    EXPECT_EQ(r->stdout_text,  "out text");
    EXPECT_EQ(r->stderr_text,  "err text");
}

TEST(FakeProcessRunner, ReturnsConfiguredNonZeroExit) {
    FakeProcessRunner fake;
    fake.set_result({1, "", ""});
    auto r = fake.run(ProcessCommand{"/bin/false", {}});
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->exit_code, 1);
}

TEST(FakeProcessRunner, ReturnsConfiguredError) {
    FakeProcessRunner fake;
    fake.set_error(Error{ErrorCode::external_process_failed, "executable not found"});

    auto r = fake.run(ProcessCommand{"/no/such/binary", {}});
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::external_process_failed);
}

TEST(FakeProcessRunner, ClearErrorRestoresResult) {
    FakeProcessRunner fake;
    fake.set_result({0, "ok", ""});
    fake.set_error(Error{ErrorCode::external_process_failed, "fail"});
    fake.clear_error();

    auto r = fake.run(ProcessCommand{"/bin/true", {}});
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->exit_code, 0);
}

TEST(FakeProcessRunner, ArgumentsWithSpacesPreserved) {
    // Arguments that contain spaces must be stored as a single token each —
    // no shell splitting should occur in the fake.
    FakeProcessRunner fake;
    ProcessCommand cmd{"/bin/echo",
                       {"arg with spaces", "another spaced arg"}};
    (void)fake.run(cmd);
    EXPECT_EQ(fake.last_command()->arguments[0], "arg with spaces");
    EXPECT_EQ(fake.last_command()->arguments[1], "another spaced arg");
}

// ---------------------------------------------------------------------------
// ProcessRunner — real POSIX implementation
// ---------------------------------------------------------------------------

TEST(ProcessRunner, EchoHelloReturnsZeroAndCapturesStdout) {
    ProcessRunner runner;
    auto r = runner.run(ProcessCommand{"/bin/echo", {"hello"}});
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->exit_code, 0);
    EXPECT_NE(r->stdout_text.find("hello"), std::string::npos);
}

TEST(ProcessRunner, TrueReturnsZeroExitCode) {
    ProcessRunner runner;
    auto r = runner.run(ProcessCommand{"/bin/true", {}});
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->exit_code, 0);
}

TEST(ProcessRunner, FalseReturnsNonZeroExitCode) {
    ProcessRunner runner;
    auto r = runner.run(ProcessCommand{"/bin/false", {}});
    EXPECT_TRUE(r.has_value()); // launch succeeded; non-zero exit is in ProcessResult
    EXPECT_NE(r->exit_code, 0);
}

TEST(ProcessRunner, StderrIsCapturedSeparately) {
    // Use a shell script trick via /bin/sh to write to stderr.
    ProcessRunner runner;
    auto r = runner.run(ProcessCommand{"/bin/sh", {"-c", "echo errout >&2"}});
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->exit_code, 0);
    EXPECT_NE(r->stderr_text.find("errout"), std::string::npos);
    // stdout must be empty since we only wrote to stderr.
    EXPECT_EQ(r->stdout_text.find("errout"), std::string::npos);
}

TEST(ProcessRunner, ArgumentsWithSpacesPassedVerbatim) {
    // Arguments with internal spaces must NOT be word-split by the shell.
    // Use echo to print the argument; it should appear as one token.
    ProcessRunner runner;
    auto r = runner.run(ProcessCommand{"/bin/echo", {"hello world"}});
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->exit_code, 0);
    // /bin/echo joins its argv with spaces, so "hello world" arrives as one
    // token and is printed as-is.
    EXPECT_NE(r->stdout_text.find("hello world"), std::string::npos);
}

TEST(ProcessRunner, NonExistentExecutableReturnsError) {
    // execvp should fail if the executable does not exist.
    ProcessRunner runner;
    auto r = runner.run(ProcessCommand{"/no/such/binary/does/not/exist", {}});
    // Either a launch error (fail) or exit code 127 (exec-not-found convention).
    if (r.has_value())
        EXPECT_EQ(r->exit_code, 127);
    else
        EXPECT_EQ(r.error().code(), ErrorCode::external_process_failed);
}
