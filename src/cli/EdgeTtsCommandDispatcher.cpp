#include "edge_tts/cli/EdgeTtsCommandDispatcher.hpp"
#include "edge_tts/api/FileWriter.hpp"
#include "edge_tts/cli/InputLoader.hpp"
#include "edge_tts/cli/VoiceFormatter.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/subtitles/SubMaker.hpp"

#include <filesystem>
#include <fstream>
#include <variant>

namespace edge_tts::cli {

EdgeTtsCommandDispatcher::EdgeTtsCommandDispatcher(
    VoiceServiceFn     voice_service,
    CommunicateFactory communicate_factory,
    std::ostream&      out,
    std::ostream&      err,
    std::istream&      in)
    : voice_service_(std::move(voice_service))
    , communicate_factory_(std::move(communicate_factory))
    , out_(out)
    , err_(err)
    , in_(in)
{}

// ---------------------------------------------------------------------------
// Public dispatch entry point
// ---------------------------------------------------------------------------

int EdgeTtsCommandDispatcher::dispatch(const ParseResult& result) {
    switch (result.action) {
    case ParseAction::help:
        // Reference: --help prints and exits 0.
        out_ << result.message;
        return 0;

    case ParseAction::version:
        // Reference: --version prints "edge-tts {ver}" and exits 0.
        out_ << result.message << '\n';
        return 0;

    case ParseAction::error:
        // Reference: argparse invalid-arg exits with code 2.
        err_ << "edge-tts: " << result.message << '\n';
        return 2;

    case ParseAction::list_voices:
        return dispatch_list_voices();

    case ParseAction::synthesize:
        return dispatch_synthesize(result.arguments);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// list-voices
// ---------------------------------------------------------------------------

int EdgeTtsCommandDispatcher::dispatch_list_voices() {
    // Reference: _print_voices() — fetch, sort, format, print to stdout.
    auto voices = voice_service_();
    if (!voices) {
        err_ << "error: " << voices.error().what() << '\n';
        return 1;
    }

    VoiceFormatter fmt;
    out_ << fmt.format(*voices);
    return 0;
}

// ---------------------------------------------------------------------------
// synthesize
// ---------------------------------------------------------------------------

int EdgeTtsCommandDispatcher::dispatch_synthesize(const EdgeTtsArguments& args) {
    // 1. Load input text.
    InputLoader loader;
    auto text = loader.load(args, in_);
    if (!text) {
        err_ << "error: " << text.error().what() << '\n';
        return 1;
    }

    // 2. Build TtsConfig from CLI arguments.
    //    Validation happens inside Communicate; we do not duplicate it here.
    core::TtsConfig config;
    config.voice  = args.voice;
    config.rate   = args.rate;
    config.volume = args.volume;
    config.pitch  = args.pitch;

    // 3. Create Communicate via the injected factory.
    api::Communicate communicate = communicate_factory_(*text, config);

    // 4. Determine routing per reference util.py _run_tts():
    //      write_media  absent | "-" → audio → out_ (stdout)
    //      write_media  non-dash     → audio → file
    //      write_subtitles absent    → no SRT
    //      write_subtitles "-"       → SRT → err_ (stderr)
    //      write_subtitles non-dash  → SRT → file
    const bool media_to_file =
        args.write_media.has_value() && *args.write_media != "-";
    const bool srt_to_file =
        args.write_subtitles.has_value() && *args.write_subtitles != "-";
    const bool srt_to_stderr =
        args.write_subtitles.has_value() && *args.write_subtitles == "-";

    // 5. Stream all chunks.
    auto chunks = communicate.stream_sync();
    if (!chunks) {
        err_ << "error: " << chunks.error().what() << '\n';
        return 1;
    }

    // 6. Route audio and collect boundaries for SubMaker.
    subtitles::SubMaker submaker;
    std::vector<std::byte> audio_bytes_for_file;

    for (const auto& chunk : *chunks) {
        if (core::is_audio(chunk)) {
            const auto& ac = std::get<core::AudioChunk>(chunk);
            if (media_to_file) {
                audio_bytes_for_file.insert(
                    audio_bytes_for_file.end(), ac.data.begin(), ac.data.end());
            } else {
                // Reference: audio_file.write(chunk["data"]) — write raw bytes.
                out_.write(reinterpret_cast<const char*>(ac.data.data()),
                           static_cast<std::streamsize>(ac.data.size()));
            }
        } else if (core::is_boundary(chunk)) {
            (void)submaker.feed(std::get<core::BoundaryChunk>(chunk));
        }
    }

    // 7. Write audio file if requested.
    if (media_to_file) {
        api::FileWriter fw;
        auto r = fw.write_binary(*args.write_media, audio_bytes_for_file);
        if (!r) {
            err_ << "error: " << r.error().what() << '\n';
            return 1;
        }
    }

    // 8. Route SRT if subtitles were requested.
    //    Reference: if sub_file is not None: sub_file.write(submaker.get_srt())
    if (srt_to_file || srt_to_stderr) {
        auto srt = submaker.to_srt();
        if (!srt) {
            err_ << "error: " << srt.error().what() << '\n';
            return 1;
        }

        if (srt_to_file) {
            api::FileWriter fw;
            auto r = fw.write_text_utf8(*args.write_subtitles, *srt);
            if (!r) {
                err_ << "error: " << r.error().what() << '\n';
                return 1;
            }
        } else {
            // srt_to_stderr: reference sends to sys.stderr.
            err_ << *srt;
        }
    }

    out_.flush();
    return 0;
}

} // namespace edge_tts::cli
