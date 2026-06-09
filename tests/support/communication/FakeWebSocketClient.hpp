#pragma once

#include "common/Error.hpp"
#include "communication/IWebSocketClient.hpp"

#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace edge_tts::communication {

// In-memory WebSocket client for unit testing.  Never performs real network I/O.
//
// Usage pattern:
//   FakeWebSocketClient fake;
//   fake.push_incoming(text_msg("X-RequestId:x\r\nPath:turn.end\r\n\r\n"));
//   auto r = fake.connect("wss://example.com");
//   EXPECT_TRUE(r.has_value());
//   EXPECT_EQ(fake.connected_url(), "wss://example.com");
//
// Errors can be injected per-operation via set_*_error().
// After an error is injected it persists for every subsequent call until cleared.
//
// receive() with an empty incoming queue and no receive_error returns
// ErrorCode::network_error ("receive queue empty") — tests should always queue
// a turn.end message as the last entry so the caller's loop terminates cleanly.
class FakeWebSocketClient final : public IWebSocketClient {
public:
    FakeWebSocketClient() = default;

    // -----------------------------------------------------------------------
    // Incoming message queue
    // -----------------------------------------------------------------------

    // Append a message that receive() will pop (FIFO order).
    void push_incoming(WebSocketMessage message);

    // Number of queued messages not yet consumed by receive().
    [[nodiscard]] std::size_t incoming_queue_size() const noexcept;

    // -----------------------------------------------------------------------
    // Error injection
    // -----------------------------------------------------------------------

    // After set_*_error, the corresponding operation returns the error on
    // every call until clear_*_error() is called.
    void set_connect_error(common::Error error) noexcept;
    void clear_connect_error() noexcept;

    // Fail only the next `count` connect() calls with error, then succeed.
    // Useful for retry tests: set_connect_fail_count(drm_error, 1) causes the
    // first connect to fail and the second to succeed automatically.
    void set_connect_fail_count(common::Error error, int count) noexcept;

    void set_send_error(common::Error error) noexcept;
    void clear_send_error() noexcept;

    void set_receive_error(common::Error error) noexcept;
    void clear_receive_error() noexcept;

    void set_close_error(common::Error error) noexcept;
    void clear_close_error() noexcept;

    // -----------------------------------------------------------------------
    // State inspection
    // -----------------------------------------------------------------------

    // The URL supplied to the most recent connect() call (empty if never called).
    [[nodiscard]] const std::string& connected_url() const noexcept;

    // All URLs passed to connect() in call order (one per attempt).
    [[nodiscard]] const std::vector<std::string>& connect_urls() const noexcept;

    // All payloads passed to send_text(), in call order.
    [[nodiscard]] const std::vector<std::string>& sent_messages() const noexcept;

    // Number of times connect() has been called (regardless of success).
    [[nodiscard]] int connect_count() const noexcept;

    // Number of times send_text() has been called (regardless of success).
    [[nodiscard]] int send_count() const noexcept;

    // True after close() has been called at least once.
    [[nodiscard]] bool is_closed() const noexcept;

    // -----------------------------------------------------------------------
    // IWebSocketClient
    // -----------------------------------------------------------------------

    [[nodiscard]] common::Result<void>           connect(std::string_view url)     override;
    [[nodiscard]] common::Result<void>           send_text(std::string_view payload) override;
    [[nodiscard]] common::Result<WebSocketMessage> receive()                        override;
    [[nodiscard]] common::Result<void>           close()                            override;

private:
    std::string                      connected_url_;
    std::vector<std::string>         connect_urls_;
    std::vector<std::string>         sent_messages_;
    std::queue<WebSocketMessage>     incoming_;
    int                              connect_count_{0};
    int                              send_count_{0};
    bool                             closed_{false};

    std::optional<common::Error>     connect_error_;
    std::optional<common::Error>     connect_fail_error_;
    int                              connect_fail_remaining_{0};
    std::optional<common::Error>     send_error_;
    std::optional<common::Error>     receive_error_;
    std::optional<common::Error>     close_error_;
};

} // namespace edge_tts::communication
