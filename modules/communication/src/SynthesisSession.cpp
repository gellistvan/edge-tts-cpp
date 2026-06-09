#include "communication/SynthesisSession.hpp"
#include "common/Error.hpp"
#include "communication/HttpDate.hpp"
#include "communication/IncomingMessage.hpp"
#include "core/Chunk.hpp"

#include <chrono>

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
    const common::IClock&      clock,
    RetryPolicy                retry_policy)
    : websocket_(websocket)
    , protocol_(protocol)
    , config_(std::move(config))
    , token_provider_(token_provider)
    , metadata_factory_(metadata_factory)
    , clock_(clock)
    , retry_policy_(retry_policy)
{}

// -------------------------------------------------------------------------
// Per-chunk helper: send + receive loop.  Assumes websocket is already open.
// Appends results to out_chunks.  Does NOT close the websocket.
//
// offset_compensation: ticks to add to each BoundaryChunk.offset_ticks.
//   Value is computed from cumulative audio bytes of all previous chunks.
//   For the first chunk this is 0.
//
// out_audio_bytes: populated with the total audio bytes received this chunk.
//   Caller uses this to update the cumulative count for the next compensation.
// -------------------------------------------------------------------------
static common::Result<void> run_one_chunk(
    IWebSocketClient&            websocket,
    EdgeProtocol&                protocol,
    const core::TtsConfig&       tts_config,
    const std::string&           text,
    const ConnectionMetadata&    metadata,
    std::int64_t                 offset_compensation,
    std::int64_t&                out_audio_bytes,
    std::vector<core::TtsChunk>& out_chunks)
{
    out_audio_bytes = 0;

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
            case IncomingMessageKind::audio: {
                audio_received = true;
                // Count bytes before moving the chunk.
                const auto& ac = std::get<core::AudioChunk>(*msg.chunk);
                out_audio_bytes += static_cast<std::int64_t>(ac.data.size());
                out_chunks.push_back(std::move(*msg.chunk));
                break;
            }
            case IncomingMessageKind::boundary: {
                // Apply offset compensation before yielding.
                //   current_offset = meta_obj["Data"]["Offset"] + offset_compensation
                auto bc = std::get<core::BoundaryChunk>(std::move(*msg.chunk));
                bc.offset_ticks += offset_compensation;
                out_chunks.push_back(std::move(bc));
                break;
            }
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

    // Cumulative audio bytes across all completed chunks.
    std::int64_t cumulative_audio_bytes = 0;

    for (const auto& text : text_chunks) {
        // Compute offset compensation for boundaries in this chunk.
        //   offset_compensation = cumulative_audio_bytes * 8 * TICKS_PER_SECOND
        //                         // MP3_BITRATE_BPS
        const std::int64_t offset_compensation =
            cumulative_audio_bytes * 8LL * 10'000'000LL / 48'000LL;

        // Retry loop — reference: communicate.py stream() try/except around
        // __stream(), retrying once on ClientResponseError(status=403).
        for (int attempt = 0; ; ++attempt) {
            // --- Generate metadata for this chunk ----------------------------
            // New metadata per attempt → new ConnectionId each time.
            auto metadata = metadata_factory_.create_for_request();

            // --- Build WebSocket URL -----------------------------------------
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
                if (retry_policy_.should_retry(conn.error(), attempt)) {
                    // the token provider before retrying.
                    if (conn.error().has_context()) {
                        auto server_ts = parse_http_date(conn.error().context());
                        if (server_ts) {
                            const double now_sec = std::chrono::duration<double>(
                                clock_.now().time_since_epoch()).count();
                            const double skew =
                                static_cast<double>(*server_ts)
                                - (now_sec + token_provider_.clock_skew_seconds());
                            token_provider_.adjust_clock_skew(skew);
                        }
                    }
                    continue;
                }
                return common::Result<std::vector<core::TtsChunk>>::fail(conn.error());
            }

            // --- Run chunk (send + receive); always close after --------------
            std::int64_t chunk_audio_bytes = 0;
            auto chunk_result = run_one_chunk(
                websocket_, protocol_, tts_config, text, metadata,
                offset_compensation, chunk_audio_bytes, all_chunks);

            (void)websocket_.close();

            if (!chunk_result)
                return common::Result<std::vector<core::TtsChunk>>::fail(
                    chunk_result.error());

            // Update cumulative bytes after a successful chunk.
            // cumulative_audio_bytes before computing the next compensation.
            cumulative_audio_bytes += chunk_audio_bytes;
            break;  // chunk done, move to next
        }
    }

    return common::Result<std::vector<core::TtsChunk>>::ok(std::move(all_chunks));
}

} // namespace edge_tts::communication
