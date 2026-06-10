#pragma once

#include "common/CancellationToken.hpp"
#include "common/Clock.hpp"
#include "common/Result.hpp"
#include "communication/ConnectionMetadata.hpp"
#include "communication/EdgeProtocol.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/IWebSocketClient.hpp"
#include "communication/RetryPolicy.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"

#include <span>
#include <string>
#include <vector>

namespace edge_tts::communication {

// Orchestrates one complete synthesis session against the Edge TTS service.
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
//   8. Verify audio was received
//   9. Close WebSocket (always)
//  10. Repeat from 1 for the next text chunk
//
// No text chunking, no subtitle generation — both are caller responsibilities.
//
// All injected objects are held by reference; callers must ensure they outlive
// the session.
class SynthesisSession {
public:
    SynthesisSession(IWebSocketClient&              websocket,
                     EdgeProtocol&                  protocol,
                     EdgeServiceConfig              config,
                     EdgeTokenProvider&             token_provider,
                     ConnectionMetadataFactory&     metadata_factory,
                     const common::IClock&          clock,
                     common::CancellationToken      cancel_token = {},
                     RetryPolicy                    retry_policy = {});

    // Run a synthesis session for all text_chunks.
    //
    // CONTRACT: text_chunks must be XML-escaped strings produced by
    // serialization::TextChunker.  Each chunk is passed directly to
    // EdgeProtocol::build_ssml_frame, which embeds it verbatim.  Passing raw
    // (unescaped) text will produce malformed SSML.
    //
    // Each text chunk opens a new WebSocket connection.
    //
    // Returns all accumulated TtsChunk events (AudioChunk + BoundaryChunk) in
    // the order they arrived from the service.
    //
    // Errors:
    //   - token generation failure    → protocol_error
    //   - connect failure             → network_error
    //   - send failure                → network_error
    //   - receive failure             → network_error or timeout
    //   - parse_incoming failure      → protocol_error
    //   - no audio received           → service_error
    //   - cancel_token.is_cancelled() → cancelled (checked before each chunk and
    //                                   before each receive() call in the loop)
    //
    // On any error, the WebSocket is closed before returning.
    // Close errors are silently ignored.
    [[nodiscard]] common::Result<std::vector<core::TtsChunk>> synthesize(
        const core::TtsConfig&           tts_config,
        std::span<const std::string>     text_chunks);

private:
    IWebSocketClient&          websocket_;
    EdgeProtocol&              protocol_;
    EdgeServiceConfig          config_;
    EdgeTokenProvider&         token_provider_;
    ConnectionMetadataFactory& metadata_factory_;
    const common::IClock&      clock_;
    common::CancellationToken  cancel_token_;
    RetryPolicy                retry_policy_;
};

} // namespace edge_tts::communication
