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
        ErrorCode::cancelled,
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

// ---------------------------------------------------------------------------
// Taxonomy: each major category is semantically distinct
// ---------------------------------------------------------------------------

TEST(ErrorCode, AllCodesDistinct) {
    // Every ErrorCode must map to a unique string so switch-on-code is reliable.
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
        ErrorCode::cancelled,
    };
    for (std::size_t i = 0; i < std::size(codes); ++i) {
        for (std::size_t j = i + 1; j < std::size(codes); ++j) {
            EXPECT_NE(to_string(codes[i]), to_string(codes[j]));
        }
    }
}

TEST(ErrorCode, ToStringCancelledValue) {
    EXPECT_EQ(to_string(ErrorCode::cancelled), "cancelled");
}

TEST(ErrorCode, ToStringTimeout) {
    EXPECT_EQ(to_string(ErrorCode::timeout), "timeout");
}

TEST(ErrorCode, ToStringParseError) {
    EXPECT_EQ(to_string(ErrorCode::parse_error), "parse_error");
}

TEST(ErrorCode, ToStringUnsupported) {
    EXPECT_EQ(to_string(ErrorCode::unsupported), "unsupported");
}

TEST(ErrorCode, ToStringExternalProcessFailed) {
    EXPECT_EQ(to_string(ErrorCode::external_process_failed), "external_process_failed");
}

// ---------------------------------------------------------------------------
// Category semantics: context field contracts per category
// ---------------------------------------------------------------------------

// invalid_argument: context carries the offending value or parameter name.
TEST(Error, InvalidArgumentContextCarriesOffendingValue) {
    Error e{ErrorCode::invalid_argument, "voice name is empty", "voice"};
    EXPECT_EQ(e.code(), ErrorCode::invalid_argument);
    EXPECT_EQ(e.context(), "voice");
    EXPECT_NE(e.what().find("invalid_argument"), std::string::npos);
}

// io_error: context carries the file path so callers can identify the failing file.
TEST(Error, IoErrorContextCarriesFilePath) {
    Error e{ErrorCode::io_error, "Failed to open file for binary write",
             "/tmp/output.mp3"};
    EXPECT_EQ(e.code(), ErrorCode::io_error);
    EXPECT_EQ(e.context(), "/tmp/output.mp3");
}

// network_error: context is optional (URL may be omitted to avoid leaking tokens).
TEST(Error, NetworkErrorWithoutContextIsValid) {
    Error e{ErrorCode::network_error, "WebSocket connect failed"};
    EXPECT_EQ(e.code(), ErrorCode::network_error);
    EXPECT_FALSE(e.has_context());
}

// protocol_error: context carries the wire value that was unexpected.
TEST(Error, ProtocolErrorContextCarriesWireValue) {
    Error e{ErrorCode::protocol_error, "unexpected Path header value", "speech.synthesis"};
    EXPECT_EQ(e.code(), ErrorCode::protocol_error);
    EXPECT_EQ(e.context(), "speech.synthesis");
}

// service_error: context carries the HTTP status code for programmatic inspection.
TEST(Error, ServiceErrorContextCarriesHttpStatus) {
    Error e{ErrorCode::service_error, "voice list HTTP request failed", "429"};
    EXPECT_EQ(e.code(), ErrorCode::service_error);
    EXPECT_EQ(e.context(), "429");
    EXPECT_NE(e.what().find("429"), std::string::npos);
}

// drm_error: context carries the server Date header for clock-skew correction.
TEST(Error, DrmErrorContextCarriesServerDate) {
    Error e{ErrorCode::drm_error, "WebSocket connect failed",
             "Mon, 01 Jan 2024 00:00:00 GMT"};
    EXPECT_EQ(e.code(), ErrorCode::drm_error);
    EXPECT_EQ(e.context(), "Mon, 01 Jan 2024 00:00:00 GMT");
}

// external_process_failed: context carries the subprocess stderr for diagnostics.
TEST(Error, ExternalProcessFailedContextCarriesStderr) {
    Error e{ErrorCode::external_process_failed,
             "ffplay exited with code 1",
             "No such file or directory"};
    EXPECT_EQ(e.code(), ErrorCode::external_process_failed);
    EXPECT_EQ(e.context(), "No such file or directory");
}

// cancelled: no context needed — the caller initiated the cancellation.
TEST(Error, CancelledHasNoContext) {
    Error e{ErrorCode::cancelled, "synthesis was cancelled"};
    EXPECT_EQ(e.code(), ErrorCode::cancelled);
    EXPECT_FALSE(e.has_context());
    EXPECT_NE(e.what().find("cancelled"), std::string::npos);
}

// timeout: no context needed — the timeout value is in SynthesisOptions, not here.
TEST(Error, TimeoutErrorIsDistinctFromNetworkError) {
    Error timeout_err{ErrorCode::timeout, "WebSocket receive timed out"};
    Error network_err{ErrorCode::network_error, "connection reset"};
    EXPECT_NE(timeout_err.code(), network_err.code());
    EXPECT_NE(to_string(timeout_err.code()), to_string(network_err.code()));
}

// invalid_state: returned when synthesize()/save() is called a second time.
TEST(Error, InvalidStateDistinctFromInvalidArgument) {
    EXPECT_NE(to_string(ErrorCode::invalid_state),
              to_string(ErrorCode::invalid_argument));
}

// unsupported: returned for proxy or platform-level stub operations.
TEST(Error, UnsupportedDistinctFromNetworkError) {
    EXPECT_NE(to_string(ErrorCode::unsupported),
              to_string(ErrorCode::network_error));
}

// parse_error vs protocol_error: JSON decode vs. semantic wire-message violation.
TEST(Error, ParseErrorDistinctFromProtocolError) {
    EXPECT_NE(to_string(ErrorCode::parse_error),
              to_string(ErrorCode::protocol_error));
}

// ---------------------------------------------------------------------------
// Context field security: secrets must not appear in context
// ---------------------------------------------------------------------------

TEST(Error, NetworkErrorContextDoesNotContainToken) {
    // The DRM token must never appear in an error context field.
    // This test documents the expectation; enforcement is in the call sites.
    const std::string fake_drm_token = "AABBCCDD11223344";
    Error e{ErrorCode::network_error, "WebSocket connect failed"};
    // No context was attached — the token stays out of the error.
    EXPECT_FALSE(e.has_context());
    EXPECT_EQ(e.what().find(fake_drm_token), std::string::npos);
}

TEST(Error, ProxyContextRedactsCredentials) {
    // Proxy URLs with credentials must have the password replaced by [credentials]
    // before being stored in the context field.  This test verifies the pattern
    // at the Error level — the redaction itself happens at the call site.
    const std::string redacted = "http://[credentials]@proxy.example.com:8080";
    Error e{ErrorCode::unsupported, "proxy not supported", redacted};
    EXPECT_EQ(e.context(), redacted);
    EXPECT_EQ(e.what().find("password"), std::string::npos);
}
