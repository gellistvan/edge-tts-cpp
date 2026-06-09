#include "communication/ConnectionMetadata.hpp"

namespace edge_tts::communication {

ConnectionMetadataFactory::ConnectionMetadataFactory(
    common::IdGenerator& ids) noexcept
    : ids_(ids)
{}

ConnectionMetadata ConnectionMetadataFactory::create_for_request()
{
    // Reference: communicate.py
    //   ConnectionId = connect_id() = uuid.uuid4().hex  (URL query param)
    //   request_id   = connect_id() = uuid.uuid4().hex  (X-RequestId header)
    // Two separate calls → two distinct UUID v4 values.
    return {
        .connection_id = ids_.uuid_v4_without_hyphens(),
        .request_id    = ids_.uuid_v4_without_hyphens(),
    };
}

} // namespace edge_tts::communication
