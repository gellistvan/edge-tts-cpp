#include "edge_tts/communication/FakeWebSocketClient.hpp"
#include "edge_tts/common/Error.hpp"

namespace edge_tts::communication {

// -----------------------------------------------------------------------
// Incoming message queue
// -----------------------------------------------------------------------

void FakeWebSocketClient::push_incoming(WebSocketMessage message)
{
    incoming_.push(std::move(message));
}

std::size_t FakeWebSocketClient::incoming_queue_size() const noexcept
{
    return incoming_.size();
}

// -----------------------------------------------------------------------
// Error injection
// -----------------------------------------------------------------------

void FakeWebSocketClient::set_connect_error(common::Error error) noexcept
{
    connect_error_ = std::move(error);
}
void FakeWebSocketClient::clear_connect_error() noexcept { connect_error_.reset(); }

void FakeWebSocketClient::set_send_error(common::Error error) noexcept
{
    send_error_ = std::move(error);
}
void FakeWebSocketClient::clear_send_error() noexcept { send_error_.reset(); }

void FakeWebSocketClient::set_receive_error(common::Error error) noexcept
{
    receive_error_ = std::move(error);
}
void FakeWebSocketClient::clear_receive_error() noexcept { receive_error_.reset(); }

void FakeWebSocketClient::set_close_error(common::Error error) noexcept
{
    close_error_ = std::move(error);
}
void FakeWebSocketClient::clear_close_error() noexcept { close_error_.reset(); }

// -----------------------------------------------------------------------
// State inspection
// -----------------------------------------------------------------------

const std::string& FakeWebSocketClient::connected_url() const noexcept
{
    return connected_url_;
}

const std::vector<std::string>& FakeWebSocketClient::sent_messages() const noexcept
{
    return sent_messages_;
}

int FakeWebSocketClient::connect_count() const noexcept { return connect_count_; }
int FakeWebSocketClient::send_count() const noexcept    { return send_count_; }
bool FakeWebSocketClient::is_closed() const noexcept    { return closed_; }

// -----------------------------------------------------------------------
// IWebSocketClient
// -----------------------------------------------------------------------

common::Result<void> FakeWebSocketClient::connect(std::string_view url)
{
    ++connect_count_;
    connected_url_ = std::string(url);
    if (connect_error_)
        return common::Result<void>::fail(*connect_error_);
    closed_ = false;
    return common::Result<void>::ok();
}

common::Result<void> FakeWebSocketClient::send_text(std::string_view payload)
{
    ++send_count_;
    sent_messages_.emplace_back(payload);
    if (send_error_)
        return common::Result<void>::fail(*send_error_);
    return common::Result<void>::ok();
}

common::Result<WebSocketMessage> FakeWebSocketClient::receive()
{
    if (receive_error_)
        return common::Result<WebSocketMessage>::fail(*receive_error_);

    if (incoming_.empty())
        return common::Result<WebSocketMessage>::fail(
            common::Error{common::ErrorCode::network_error, "receive queue empty"});

    WebSocketMessage msg = std::move(incoming_.front());
    incoming_.pop();
    return common::Result<WebSocketMessage>::ok(std::move(msg));
}

common::Result<void> FakeWebSocketClient::close()
{
    closed_ = true;
    if (close_error_)
        return common::Result<void>::fail(*close_error_);
    return common::Result<void>::ok();
}

} // namespace edge_tts::communication
