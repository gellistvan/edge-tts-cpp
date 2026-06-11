#include "communication/ConnectionMetadata.hpp"

namespace edge_tts::communication {

ConnectionMetadataFactory::ConnectionMetadataFactory(
    common::IdGenerator& ids) noexcept
    : ids_(ids)
{}

ConnectionMetadata ConnectionMetadataFactory::create_for_request()
{
    // Two separate UUID v4 values: one for the URL ConnectionId, one for X-RequestId.
    return {
        .connection_id = ids_.uuid_v4_without_hyphens(),
        .request_id    = ids_.uuid_v4_without_hyphens(),
    };
}

} // namespace edge_tts::communication
