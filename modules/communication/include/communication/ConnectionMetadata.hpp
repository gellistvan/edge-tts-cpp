#pragma once

#include "common/IdGenerator.hpp"

#include <string>

namespace edge_tts::communication {

// A pair of identifiers required for one Edge TTS WebSocket request cycle.
//
//
//
// Both IDs are independently generated values; 
struct ConnectionMetadata {
    std::string connection_id;  // 32 lowercase hex chars, no hyphens
    std::string request_id;     // 32 lowercase hex chars, no hyphens
};

// Generates ConnectionMetadata pairs using the injected IdGenerator.
//
// Each call to create_for_request() produces a fresh ConnectionMetadata with
// calls to connect_id()).
//
// The IdGenerator is held by reference — callers own the generator and must
// ensure it outlives the factory.
class ConnectionMetadataFactory {
public:
    explicit ConnectionMetadataFactory(common::IdGenerator& ids) noexcept;

    // Returns a new ConnectionMetadata with freshly generated IDs.
    [[nodiscard]] ConnectionMetadata create_for_request();

private:
    common::IdGenerator& ids_;
};

} // namespace edge_tts::communication
