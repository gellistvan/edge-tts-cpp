#include "edge_tts/communication/SynthesisSession.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/communication/IncomingMessage.hpp"
#include "edge_tts/core/Chunk.hpp"

#include <span>
#include <string>
#include <variant>
#include <vector>

namespace edge_tts::communication {

SynthesisSession::SynthesisSession(
    IWebSocketClient&          websocket,
    EdgeProtocol&              protocol,
    EdgeServiceConfig          config,
    EdgeTokenProvider&         token_provider,
    ConnectionMetadataFactory& metadata_factory,
    RetryPolicy                retry_policy)
    : websocket_(websocket)
    , protocol_(protocol)
    , config_(std::move(config))
    , token_provider_(token_provider)
    , metadata_factory_(metadata_factory)
    , retry_policy_(retry_policy)
{}

// -------------------------------------------------------------------------
// Per-chunk helper: send + receive loop.  Assumes websocket is already open.
// Appends results to out_chunks.  Does NOT close the websocket.
// -------------------------------------------------------------------------
static common::Result<void> run_one_chunk(
    IWebSocketClient&         websocket,
    EdgeProtocol&             protocol,
    const core::TtsConfig&    tts_config,
    const std::string&        text,
    const ConnectionMetadata& metadata,
    std::vector<core::TtsChunk>& out_chunks)
{
    // --- Send speech.config frame -------------------------------------------
    auto speech_cfg = protocol.build_speech_config_frame(tts_config, metadata);
    if (!speech_cfg)
        return common::Result<void>::fail(speech_cfg.error());

    auto send1 = websocket.send_text(*speech_cfg);
    if (!send1)
        return common::Result<void>::fail(send1.error());

    // --- Send SSML frame -----------------------------------------------------
    auto ssml_frame = protocol.build_ssml_frame(tts_config, text, metadata);
    if (!ssml_frame)
        return common::Result<void>::fail(ssml_frame.error());

    auto send2 = websocket.send_text(*ssml_frame);
    if (!send2)
        return common::Result<void>::fail(send2.error());

    // --- Receive loop --------------------------------------------------------
    // Reference: async for received in websocket:  (break on turn.end)
    bool audio_received = false;

    while (true) {
        auto recv = websocket.receive();
        if (!recv)
            return common::Result<void>::fail(recv.error());

        auto parsed = protocol.parse_incoming(*recv);
        if (!parsed)
            return common::Result<void>::fail(parsed.error());

        bool done = false;
        for (auto& msg : *parsed) {
            switch (msg.kind) {
            case IncomingMessageKind::audio:
                audio_received = true;
                out_chunks.push_back(std::move(*msg.chunk));
                break;
            case IncomingMessageKind::boundary:
                out_chunks.push_back(std::move(*msg.chunk));
                break;
            case IncomingMessageKind::turn_end:
                done = true;
                break;
            case IncomingMessageKind::ignored:
                break;
            }
            if (done) break;
        }
        if (done) break;
    }

    // Reference: if not audio_was_received: raise NoAudioReceived(...)
    if (!audio_received)
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::service_error,
                          "No audio was received. Please verify that your parameters are correct."});

    return common::Result<void>::ok();
}

// -------------------------------------------------------------------------
// synthesize
// -------------------------------------------------------------------------

common::Result<std::vector<core::TtsChunk>> SynthesisSession::synthesize(
    const core::TtsConfig&       tts_config,
    std::span<const std::string> text_chunks)
{
    std::vector<core::TtsChunk> all_chunks;

    for (const auto& text : text_chunks) {
        // Retry loop — reference: communicate.py stream() try/except around
        // __stream(), retrying once on ClientResponseError(status=403).
        for (int attempt = 0; ; ++attempt) {
            // --- Generate metadata for this chunk ----------------------------
            // New metadata per attempt → new ConnectionId each time.
            auto metadata = metadata_factory_.create_for_request();

            // --- Build WebSocket URL -----------------------------------------
            // Reference: f"{WSS_URL}&ConnectionId={connect_id()}"
            //            f"&Sec-MS-GEC={DRM.generate_sec_ms_gec()}"
            //            f"&Sec-MS-GEC-Version={SEC_MS_GEC_VERSION}"
            // Token is regenerated each attempt; if adjust_clock_skew() was
            // called between attempts the token will reflect the corrected clock.
            auto gec = token_provider_.sec_ms_gec();
            if (!gec)
                return common::Result<std::vector<core::TtsChunk>>::fail(gec.error());

            const std::string url =
                config_.websocket_endpoint
                + "&ConnectionId=" + metadata.connection_id
                + "&Sec-MS-GEC=" + *gec
                + "&Sec-MS-GEC-Version=" + token_provider_.sec_ms_gec_version();

            // --- Connect -----------------------------------------------------
            auto conn = websocket_.connect(url);
            if (!conn) {
                // Reference: `if e.status != 403: raise` — only drm_error retries.
                if (retry_policy_.should_retry(conn.error(), attempt)) {
                    // Token rejected; regenerate on next loop iteration.
                    // Caller may have called adjust_clock_skew() to shift the clock
                    // before this session, or the next sec_ms_gec() call uses the
                    // same clock (same token within the same 5-min bucket).
                    // Reference: DRM.handle_client_response_error(e) adjusts skew
                    // from the Date header; that extraction happens in WebSocketClient.
                    continue;
                }
                return common::Result<std::vector<core::TtsChunk>>::fail(conn.error());
            }

            // --- Run chunk (send + receive); always close after --------------
            // Reference: context manager ensures close on success and error.
            // Post-connect errors are NOT retried (Python only catches ws_connect errors).
            auto chunk_result = run_one_chunk(
                websocket_, protocol_, tts_config, text, metadata, all_chunks);

            (void)websocket_.close();

            if (!chunk_result)
                return common::Result<std::vector<core::TtsChunk>>::fail(
                    chunk_result.error());

            break;  // chunk done, move to next
        }
    }

    return common::Result<std::vector<core::TtsChunk>>::ok(std::move(all_chunks));
}

} // namespace edge_tts::communication
