#include "edge_tts/cli/EdgeTtsCommandDispatcher.hpp"
#include "edge_tts/api/FileWriter.hpp"
#include "edge_tts/cli/InputLoader.hpp"
#include "edge_tts/cli/VoiceFormatter.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/subtitles/SubMaker.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <variant>

namespace edge_tts::cli {

// Strip user:password credentials from a URL before logging.
// "http://user:pass@host:8080/path" → "http://[credentials]@host:8080/path"
// URLs with no credentials are returned unchanged.
static std::string redact_url_credentials(std::string_view url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos)
        return std::string(url);

    const auto authority_start = scheme_end + 3;
    const auto at_pos = url.find('@', authority_start);
    if (at_pos == std::string_view::npos)
        return std::string(url);

    std::string out;
    out.reserve(url.size());
    out.append(url.data(), authority_start);
    out.append("[credentials]");
    out.append(url.data() + at_pos, url.size() - at_pos);
    return out;
}

// Format an error for display: "[code] message (context: redacted_context)".
// URL credentials in the context field are redacted before printing.
static std::string format_error(const common::Error& e) {
    const auto raw = e.what();
    // If context looks like a URL (contains "://") redact any embedded credentials.
    if (e.has_context() && e.context().find("://") != std::string_view::npos) {
        const std::string safe_context = redact_url_credentials(e.context());
        // Rebuild what() with the redacted context.
        const std::string prefix = "[" + std::string(to_string(e.code())) + "] "
                                   + std::string(e.message())
                                   + " (context: ";
        return prefix + safe_context + ")";
    }
    return raw;
}

EdgeTtsCommandDispatcher::EdgeTtsCommandDispatcher(
    VoiceServiceFn     voice_service,
    CommunicateFactory communicate_factory,
    std::ostream&      out,
    std::ostream&      err,
    std::istream&      in,
    TtyCheckFn         tty_check)
    : voice_service_(std::move(voice_service))
    , communicate_factory_(std::move(communicate_factory))
    , out_(out)
    , err_(err)
    , in_(in)
    , tty_check_(std::move(tty_check))
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
        err_ << "error: " << format_error(voices.error()) << '\n';
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
        err_ << "error: " << format_error(text.error()) << '\n';
        return 1;
    }

    // 2. Build TtsConfig from CLI arguments.
    //    Validation happens inside Communicate; we do not duplicate it here.
    core::TtsConfig config;
    config.voice  = args.voice;
    config.rate   = args.rate;
    config.volume = args.volume;
    config.pitch  = args.pitch;

    // 3. Build transport options from CLI arguments.
    //    proxy maps from --proxy; timeouts use CommunicateOptions defaults.
    api::CommunicateOptions opts;
    opts.proxy = args.proxy;

    // 4. Create Communicate via the injected factory.
    api::Communicate communicate = communicate_factory_(*text, config, opts);

    // 5. Determine routing per reference util.py _run_tts():
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

    // 5a. Interactive TTY warning.
    //
    // Reference: util.py _run_tts() — if stdin.isatty() and stdout.isatty()
    //   and not write_media: warn on stderr and wait for Enter.
    //
    // The check fires only when write_media is absent (not when it is "-"),
    // matching the Python condition `not args.write_media` which is True only
    // for None, not for the string "-".
    //
    // If the user provides EOF on stdin (Ctrl-C on a real terminal or an empty
    // injected stream in tests), we print "Operation canceled." and return 0,
    // matching the Python KeyboardInterrupt handler.
    if (!args.write_media.has_value() && tty_check_ && tty_check_()) {
        err_ << "Warning: TTS output will be written to the terminal. "
                "Use --write-media to write to a file.\n"
                "Press Ctrl+C to cancel the operation. "
                "Press Enter to continue.\n";
        std::string dummy;
        if (!std::getline(in_, dummy)) {
            err_ << "\nOperation canceled.\n";
            return 0;
        }
    }

    // 6. Stream all chunks.
    auto chunks = communicate.stream_sync();
    if (!chunks) {
        err_ << "error: " << format_error(chunks.error()) << '\n';
        return 1;
    }

    // 7. Route audio and collect boundaries for SubMaker.
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
            auto feed_r = submaker.feed(std::get<core::BoundaryChunk>(chunk));
            if (!feed_r) {
                err_ << "error: " << format_error(feed_r.error()) << '\n';
                return 1;
            }
        }
    }

    // 8. Write audio file if requested.
    if (media_to_file) {
        api::FileWriter fw;
        auto r = fw.write_binary(*args.write_media, audio_bytes_for_file);
        if (!r) {
            err_ << "error: " << format_error(r.error()) << '\n';
            return 1;
        }
    }

    // 9. Route SRT if subtitles were requested.
    //    Reference: if sub_file is not None: sub_file.write(submaker.get_srt())
    if (srt_to_file || srt_to_stderr) {
        auto srt = submaker.to_srt();
        if (!srt) {
            err_ << "error: " << format_error(srt.error()) << '\n';
            return 1;
        }

        if (srt_to_file) {
            api::FileWriter fw;
            auto r = fw.write_text_utf8(*args.write_subtitles, *srt);
            if (!r) {
                err_ << "error: " << format_error(r.error()) << '\n';
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
