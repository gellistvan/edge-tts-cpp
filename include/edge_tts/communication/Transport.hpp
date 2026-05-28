#pragma once

#include <string>
#include <vector>

namespace edge_tts::communication {

struct RawMessage {
    bool binary{};
    std::vector<unsigned char> payload;
};

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual void connect(const std::string& url) = 0;
    virtual void send_text(const std::string& message) = 0;
    virtual RawMessage receive() = 0;
    virtual void close() = 0;
};

} // namespace edge_tts::communication
