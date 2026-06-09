#include "common/Result.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <memory>
#include <string>

using edge_tts::common::BadResultAccess;
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::common::Result;

// ---------------------------------------------------------------------------
// Result<int> — basic success and failure
// ---------------------------------------------------------------------------

TEST(Result, SuccessHasValue) {
    auto r = Result<int>::ok(42);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(Result, SuccessValue) {
    auto r = Result<int>::ok(99);
    EXPECT_EQ(r.value(), 99);
}

TEST(Result, FailureHasNoValue) {
    auto r = Result<int>::fail(Error{ErrorCode::io_error, "read failed"});
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(static_cast<bool>(r));
}

TEST(Result, FailureErrorCode) {
    auto r = Result<int>::fail(Error{ErrorCode::network_error, "timeout"});
    EXPECT_TRUE(r.error().code() == ErrorCode::network_error);
}

TEST(Result, FailureErrorMessage) {
    auto r = Result<int>::fail(Error{ErrorCode::parse_error, "bad json"});
    EXPECT_EQ(r.error().message(), "bad json");
}

// ---------------------------------------------------------------------------
// Result<std::string> — value retrieval and operator*
// ---------------------------------------------------------------------------

TEST(Result, StringSuccess) {
    auto r = Result<std::string>::ok("hello");
    EXPECT_EQ(r.value(), "hello");
    EXPECT_EQ(*r, "hello");
}

TEST(Result, OperatorArrow) {
    auto r = Result<std::string>::ok("world");
    EXPECT_EQ(r->size(), 5u);
}

// ---------------------------------------------------------------------------
// Move-only value
// ---------------------------------------------------------------------------

TEST(Result, MoveOnlyValue) {
    auto r = Result<std::unique_ptr<int>>::ok(std::make_unique<int>(7));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(**r, 7);
}

TEST(Result, MoveResultSucceeds) {
    auto r1 = Result<std::unique_ptr<int>>::ok(std::make_unique<int>(3));
    auto r2 = std::move(r1);
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(**r2, 3);
}

// ---------------------------------------------------------------------------
// Misuse: value() on failure, error() on success
// ---------------------------------------------------------------------------

TEST(Result, ValueOnFailureThrows) {
    auto r = Result<int>::fail(Error{ErrorCode::service_error, "no audio"});
    EXPECT_THROW(r.value(), BadResultAccess);
}

TEST(Result, ErrorOnSuccessThrows) {
    auto r = Result<int>::ok(1);
    EXPECT_THROW(r.error(), BadResultAccess);
}

// ---------------------------------------------------------------------------
// Result<void>
// ---------------------------------------------------------------------------

TEST(ResultVoid, SuccessHasValue) {
    auto r = Result<void>::ok();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultVoid, SuccessValueDoesNotThrow) {
    auto r = Result<void>::ok();
    EXPECT_NO_THROW(r.value());
}

TEST(ResultVoid, FailureHasNoValue) {
    auto r = Result<void>::fail(Error{ErrorCode::io_error, "write failed"});
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(static_cast<bool>(r));
}

TEST(ResultVoid, FailureErrorCode) {
    auto r = Result<void>::fail(
        Error{ErrorCode::external_process_failed, "ffmpeg exited 1"});
    EXPECT_TRUE(r.error().code() == ErrorCode::external_process_failed);
}

TEST(ResultVoid, ValueOnFailureThrows) {
    auto r = Result<void>::fail(Error{ErrorCode::timeout, "connect timeout"});
    EXPECT_THROW(r.value(), BadResultAccess);
}

TEST(ResultVoid, ErrorOnSuccessThrows) {
    auto r = Result<void>::ok();
    EXPECT_THROW(r.error(), BadResultAccess);
}

TEST(ResultVoid, MoveResult) {
    auto r1 = Result<void>::fail(Error{ErrorCode::unsupported, "not on this OS"});
    auto r2 = std::move(r1);
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().message(), "not on this OS");
}

// ---------------------------------------------------------------------------
// Error with context in Result
// ---------------------------------------------------------------------------

TEST(Result, ErrorContextPreserved) {
    auto r = Result<int>::fail(
        Error{ErrorCode::protocol_error, "unknown path", "audio.metadata"});
    EXPECT_TRUE(r.error().has_context());
    EXPECT_EQ(r.error().context(), "audio.metadata");
}
