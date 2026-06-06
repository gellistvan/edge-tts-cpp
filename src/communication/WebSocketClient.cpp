#include "edge_tts/communication/WebSocketClient.hpp"
#include "edge_tts/common/Error.hpp"

// ixwebsocket headers are included here only, never in the public header.
// Guard ensures the translation unit compiles when the submodule is absent.
#ifdef EDGE_TTS_HAVE_IXWEBSOCKET
#  include <ixwebsocket/IXNetSystem.h>
#  include <ixwebsocket/IXSocketTLSOptions.h>
#  include <ixwebsocket/IXWebSocket.h>
#endif

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>
#include <variant>

namespace edge_tts::communication {

// ---------------------------------------------------------------------------
// Pimpl implementation
// ---------------------------------------------------------------------------

#ifdef EDGE_TTS_HAVE_IXWEBSOCKET

// Each item in the incoming queue is either a message or a terminal error.
using QueueItem = std::variant<WebSocketMessage, common::Error>;

struct WebSocketClient::Impl {
    WebSocketClientOptions options;
    ix::WebSocket          ws;

    std::mutex              mtx;
    std::condition_variable cv;
    std::queue<QueueItem>   incoming;

    // Background thread that pumps ws.run() after a successful connect().
    std::thread run_thread;

    explicit Impl(WebSocketClientOptions opts)
        : options(std::move(opts))
    {
        ix::initNetSystem();
    }

    ~Impl() { stop_and_join(); }

    void stop_and_join() {
        ws.stop();
        if (run_thread.joinable())
            run_thread.join();
    }
};

#else

// Stub Impl: no ixwebsocket available.
struct WebSocketClient::Impl {
    WebSocketClientOptions options;
    explicit Impl(WebSocketClientOptions opts) : options(std::move(opts)) {}
};

#endif

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

WebSocketClient::WebSocketClient(WebSocketClientOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{}

WebSocketClient::~WebSocketClient() = default;

const WebSocketClientOptions& WebSocketClient::options() const noexcept {
    return impl_->options;
}

// ---------------------------------------------------------------------------
// ixwebsocket-backed implementation
// ---------------------------------------------------------------------------

#ifdef EDGE_TTS_HAVE_IXWEBSOCKET

common::Result<void> WebSocketClient::connect(std::string_view url)
{
    // Stop any previous connection before reusing this client.
    impl_->stop_and_join();
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        while (!impl_->incoming.empty())
            impl_->incoming.pop();
    }

    // --- URL ------------------------------------------------------------------
    impl_->ws.setUrl(std::string(url));

    // --- TLS ------------------------------------------------------------------
    // Reference: voices.py / communicate.py use ssl.create_default_context() which
    // uses the system CA bundle.  "SYSTEM" tells ixwebsocket to do the same.
    ix::SocketTLSOptions tls_opts;
    tls_opts.caFile = "SYSTEM";
    impl_->ws.setTLSOptions(tls_opts);

    // --- Extra upgrade headers ------------------------------------------------
    // Reference: communicate.py WSS_HEADERS (Pragma, Cache-Control, Origin,
    // User-Agent, Accept-Encoding, Accept-Language, Cookie).
    ix::WebSocketHttpHeaders headers;
    for (const auto& [k, v] : impl_->options.extra_headers)
        headers[k] = v;
    impl_->ws.setExtraHeaders(headers);

    // --- Timeouts -------------------------------------------------------------
    const int connect_secs = static_cast<int>(
        impl_->options.connect_timeout.count() / 1000);
    impl_->ws.setHandshakeTimeout(connect_secs);

    // Disable auto-reconnect: the caller owns the reconnect strategy.
    impl_->ws.disableAutomaticReconnection();

    // --- Message callback -----------------------------------------------------
    // Called from the ixwebsocket run thread.  Pushes items to the queue and
    // notifies the waiter in receive().  The lock is released before notify so
    // the waiter can acquire it immediately.
    impl_->ws.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        QueueItem item;
        bool push = false;

        switch (msg->type) {
        case ix::WebSocketMessageType::Message: {
            WebSocketMessage wsmsg;
            if (msg->binary) {
                wsmsg.type = WebSocketMessage::Type::binary;
                wsmsg.binary.resize(msg->str.size());
                std::memcpy(wsmsg.binary.data(), msg->str.data(), msg->str.size());
            } else {
                wsmsg.type = WebSocketMessage::Type::text;
                wsmsg.text = msg->str;
            }
            item = std::move(wsmsg);
            push = true;
            break;
        }
        case ix::WebSocketMessageType::Error:
            item = common::Error{
                common::ErrorCode::network_error,
                msg->errorInfo.reason.empty()
                    ? "WebSocket error"
                    : msg->errorInfo.reason};
            push = true;
            break;
        case ix::WebSocketMessageType::Close:
            // Push a terminal error only if the close was unexpected (not our
            // own ws.stop() call).  The code 1000 is normal closure.
            if (msg->closeInfo.code != 1000 || !msg->closeInfo.reason.empty()) {
                item = common::Error{
                    common::ErrorCode::network_error,
                    "WebSocket closed by server"};
                push = true;
            }
            break;
        default:
            break;
        }

        if (push) {
            {
                std::lock_guard<std::mutex> lock(impl_->mtx);
                impl_->incoming.push(std::move(item));
            }
            impl_->cv.notify_one();
        }
    });

    // --- Synchronous connect --------------------------------------------------
    const auto init = impl_->ws.connect(connect_secs);
    if (!init.success) {
        // HTTP 403 from the upgrade response indicates a DRM token rejection.
        // Reference: communicate.py catches aiohttp.ClientResponseError(status=403)
        // and retries after adjusting the clock skew via the server Date header.
        if (init.http_status == 403) {
            // Extract the Date response header to allow clock-skew correction.
            // The header map uses case-insensitive comparison (CaseInsensitiveLess).
            std::string date_ctx;
            const auto it = init.headers.find("Date");
            if (it != init.headers.end())
                date_ctx = it->second;
            return common::Result<void>::fail(
                common::Error{common::ErrorCode::drm_error,
                              init.errorStr.empty() ? "WebSocket connect failed"
                                                    : init.errorStr,
                              date_ctx});
        }
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::network_error,
                          init.errorStr.empty() ? "WebSocket connect failed"
                                                : init.errorStr});
    }

    // --- Start receive pump ---------------------------------------------------
    // ws.run() processes frames and fires the callback until ws.stop() is called.
    impl_->run_thread = std::thread([this]() {
        impl_->ws.run();
    });

    return common::Result<void>::ok();
}

common::Result<void> WebSocketClient::send_text(std::string_view payload)
{
    // sendText validates UTF-8 and sends a text frame.
    const auto info = impl_->ws.sendText(std::string(payload));
    if (!info.success) {
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::network_error,
                          "WebSocket send_text failed"});
    }
    return common::Result<void>::ok();
}

common::Result<WebSocketMessage> WebSocketClient::receive()
{
    std::unique_lock<std::mutex> lock(impl_->mtx);
    const bool arrived = impl_->cv.wait_for(
        lock,
        impl_->options.read_timeout,
        [this] { return !impl_->incoming.empty(); });

    if (!arrived) {
        return common::Result<WebSocketMessage>::fail(
            common::Error{common::ErrorCode::timeout,
                          "WebSocket receive timed out"});
    }

    QueueItem item = std::move(impl_->incoming.front());
    impl_->incoming.pop();

    if (auto* msg = std::get_if<WebSocketMessage>(&item))
        return common::Result<WebSocketMessage>::ok(std::move(*msg));

    return common::Result<WebSocketMessage>::fail(
        std::get<common::Error>(std::move(item)));
}

common::Result<void> WebSocketClient::close()
{
    impl_->stop_and_join();
    return common::Result<void>::ok();
}

// ---------------------------------------------------------------------------
// Stub: ixwebsocket submodule not available
// ---------------------------------------------------------------------------

#else

common::Result<void> WebSocketClient::connect(std::string_view) {
    return common::Result<void>::fail(
        common::Error{common::ErrorCode::unsupported,
                      "WebSocketClient requires the ixwebsocket submodule. "
                      "Run: git submodule update --init submodules/ixwebsocket"});
}

common::Result<void> WebSocketClient::send_text(std::string_view) {
    return common::Result<void>::fail(
        common::Error{common::ErrorCode::unsupported,
                      "WebSocketClient requires the ixwebsocket submodule."});
}

common::Result<WebSocketMessage> WebSocketClient::receive() {
    return common::Result<WebSocketMessage>::fail(
        common::Error{common::ErrorCode::unsupported,
                      "WebSocketClient requires the ixwebsocket submodule."});
}

common::Result<void> WebSocketClient::close() {
    return common::Result<void>::ok();
}

#endif // EDGE_TTS_HAVE_IXWEBSOCKET

} // namespace edge_tts::communication
