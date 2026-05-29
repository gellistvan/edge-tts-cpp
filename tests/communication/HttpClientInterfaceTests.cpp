#include "edge_tts/communication/FakeHttpClient.hpp"
#include "edge_tts/communication/IHttpClient.hpp"
#include "edge_tts/communication/HttpTypes.hpp"
#include "edge_tts/common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <map>
#include <string>

using edge_tts::communication::FakeHttpClient;
using edge_tts::communication::HttpRequest;
using edge_tts::communication::HttpResponse;
using edge_tts::common::ErrorCode;

// Helper: build a minimal GET request.
static HttpRequest make_get(const char* url) {
    return {.method = "GET", .url = url, .headers = {}, .body = {}};
}

// ---------------------------------------------------------------------------
// Fake returns configured response
// ---------------------------------------------------------------------------

TEST(FakeHttpClient, ReturnsConfiguredStatusCode) {
    FakeHttpClient fake;
    fake.set_response({404, {}, "Not Found"});
    const auto r = fake.send(make_get("https://example.com"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().status_code, 404);
}

TEST(FakeHttpClient, ReturnsConfiguredBody) {
    FakeHttpClient fake;
    fake.set_response({200, {}, R"([{"Name":"Emma"}])"});
    const auto r = fake.send(make_get("https://example.com"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().body, R"([{"Name":"Emma"}])");
}

TEST(FakeHttpClient, DefaultResponseIs200Empty) {
    FakeHttpClient fake;
    const auto r = fake.send(make_get("https://example.com"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().status_code, 200);
    EXPECT_TRUE(r.value().body.empty());
}

TEST(FakeHttpClient, MultipleCallsReturnSameConfiguredResponse) {
    FakeHttpClient fake;
    fake.set_response({200, {}, "data"});
    for (int i = 0; i < 3; ++i) {
        const auto r = fake.send(make_get("https://example.com"));
        EXPECT_TRUE(r.has_value());
        EXPECT_EQ(r.value().body, "data");
    }
}

// ---------------------------------------------------------------------------
// Fake captures last request
// ---------------------------------------------------------------------------

TEST(FakeHttpClient, CapturesLastRequestUrl) {
    FakeHttpClient fake;
    [[maybe_unused]] auto r = fake.send(make_get("https://speech.platform.bing.com/voices/list"));
    EXPECT_TRUE(fake.last_request().has_value());
    EXPECT_EQ(fake.last_request()->url, "https://speech.platform.bing.com/voices/list");
}

TEST(FakeHttpClient, CapturesLastRequestMethod) {
    FakeHttpClient fake;
    [[maybe_unused]] auto r = fake.send({.method = "GET", .url = "https://example.com", .headers = {}, .body = {}});
    EXPECT_EQ(fake.last_request()->method, "GET");
}

TEST(FakeHttpClient, LastRequestIsNulloptBeforeSend) {
    FakeHttpClient fake;
    EXPECT_FALSE(fake.last_request().has_value());
}

TEST(FakeHttpClient, LastRequestUpdatedOnEachSend) {
    FakeHttpClient fake;
    [[maybe_unused]] auto r1 = fake.send(make_get("https://first.example.com"));
    [[maybe_unused]] auto r2 = fake.send(make_get("https://second.example.com"));
    EXPECT_EQ(fake.last_request()->url, "https://second.example.com");
}

TEST(FakeHttpClient, SendCountTracked) {
    FakeHttpClient fake;
    EXPECT_EQ(fake.send_count(), 0);
    [[maybe_unused]] auto r1 = fake.send(make_get("https://example.com"));
    EXPECT_EQ(fake.send_count(), 1);
    [[maybe_unused]] auto r2 = fake.send(make_get("https://example.com"));
    EXPECT_EQ(fake.send_count(), 2);
}

// ---------------------------------------------------------------------------
// Fake can return error
// ---------------------------------------------------------------------------

TEST(FakeHttpClient, ReturnsConfiguredError) {
    FakeHttpClient fake;
    fake.set_error({ErrorCode::network_error, "connection refused"});
    const auto r = fake.send(make_get("https://example.com"));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
}

TEST(FakeHttpClient, ErrorStillCapturesRequest) {
    FakeHttpClient fake;
    fake.set_error({ErrorCode::network_error, "timeout"});
    [[maybe_unused]] auto r = fake.send(make_get("https://example.com/voices"));
    EXPECT_TRUE(fake.last_request().has_value());
    EXPECT_EQ(fake.last_request()->url, "https://example.com/voices");
}

TEST(FakeHttpClient, ErrorCountsAsSend) {
    FakeHttpClient fake;
    fake.set_error({ErrorCode::service_error, "503"});
    [[maybe_unused]] auto r = fake.send(make_get("https://example.com"));
    EXPECT_EQ(fake.send_count(), 1);
}

TEST(FakeHttpClient, ClearErrorReverts) {
    FakeHttpClient fake;
    fake.set_error({ErrorCode::network_error, "down"});
    fake.clear_error();
    const auto r = fake.send(make_get("https://example.com"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().status_code, 200);
}

TEST(FakeHttpClient, SetResponseAfterErrorClearsError) {
    FakeHttpClient fake;
    fake.set_error({ErrorCode::network_error, "down"});
    fake.set_response({200, {}, "ok"});
    const auto r = fake.send(make_get("https://example.com"));
    EXPECT_TRUE(r.has_value());
}

// ---------------------------------------------------------------------------
// Headers preserved in request
// ---------------------------------------------------------------------------

TEST(FakeHttpClient, CapturesRequestHeaders) {
    FakeHttpClient fake;
    HttpRequest req{
        .method  = "GET",
        .url     = "https://speech.platform.bing.com/voices/list",
        .headers = {
            {"User-Agent", "Mozilla/5.0"},
            {"Accept",     "*/*"},
        },
        .body = {},
    };
    (void)fake.send(req);
    EXPECT_EQ(fake.last_request()->headers.at("User-Agent"), "Mozilla/5.0");
    EXPECT_EQ(fake.last_request()->headers.at("Accept"),     "*/*");
}

TEST(FakeHttpClient, ReturnsConfiguredResponseHeaders) {
    FakeHttpClient fake;
    fake.set_response({
        200,
        {{"Content-Type", "application/json"}, {"Date", "Mon, 01 Jan 2024 00:00:00 GMT"}},
        "[]",
    });
    const auto r = fake.send(make_get("https://example.com"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().headers.at("Content-Type"), "application/json");
    EXPECT_EQ(r.value().headers.at("Date"), "Mon, 01 Jan 2024 00:00:00 GMT");
}

// ---------------------------------------------------------------------------
// Body preserved in request and response
// ---------------------------------------------------------------------------

TEST(FakeHttpClient, CapturesRequestBody) {
    FakeHttpClient fake;
    HttpRequest req{
        .method  = "POST",
        .url     = "https://example.com",
        .headers = {},
        .body    = "hello body",
    };
    (void)fake.send(req);
    EXPECT_TRUE(fake.last_request()->body.has_value());
    EXPECT_EQ(*fake.last_request()->body, "hello body");
}

TEST(FakeHttpClient, NoBodyRequestBodyIsAbsent) {
    FakeHttpClient fake;
    (void)fake.send(make_get("https://example.com"));
    EXPECT_FALSE(fake.last_request()->body.has_value());
}

TEST(FakeHttpClient, ResponseBodyPreservedVerbatim) {
    FakeHttpClient fake;
    const std::string json_body = R"([{"Name":"en-US-EmmaMultilingualNeural"}])";
    fake.set_response({200, {}, json_body});
    const auto r = fake.send(make_get("https://example.com"));
    EXPECT_EQ(r.value().body, json_body);
}

// ---------------------------------------------------------------------------
// Interface abstraction — usable through IHttpClient pointer
// ---------------------------------------------------------------------------

TEST(FakeHttpClient, UsableViaInterface) {
    FakeHttpClient fake;
    fake.set_response({200, {}, "voices_json"});
    edge_tts::communication::IHttpClient& client = fake;
    const auto r = client.send(make_get("https://example.com"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().body, "voices_json");
}
