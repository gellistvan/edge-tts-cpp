#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/FileWriter.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/serialization/TextChunker.hpp"
#include "edge_tts/subtitles/SubMaker.hpp"

#include <utility>
#include <variant>

namespace edge_tts::api {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

// Shared stub for the production constructors.
static SynthesizerFn make_production_stub() {
    return [](const core::TtsConfig&, std::span<const std::string>)
               -> common::Result<std::vector<core::TtsChunk>>
    {
        return common::Result<std::vector<core::TtsChunk>>::fail(
            common::Error{common::ErrorCode::network_error,
                          "WebSocket transport not yet implemented"});
    };
}

Communicate::Communicate(std::string text, core::TtsConfig config)
    : text_(std::move(text))
    , config_(std::move(config))
    , options_{}
    , synthesizer_(make_production_stub())
{}

Communicate::Communicate(std::string text, core::TtsConfig config,
                         CommunicateOptions options)
    : text_(std::move(text))
    , config_(std::move(config))
    , options_(std::move(options))
    , synthesizer_(make_production_stub())
{}

Communicate::Communicate(std::string text, core::TtsConfig config,
                         SynthesizerFn synthesizer)
    : text_(std::move(text))
    , config_(std::move(config))
    , options_{}
    , synthesizer_(std::move(synthesizer))
{}

Communicate::Communicate(std::string text, core::TtsConfig config,
                         CommunicateOptions options, SynthesizerFn synthesizer)
    : text_(std::move(text))
    , config_(std::move(config))
    , options_(std::move(options))
    , synthesizer_(std::move(synthesizer))
{}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const std::string&        Communicate::text()    const noexcept { return text_; }
const core::TtsConfig&    Communicate::config()  const noexcept { return config_; }
const CommunicateOptions& Communicate::options() const noexcept { return options_; }

// ---------------------------------------------------------------------------
// Internal synthesis pipeline
// ---------------------------------------------------------------------------

common::Result<std::vector<core::TtsChunk>> Communicate::run_synthesis()
{
    // 1. Validate TTS configuration.
    //    Reference: communicate.py TTSConfig.__init__ validates on construction.
    if (auto r = core::validate_tts_config(config_); !r)
        return common::Result<std::vector<core::TtsChunk>>::fail(r.error());

    // 2. Normalize, XML-escape, and split text into service-safe chunks.
    //    Reference: escape(remove_incompatible_characters(text)) → split_text_by_byte_length(…, 4096)
    serialization::TextChunker chunker;
    auto chunk_result = chunker.chunk(text_);
    if (!chunk_result)
        return common::Result<std::vector<core::TtsChunk>>::fail(chunk_result.error());

    const std::vector<std::string>& text_chunks = *chunk_result;

    // 3. Empty input: no text chunks → no audio (Python yields nothing from an empty texts generator).
    if (text_chunks.empty())
        return common::Result<std::vector<core::TtsChunk>>::ok({});

    // 4. Run the injected synthesizer (real SynthesisSession or test double).
    return synthesizer_(config_, std::span<const std::string>{text_chunks});
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

common::Result<std::vector<core::TtsChunk>> Communicate::stream_sync()
{
    // Reference: Communicate.stream() raises RuntimeError if called a second time.
    if (stream_called_)
        return common::Result<std::vector<core::TtsChunk>>::fail(
            common::Error{common::ErrorCode::invalid_state,
                          "stream can only be called once"});
    stream_called_ = true;
    return run_synthesis();
}

common::Result<void> Communicate::save(
    const std::filesystem::path& media_path,
    std::optional<std::filesystem::path> subtitles_path)
{
    // Reference: Communicate.save() calls self.stream() internally, so both
    // stream() and save() consume the single-use stream.
    if (stream_called_)
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::invalid_state,
                          "stream can only be called once"});
    stream_called_ = true;

    // 1. Run synthesis pipeline.
    auto synthesis = run_synthesis();
    if (!synthesis)
        return common::Result<void>::fail(synthesis.error());

    const auto& chunks = *synthesis;

    // 2. Collect audio bytes and feed boundary events to SubMaker.
    //    Reference: communicate.py save():
    //      audio   → audio.write(chunk["data"])
    //      boundary → submaker.feed(chunk) then submaker.get_srt()
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

    // 3. Write media file (binary, reference: open(audio_fname, "wb")).
    FileWriter writer;
    if (auto r = writer.write_binary(media_path, audio_bytes); !r)
        return r;

    // 4. Write subtitle file if requested (UTF-8 text, reference: open(metadata_fname, "w", encoding="utf-8")).
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
