#include "api/SpeechSynthesizer.hpp"
#include "api/FileWriter.hpp"
#include "common/Clock.hpp"
#include "common/Error.hpp"
#include "common/IdGenerator.hpp"
#include "communication/ConnectionMetadata.hpp"
#include "communication/EdgeProtocol.hpp"
#include "communication/EdgeRequestHeaders.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/RetryPolicy.hpp"
#include "communication/SynthesisSession.hpp"
#include "communication/WebSocketClient.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "serialization/TextChunker.hpp"
#include "subtitles/SubMaker.hpp"

#include <memory>
#include <utility>
#include <variant>

namespace edge_tts::api {

// ---------------------------------------------------------------------------
// Production synthesizer — owns the full networking stack.
//
// All members that hold references to other members (EdgeTokenProvider,
// EdgeProtocol, ConnectionMetadataFactory, SynthesisSession) must be declared
// AFTER the objects they reference so that initialization order is correct.
// The struct is heap-allocated (shared_ptr) so all addresses are stable for
// the lifetime of the owning SynthesizerFn lambda.
// ---------------------------------------------------------------------------

struct ProductionSynthesizer {
    // Owned objects — initialization order matches declaration order.
    common::SystemClock                      clock;
    common::IdGenerator                      ids;
    communication::EdgeServiceConfig         service_config;
    communication::EdgeTokenProvider         token_provider;     // holds ref: clock
    communication::EdgeProtocol              protocol;           // holds ref: clock
    communication::ConnectionMetadataFactory metadata_factory;   // holds ref: ids
    communication::WebSocketClient           websocket;          // owns WS options
    communication::SynthesisSession          session;            // holds refs: all above

    ProductionSynthesizer(const SynthesisOptions& opts,
                          common::CancellationToken cancel_token);

    // Non-copyable, non-movable — members hold references to each other.
    ProductionSynthesizer(const ProductionSynthesizer&)            = delete;
    ProductionSynthesizer& operator=(const ProductionSynthesizer&) = delete;
    ProductionSynthesizer(ProductionSynthesizer&&)                 = delete;
    ProductionSynthesizer& operator=(ProductionSynthesizer&&)      = delete;
};

// Build WebSocketClientOptions from SynthesisOptions + service config.
// Separate function so it can be called safely in the member-initializer list
// after service_config and ids have been initialized.
static communication::WebSocketClientOptions make_ws_options(
    const SynthesisOptions&                  opts,
    const communication::EdgeServiceConfig&  svc_cfg,
    common::IdGenerator&                     ids)
{
    communication::WebSocketClientOptions ws;
    ws.connect_timeout = opts.ws_connect_timeout;
    ws.read_timeout    = opts.ws_read_timeout;
    // Build the HTTP upgrade headers (including fresh MUID cookie).
    ws.extra_headers   = communication::build_websocket_headers(svc_cfg, ids);
    return ws;
}

ProductionSynthesizer::ProductionSynthesizer(const SynthesisOptions& opts,
                                             common::CancellationToken cancel_token)
    : clock{}
    , ids{}
    , service_config{communication::default_edge_service_config()}
    , token_provider{service_config, clock}
    , protocol{clock}
    , metadata_factory{ids}
    , websocket{make_ws_options(opts, service_config, ids)}
    , session{websocket, protocol, service_config, token_provider, metadata_factory,
              clock, std::move(cancel_token)}
{}

// Build the production SynthesizerFn that drives the real communication stack.
// No network work is performed here — synthesis is deferred to synthesize()/save().
// The cancel_token is shared with the ProductionSynthesizer so that
// SpeechSynthesizer::cancel() propagates into the session's receive loop.
static SynthesizerFn make_production_synthesizer(const SynthesisOptions& opts,
                                                  common::CancellationToken cancel_token)
{
    auto synth = std::make_shared<ProductionSynthesizer>(opts, std::move(cancel_token));
    return [synth](const core::TtsConfig&       cfg,
                   std::span<const std::string> chunks)
               -> common::Result<std::vector<core::TtsChunk>>
    {
        return synth->session.synthesize(cfg, chunks);
    };
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SpeechSynthesizer::SpeechSynthesizer(std::string text, core::TtsConfig config)
    : text_(std::move(text))
    , config_(std::move(config))
    , options_{}
    , cancel_token_{}
    , synthesizer_(make_production_synthesizer(options_, cancel_token_))
{}

SpeechSynthesizer::SpeechSynthesizer(std::string text, core::TtsConfig config,
                                     SynthesisOptions options)
    : text_(std::move(text))
    , config_(std::move(config))
    , options_(std::move(options))
    , cancel_token_{}
    , synthesizer_(make_production_synthesizer(options_, cancel_token_))
{}

SpeechSynthesizer::SpeechSynthesizer(std::string text, core::TtsConfig config,
                                     SynthesizerFn synthesizer)
    : text_(std::move(text))
    , config_(std::move(config))
    , options_{}
    , cancel_token_{}
    , synthesizer_(std::move(synthesizer))
{}

SpeechSynthesizer::SpeechSynthesizer(std::string text, core::TtsConfig config,
                                     SynthesisOptions options, SynthesizerFn synthesizer)
    : text_(std::move(text))
    , config_(std::move(config))
    , options_(std::move(options))
    , cancel_token_{}
    , synthesizer_(std::move(synthesizer))
{}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const std::string&      SpeechSynthesizer::text()    const noexcept { return text_; }
const core::TtsConfig&  SpeechSynthesizer::config()  const noexcept { return config_; }
const SynthesisOptions& SpeechSynthesizer::options() const noexcept { return options_; }

void SpeechSynthesizer::cancel() noexcept { cancel_token_.cancel(); }

// ---------------------------------------------------------------------------
// Internal synthesis pipeline
// ---------------------------------------------------------------------------

common::Result<std::vector<core::TtsChunk>> SpeechSynthesizer::run_pipeline()
{
    // 0. Check for cancellation before doing any work.
    if (cancel_token_.is_cancelled())
        return common::Result<std::vector<core::TtsChunk>>::fail(
            common::Error{common::ErrorCode::cancelled,
                          "synthesis was cancelled"});

    // 1. Reject unsupported options before any network work.
    if (options_.proxy.has_value())
        return common::Result<std::vector<core::TtsChunk>>::fail(
            common::Error{common::ErrorCode::unsupported,
                          "proxy is not supported"});

    // 2. Validate TTS configuration.
    if (auto r = core::validate_tts_config(config_); !r)
        return common::Result<std::vector<core::TtsChunk>>::fail(r.error());

    // 3. Normalize, XML-escape, and split text into service-safe chunks.
    serialization::TextChunker chunker;
    auto chunk_result = chunker.chunk(text_);
    if (!chunk_result)
        return common::Result<std::vector<core::TtsChunk>>::fail(chunk_result.error());

    const std::vector<std::string>& text_chunks = *chunk_result;

    // 4. Empty input: no text chunks → no audio.
    if (text_chunks.empty())
        return common::Result<std::vector<core::TtsChunk>>::ok({});

    // 5. Run the synthesizer (real SynthesisSession or injected test double).
    return synthesizer_(config_, std::span<const std::string>{text_chunks});
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

common::Result<std::vector<core::TtsChunk>> SpeechSynthesizer::synthesize()
{
    if (called_)
        return common::Result<std::vector<core::TtsChunk>>::fail(
            common::Error{common::ErrorCode::invalid_state,
                          "synthesize can only be called once"});
    called_ = true;
    return run_pipeline();
}

common::Result<void> SpeechSynthesizer::save(
    const std::filesystem::path& media_path,
    std::optional<std::filesystem::path> subtitles_path)
{
    if (called_)
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::invalid_state,
                          "synthesize can only be called once"});
    called_ = true;

    auto synthesis = run_pipeline();
    if (!synthesis)
        return common::Result<void>::fail(synthesis.error());

    const auto& chunks = *synthesis;

    // Collect audio bytes and feed boundary events to SubMaker.
    std::vector<std::byte> audio_bytes;
    subtitles::SubMaker submaker;

    for (const auto& chunk : chunks) {
        if (core::is_audio(chunk)) {
            const auto& ac = std::get<core::AudioChunk>(chunk);
            audio_bytes.insert(audio_bytes.end(),
                               ac.data.begin(), ac.data.end());
        } else if (core::is_boundary(chunk)) {
            const auto& bc = std::get<core::BoundaryChunk>(chunk);
            if (auto r = submaker.feed(bc); !r)
                return common::Result<void>::fail(r.error());
        }
    }

    // Write media file first, then subtitle file (if requested).
    //
    // NOTE: writes are sequential, not atomic.  If the media write succeeds
    // but the subtitle write fails, the media file remains on disk and the
    // error from the subtitle write is returned.  Callers that need all-or-
    // nothing semantics must perform cleanup themselves on failure.
    FileWriter writer;
    if (auto r = writer.write_binary(media_path, audio_bytes); !r)
        return r;

    if (subtitles_path.has_value()) {
        auto srt = submaker.to_srt();
        if (!srt)
            return common::Result<void>::fail(srt.error());

        if (auto r = writer.write_text_utf8(*subtitles_path, *srt); !r)
            return r;
    }

    return common::Result<void>::ok();
}

} // namespace edge_tts::api
