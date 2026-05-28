#pragma once

#include "edge_tts/communication/Transport.hpp"

namespace edge_tts::communication {

class WebSocketTransport final : public ITransport {
public:
    void connect(const std::string& url) override;
    void send_text(const std::string& message) override;
    RawMessage receive() override;
    void close() override;
};

} // namespace edge_tts::communication
