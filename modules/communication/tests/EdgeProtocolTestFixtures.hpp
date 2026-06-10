#pragma once
// Shared fixture for EdgeProtocol tests.
#include "common/IdGenerator.hpp"
#include "communication/ConnectionMetadata.hpp"

namespace edge_tts::test {

inline communication::ConnectionMetadata make_metadata() {
    common::IdGenerator ids;
    communication::ConnectionMetadataFactory factory{ids};
    return factory.create_for_request();
}

} // namespace edge_tts::test
