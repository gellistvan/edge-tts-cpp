#pragma once

#include "common/Error.hpp"
#include "communication/IHttpClient.hpp"

#include <optional>
#include <queue>

namespace edge_tts::communication {

// In-memory HTTP client for unit testing.  Never performs real network I/O.
//
// Usage — fixed single response:
//   FakeHttpClient fake;
//   fake.set_response({200, {{"Content-Type", "application/json"}}, "[...]"});
//   const auto result = fake.send({.method="GET", .url="https://..."});
//   EXPECT_EQ(fake.last_request()->url, "https://...");
//
// Usage — queued responses (returned in order, then fallback to set_response):
//   FakeHttpClient fake;
//   fake.push_response({403, {}, "Forbidden"});  // first call
//   fake.push_response({200, {}, "[...]"});       // second call
//   fake.set_response({200, {}, "[]"});           // subsequent calls fallback
//
// By default (no set_response called) returns HTTP 200 with an empty body.
// Call set_error() to simulate a transport-level failure.
class FakeHttpClient final : public IHttpClient {
public:
    FakeHttpClient() = default;

    // Configure the response returned by the next send() call(s).
    // Clears any previously queued responses.
    void set_response(HttpResponse response) noexcept;

    // Enqueue a response.  Queued responses are consumed in order on each
    // send() call; when the queue is empty, falls back to set_response().
    // Does NOT clear the queue — multiple push_response() calls build a sequence.
    void push_response(HttpResponse response);

    // Configure send() to return an error instead of a response.
    // Clears any previously configured response override.
    void set_error(common::Error error) noexcept;

    // Clear any configured error, reverting to the current set_response value.
    void clear_error() noexcept;

    // Returns the most recently received request, or nullopt if send() has
    // never been called.
    [[nodiscard]] const std::optional<HttpRequest>& last_request() const noexcept;

    // Returns the total number of times send() has been called.
    [[nodiscard]] int send_count() const noexcept;

    // IHttpClient
    [[nodiscard]] common::Result<HttpResponse>
    send(const HttpRequest& request) override;

private:
    HttpResponse                  response_{200, {}, ""};
    std::queue<HttpResponse>      response_queue_;
    std::optional<common::Error>  error_;
    std::optional<HttpRequest>    last_request_;
    int                           send_count_{0};
};

} // namespace edge_tts::communication
