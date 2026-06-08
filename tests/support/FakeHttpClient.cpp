#include "edge_tts/communication/FakeHttpClient.hpp"

namespace edge_tts::communication {

void FakeHttpClient::set_response(HttpResponse response) noexcept
{
    response_ = std::move(response);
    while (!response_queue_.empty()) response_queue_.pop();
    error_.reset();
}

void FakeHttpClient::push_response(HttpResponse response)
{
    response_queue_.push(std::move(response));
}

void FakeHttpClient::set_error(common::Error error) noexcept
{
    error_ = std::move(error);
}

void FakeHttpClient::clear_error() noexcept
{
    error_.reset();
}

const std::optional<HttpRequest>& FakeHttpClient::last_request() const noexcept
{
    return last_request_;
}

int FakeHttpClient::send_count() const noexcept
{
    return send_count_;
}

common::Result<HttpResponse> FakeHttpClient::send(const HttpRequest& request)
{
    last_request_ = request;
    ++send_count_;

    if (error_.has_value())
        return common::Result<HttpResponse>::fail(*error_);

    if (!response_queue_.empty()) {
        HttpResponse resp = std::move(response_queue_.front());
        response_queue_.pop();
        return common::Result<HttpResponse>::ok(std::move(resp));
    }

    return common::Result<HttpResponse>::ok(response_);
}

} // namespace edge_tts::communication
