#include "common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::common::to_string;

// ---------------------------------------------------------------------------
// to_string(ErrorCode)
// ---------------------------------------------------------------------------

TEST(ErrorCode, ToStringCoversAllCodes) {
    // Every code must produce a non-empty, unique string.
    const ErrorCode codes[] = {
        ErrorCode::none,
        ErrorCode::invalid_argument,
        ErrorCode::invalid_state,
        ErrorCode::io_error,
        ErrorCode::network_error,
        ErrorCode::protocol_error,
        ErrorCode::parse_error,
        ErrorCode::timeout,
        ErrorCode::unsupported,
        ErrorCode::external_process_failed,
        ErrorCode::service_error,
        ErrorCode::drm_error,
    };
    for (auto c : codes) {
        EXPECT_FALSE(to_string(c).empty());
    }
}

TEST(ErrorCode, ToStringDistinct) {
    EXPECT_NE(to_string(ErrorCode::network_error), to_string(ErrorCode::protocol_error));
    EXPECT_NE(to_string(ErrorCode::io_error),      to_string(ErrorCode::none));
}

TEST(ErrorCode, ToStringKnownValue) {
    EXPECT_EQ(to_string(ErrorCode::network_error),    "network_error");
    EXPECT_EQ(to_string(ErrorCode::invalid_argument), "invalid_argument");
    EXPECT_EQ(to_string(ErrorCode::service_error),    "service_error");
    EXPECT_EQ(to_string(ErrorCode::drm_error),        "drm_error");
}

// ---------------------------------------------------------------------------
// Error construction and accessors
// ---------------------------------------------------------------------------

TEST(Error, CodeIsStored) {
    Error e{ErrorCode::io_error, "read failed"};
    EXPECT_TRUE(e.code() == ErrorCode::io_error);
}

TEST(Error, MessageIsStored) {
    Error e{ErrorCode::parse_error, "unexpected token"};
    EXPECT_EQ(e.message(), "unexpected token");
}

TEST(Error, NoContextByDefault) {
    Error e{ErrorCode::timeout, "connection timed out"};
    EXPECT_FALSE(e.has_context());
    EXPECT_TRUE(e.context().empty());
}

TEST(Error, ContextIsStored) {
    Error e{ErrorCode::network_error, "connection refused", "wss://speech.bing.com"};
    EXPECT_TRUE(e.has_context());
    EXPECT_EQ(e.context(), "wss://speech.bing.com");
}

TEST(Error, WhatWithoutContext) {
    Error e{ErrorCode::io_error, "disk full"};
    const auto w = e.what();
    EXPECT_NE(w.find("io_error"), std::string::npos);
    EXPECT_NE(w.find("disk full"), std::string::npos);
}

TEST(Error, WhatWithContext) {
    Error e{ErrorCode::protocol_error, "unknown path", "turn.end"};
    const auto w = e.what();
    EXPECT_NE(w.find("protocol_error"), std::string::npos);
    EXPECT_NE(w.find("unknown path"), std::string::npos);
    EXPECT_NE(w.find("turn.end"), std::string::npos);
}

TEST(Error, MoveConstruction) {
    Error src{ErrorCode::service_error, "no audio", "chunk 3"};
    Error dst{std::move(src)};
    EXPECT_TRUE(dst.code() == ErrorCode::service_error);
    EXPECT_EQ(dst.message(), "no audio");
    EXPECT_EQ(dst.context(), "chunk 3");
}

// ---------------------------------------------------------------------------
// Context field carries diagnostic data (filename, HTTP status, etc.)
// ---------------------------------------------------------------------------

TEST(Error, FilePathPreservedInContext) {
    // io_error should carry the file path so callers can identify the failing file.
    Error e{ErrorCode::io_error, "Failed to open file for binary write",
             "/var/data/output.mp3"};
    EXPECT_EQ(e.context(), "/var/data/output.mp3");
    EXPECT_NE(e.what().find("/var/data/output.mp3"), std::string::npos);
}

TEST(Error, HttpStatusPreservedInContext) {
    // service_error should carry the HTTP status so callers can distinguish
    // 403 (DRM) from 429 (rate-limit) from 503 (service unavailable).
    Error e{ErrorCode::service_error, "Request failed", "403"};
    EXPECT_EQ(e.context(), "403");
    EXPECT_NE(e.what().find("403"), std::string::npos);
}

TEST(Error, DrmErrorCodeDistinctFromServiceError) {
    // drm_error and service_error must map to different strings so callers
    // that inspect error codes programmatically can distinguish them.
    EXPECT_NE(to_string(ErrorCode::drm_error), to_string(ErrorCode::service_error));
    EXPECT_NE(to_string(ErrorCode::drm_error), to_string(ErrorCode::network_error));
}
