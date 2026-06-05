#pragma once

#include "edge_tts/common/Result.hpp"
#include "edge_tts/communication/ConnectionMetadata.hpp"
#include "edge_tts/communication/EdgeProtocol.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/EdgeTokenProvider.hpp"
#include "edge_tts/communication/IWebSocketClient.hpp"
#include "edge_tts/communication/RetryPolicy.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"

#include <span>
#include <string>
#include <vector>

namespace edge_tts::communication {

// Orchestrates one complete synthesis session against the Edge TTS service.
//
// Reference: communicate.py Communicate.stream() + __stream()
//
// Session lifecycle per text chunk:
//   1. Generate ConnectionMetadata (connection_id + request_id)
//   2. Obtain Sec-MS-GEC token from EdgeTokenProvider
//   3. Build WebSocket URL:
//        {websocket_endpoint}&ConnectionId={connection_id}
//        &Sec-MS-GEC={token}&Sec-MS-GEC-Version={version}
//   4. Connect WebSocket
//   5. Send speech.config frame (text)
//   6. Send SSML frame (text)
//   7. Receive loop:
//        audio     → accumulate AudioChunk
//        boundary  → accumulate BoundaryChunk
//        turn_end  → break (chunk complete)
//        ignored   → continue
//        error     → close WebSocket, propagate error
//   8. Check audio was received (reference: NoAudioReceived)
//   9. Close WebSocket (always — reference: context manager exit)
//  10. Repeat from 1 for the next text chunk
//
// No text chunking, no subtitle generation — both are caller responsibilities.
//
// All injected objects are held by reference; callers must ensure they outlive
// the session.
class SynthesisSession {
public:
    SynthesisSession(IWebSocketClient&          websocket,
                     EdgeProtocol&              protocol,
                     EdgeServiceConfig          config,
                     EdgeTokenProvider&         token_provider,
                     ConnectionMetadataFactory& metadata_factory,
                     RetryPolicy                retry_policy = {});

    // Run a synthesis session for all text_chunks.
    //
    // Each chunk opens a new WebSocket connection (matching the Python reference
    // which calls __stream() per chunk, each opening its own ws_connect()).
    //
    // Returns all accumulated TtsChunk events (AudioChunk + BoundaryChunk) in
    // the order they arrived from the service.
    //
    // Errors:
    //   - token generation failure → protocol_error
    //   - connect failure          → network_error
    //   - send failure             → network_error
    //   - receive failure          → network_error
    //   - parse_incoming failure   → protocol_error
    //   - no audio received        → service_error (reference: NoAudioReceived)
    //
    // On any error, the WebSocket is closed before returning (matching Python's
    // context manager semantics). Close errors are silently ignored.
    [[nodiscard]] common::Result<std::vector<core::TtsChunk>> synthesize(
        const core::TtsConfig&           tts_config,
        std::span<const std::string>     text_chunks);

private:
    IWebSocketClient&          websocket_;
    EdgeProtocol&              protocol_;
    EdgeServiceConfig          config_;
    EdgeTokenProvider&         token_provider_;
    ConnectionMetadataFactory& metadata_factory_;
    RetryPolicy                retry_policy_;
};

} // namespace edge_tts::communication
