#include "edge_tts/communication/FakeWebSocketClient.hpp"
#include "edge_tts/communication/IWebSocketClient.hpp"
#include "edge_tts/communication/WebSocketMessage.hpp"
#include "edge_tts/common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstddef>
#include <string>

using edge_tts::communication::FakeWebSocketClient;
using edge_tts::communication::IWebSocketClient;
using edge_tts::communication::WebSocketMessage;
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static WebSocketMessage make_text(const std::string& text) {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text = text;
    return m;
}

static WebSocketMessage make_binary(std::vector<std::byte> data) {
    WebSocketMessage m;
    m.type   = WebSocketMessage::Type::binary;
    m.binary = std::move(data);
    return m;
}

// ---------------------------------------------------------------------------
// connect() captures URL
// ---------------------------------------------------------------------------

TEST(FakeWebSocketClient, ConnectCapturesUrl) {
    FakeWebSocketClient fake;
    auto r = fake.connect("wss://speech.platform.bing.com/edge/v1");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(fake.connected_url(), "wss://speech.platform.bing.com/edge/v1");
}

TEST(FakeWebSocketClient, ConnectCountStartsAtZero) {
    FakeWebSocketClient fake;
    EXPECT_EQ(fake.connect_count(), 0);
}

TEST(FakeWebSocketClient, ConnectIncreasesCount) {
    FakeWebSocketClient fake;
    (void)fake.connect("wss://a");
    (void)fake.connect("wss://b");
    EXPECT_EQ(fake.connect_count(), 2);
}

TEST(FakeWebSocketClient, ConnectUpdatesUrlOnEachCall) {
    FakeWebSocketClient fake;
    (void)fake.connect("wss://first");
    (void)fake.connect("wss://second");
    EXPECT_EQ(fake.connected_url(), "wss://second");
}

TEST(FakeWebSocketClient, ConnectSetsClosedFalse) {
    FakeWebSocketClient fake;
    (void)fake.close();
    EXPECT_TRUE(fake.is_closed());
    (void)fake.connect("wss://a");
    EXPECT_FALSE(fake.is_closed());
}

// ---------------------------------------------------------------------------
// send_text() captures payloads
// ---------------------------------------------------------------------------

TEST(FakeWebSocketClient, SendTextCapturesPayload) {
    FakeWebSocketClient fake;
    (void)fake.connect("wss://a");
    (void)fake.send_text("hello");
    EXPECT_EQ(fake.sent_messages().size(), 1u);
    EXPECT_EQ(fake.sent_messages()[0], "hello");
}

TEST(FakeWebSocketClient, SendTextCapturesMultiplePayloadsInOrder) {
    FakeWebSocketClient fake;
    (void)fake.connect("wss://a");
    (void)fake.send_text("speech.config");
    (void)fake.send_text("ssml");
    EXPECT_EQ(fake.sent_messages().size(), 2u);
    EXPECT_EQ(fake.sent_messages()[0], "speech.config");
    EXPECT_EQ(fake.sent_messages()[1], "ssml");
}

TEST(FakeWebSocketClient, SendCountStartsAtZero) {
    FakeWebSocketClient fake;
    EXPECT_EQ(fake.send_count(), 0);
}

TEST(FakeWebSocketClient, SendCountIncrementsPerCall) {
    FakeWebSocketClient fake;
    fake.connect("wss://a");
    fake.send_text("a");
    fake.send_text("b");
    EXPECT_EQ(fake.send_count(), 2);
}

// ---------------------------------------------------------------------------
// receive() pops queued messages in FIFO order
// ---------------------------------------------------------------------------

TEST(FakeWebSocketClient, ReceivePopsQueuedTextMessage) {
    FakeWebSocketClient fake;
    fake.push_incoming(make_text("Path:turn.end\r\n\r\n"));
    fake.connect("wss://a");

    const auto r = fake.receive();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->type, WebSocketMessage::Type::text);
    EXPECT_EQ(r->text, "Path:turn.end\r\n\r\n");
}

TEST(FakeWebSocketClient, ReceivePopsMessagesInFifoOrder) {
    FakeWebSocketClient fake;
    fake.push_incoming(make_text("first"));
    fake.push_incoming(make_text("second"));
    fake.push_incoming(make_text("third"));
    fake.connect("wss://a");

    EXPECT_EQ(fake.receive()->text, "first");
    EXPECT_EQ(fake.receive()->text, "second");
    EXPECT_EQ(fake.receive()->text, "third");
}

TEST(FakeWebSocketClient, ReceivePopsQueuedBinaryMessage) {
    FakeWebSocketClient fake;
    const std::vector<std::byte> payload = {std::byte{0xAB}, std::byte{0xCD}};
    fake.push_incoming(make_binary(payload));
    fake.connect("wss://a");

    const auto r = fake.receive();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->type, WebSocketMessage::Type::binary);
    EXPECT_EQ(r->binary, payload);
}

TEST(FakeWebSocketClient, IncomingQueueSizeDecrementsOnReceive) {
    FakeWebSocketClient fake;
    fake.push_incoming(make_text("a"));
    fake.push_incoming(make_text("b"));
    EXPECT_EQ(fake.incoming_queue_size(), 2u);

    fake.connect("wss://a");
    fake.receive();
    EXPECT_EQ(fake.incoming_queue_size(), 1u);

    fake.receive();
    EXPECT_EQ(fake.incoming_queue_size(), 0u);
}

// ---------------------------------------------------------------------------
// receive() with empty queue returns an error
// ---------------------------------------------------------------------------

TEST(FakeWebSocketClient, ReceiveEmptyQueueReturnsError) {
    FakeWebSocketClient fake;
    fake.connect("wss://a");
    const auto r = fake.receive();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
}

// ---------------------------------------------------------------------------
// Configured connect error
// ---------------------------------------------------------------------------

TEST(FakeWebSocketClient, ConnectErrorIsReturned) {
    FakeWebSocketClient fake;
    fake.set_connect_error(Error{ErrorCode::network_error, "refused"});
    const auto r = fake.connect("wss://a");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
}

TEST(FakeWebSocketClient, ConnectErrorStillCapturesUrl) {
    FakeWebSocketClient fake;
    fake.set_connect_error(Error{ErrorCode::network_error, "refused"});
    fake.connect("wss://captured");
    EXPECT_EQ(fake.connected_url(), "wss://captured");
}

TEST(FakeWebSocketClient, ConnectErrorPersistsUntilCleared) {
    FakeWebSocketClient fake;
    fake.set_connect_error(Error{ErrorCode::network_error, "refused"});
    EXPECT_FALSE(fake.connect("wss://a").has_value());
    EXPECT_FALSE(fake.connect("wss://b").has_value());
    fake.clear_connect_error();
    EXPECT_TRUE(fake.connect("wss://c").has_value());
}

// ---------------------------------------------------------------------------
// Configured send error
// ---------------------------------------------------------------------------

TEST(FakeWebSocketClient, SendErrorIsReturned) {
    FakeWebSocketClient fake;
    fake.connect("wss://a");
    fake.set_send_error(Error{ErrorCode::network_error, "send failed"});
    const auto r = fake.send_text("hello");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
}

TEST(FakeWebSocketClient, SendErrorStillCapturesPayload) {
    FakeWebSocketClient fake;
    fake.connect("wss://a");
    fake.set_send_error(Error{ErrorCode::network_error, "send failed"});
    fake.send_text("captured");
    EXPECT_EQ(fake.sent_messages().size(), 1u);
    EXPECT_EQ(fake.sent_messages()[0], "captured");
}

TEST(FakeWebSocketClient, SendErrorPersistsUntilCleared) {
    FakeWebSocketClient fake;
    fake.connect("wss://a");
    fake.set_send_error(Error{ErrorCode::network_error, "fail"});
    EXPECT_FALSE(fake.send_text("a").has_value());
    EXPECT_FALSE(fake.send_text("b").has_value());
    fake.clear_send_error();
    EXPECT_TRUE(fake.send_text("c").has_value());
}

// ---------------------------------------------------------------------------
// Configured receive error (takes priority over queued messages)
// ---------------------------------------------------------------------------

TEST(FakeWebSocketClient, ReceiveErrorIsReturned) {
    FakeWebSocketClient fake;
    fake.connect("wss://a");
    fake.set_receive_error(Error{ErrorCode::network_error, "connection dropped"});
    const auto r = fake.receive();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
}

TEST(FakeWebSocketClient, ReceiveErrorTakesPriorityOverQueuedMessages) {
    FakeWebSocketClient fake;
    fake.push_incoming(make_text("should not be returned"));
    fake.connect("wss://a");
    fake.set_receive_error(Error{ErrorCode::network_error, "dropped"});
    const auto r = fake.receive();
    EXPECT_FALSE(r.has_value());
}

TEST(FakeWebSocketClient, ReceiveErrorPersistsUntilCleared) {
    FakeWebSocketClient fake;
    fake.connect("wss://a");
    fake.set_receive_error(Error{ErrorCode::network_error, "fail"});
    EXPECT_FALSE(fake.receive().has_value());
    fake.clear_receive_error();
    fake.push_incoming(make_text("ok"));
    EXPECT_TRUE(fake.receive().has_value());
}

// ---------------------------------------------------------------------------
// close() changes state
// ---------------------------------------------------------------------------

TEST(FakeWebSocketClient, IsClosedFalseInitially) {
    FakeWebSocketClient fake;
    EXPECT_FALSE(fake.is_closed());
}

TEST(FakeWebSocketClient, CloseChangesIsClosedToTrue) {
    FakeWebSocketClient fake;
    fake.connect("wss://a");
    EXPECT_FALSE(fake.is_closed());
    const auto r = fake.close();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(fake.is_closed());
}

TEST(FakeWebSocketClient, CloseWithoutConnectAlsoSetsClosedState) {
    FakeWebSocketClient fake;
    (void)fake.close();
    EXPECT_TRUE(fake.is_closed());
}

TEST(FakeWebSocketClient, CloseErrorIsReturned) {
    FakeWebSocketClient fake;
    fake.connect("wss://a");
    fake.set_close_error(Error{ErrorCode::network_error, "close failed"});
    const auto r = fake.close();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
}

TEST(FakeWebSocketClient, CloseErrorStillSetsClosedState) {
    FakeWebSocketClient fake;
    fake.connect("wss://a");
    fake.set_close_error(Error{ErrorCode::network_error, "close failed"});
    (void)fake.close();
    EXPECT_TRUE(fake.is_closed());
}

TEST(FakeWebSocketClient, CloseErrorPersistsUntilCleared) {
    FakeWebSocketClient fake;
    fake.set_close_error(Error{ErrorCode::network_error, "fail"});
    EXPECT_FALSE(fake.close().has_value());
    fake.clear_close_error();
    EXPECT_TRUE(fake.close().has_value());
}

// ---------------------------------------------------------------------------
// FakeWebSocketClient is usable via IWebSocketClient interface
// ---------------------------------------------------------------------------

TEST(FakeWebSocketClient, UsableViaInterface) {
    FakeWebSocketClient fake;
    fake.push_incoming(make_text("X-RequestId:x\r\nPath:turn.end\r\n\r\n"));
    IWebSocketClient& iface = fake;

    EXPECT_TRUE(iface.connect("wss://a").has_value());
    EXPECT_TRUE(iface.send_text("frame1").has_value());
    EXPECT_TRUE(iface.send_text("frame2").has_value());
    const auto msg = iface.receive();
    EXPECT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, WebSocketMessage::Type::text);
    EXPECT_TRUE(iface.close().has_value());
}

// ---------------------------------------------------------------------------
// Lifecycle: reference send order (speech.config then ssml)
// ---------------------------------------------------------------------------

TEST(FakeWebSocketClient, ReferenceSendOrder) {
    // communicate.py __stream():
    //   await send_command_request()  → speech.config text frame
    //   await send_ssml_request()     → ssml text frame
    //   async for received in websocket: ...
    FakeWebSocketClient fake;
    fake.push_incoming(make_text("X-RequestId:x\r\nPath:turn.end\r\n\r\n"));

    EXPECT_TRUE(fake.connect("wss://example.com?ConnectionId=abc").has_value());
    EXPECT_TRUE(fake.send_text("speech.config frame").has_value());
    EXPECT_TRUE(fake.send_text("ssml frame").has_value());

    // Receive loop
    const auto msg = fake.receive();
    EXPECT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, WebSocketMessage::Type::text);

    EXPECT_TRUE(fake.close().has_value());

    EXPECT_EQ(fake.sent_messages()[0], "speech.config frame");
    EXPECT_EQ(fake.sent_messages()[1], "ssml frame");
    EXPECT_EQ(fake.send_count(), 2);
    EXPECT_TRUE(fake.is_closed());
}
