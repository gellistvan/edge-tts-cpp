#include "cli/PlaybackCommandDispatcher.hpp"

#include "cli/InputLoader.hpp"
#include "common/Error.hpp"
#include "core/TtsConfig.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace edge_tts::cli {

namespace fs = std::filesystem;

// Replace user:password credentials in a URL with [credentials].
// "http://user:pass@host:8080" → "http://[credentials]@host:8080"
// URLs with no credentials are returned unchanged.
static std::string redact_url_credentials(std::string_view url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) return std::string(url);
    const auto auth_start = scheme_end + 3;
    const auto at_pos     = url.find('@', auth_start);
    if (at_pos == std::string_view::npos) return std::string(url);
    std::string out;
    out.reserve(url.size());
    out.append(url.data(), auth_start);
    out.append("[credentials]");
    out.append(url.data() + at_pos, url.size() - at_pos);
    return out;
}

// Format an error for display, redacting any URL credentials in the context.
static std::string format_error(const common::Error& e) {
    if (e.has_context() && e.context().find("://") != std::string_view::npos) {
        const std::string safe = redact_url_credentials(e.context());
        return "[" + std::string(common::to_string(e.code())) + "] "
               + std::string(e.message()) + " (context: " + safe + ")";
    }
    return std::string(e.what());
}

PlaybackCommandDispatcher::PlaybackCommandDispatcher(
    SynthesizerFactory      synthesizer_factory,
    media::IAudioConverter& converter,
    TempFileProvider        temp_provider,
    bool                    keep_temp,
    std::ostream&           out,
    std::ostream&           err,
    std::istream&           in)
    : synthesizer_factory_(std::move(synthesizer_factory))
    , converter_(converter)
    , temp_provider_(std::move(temp_provider))
    , keep_temp_(keep_temp)
    , out_(out)
    , err_(err)
    , in_(in)
{}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

int PlaybackCommandDispatcher::dispatch(const PlaybackParseResult& result) {
    switch (result.action) {
    case PlaybackParseAction::help:
        out_ << result.message;
        return 0;

    case PlaybackParseAction::error:
        err_ << "edge-playback: " << result.message << '\n';
        return 2;

    case PlaybackParseAction::play:
        return dispatch_play(result.arguments);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Synthesis + playback
// ---------------------------------------------------------------------------

int PlaybackCommandDispatcher::dispatch_play(const PlaybackArguments& args) {
    // 1. Reject --mpv: only ffplay is supported in this build.
    //    Reference: __main__.py uses mpv on non-Windows; the C++ build always
    //    uses ffplay.  Silently ignoring --mpv would leave the user without the
    //    subtitle display or mpv-specific behaviour they requested.
    if (args.use_mpv) {
        err_ << "error: --mpv is not supported in this build; "
                "only ffplay is available. Remove --mpv to use ffplay.\n";
        return 1;
    }

    // 2. Load text.
    //    Reference: edge-playback forwards text to edge-tts which reads it.
    //    Here we load it directly for the library call.
    InputLoader loader;
    auto text = loader.load_playback(args, in_);
    if (!text) {
        err_ << "error: " << format_error(text.error()) << '\n';
        return 1;
    }

    // 3. Build TtsConfig.
    core::TtsConfig config;
    config.voice  = args.voice;
    config.rate   = args.rate;
    config.volume = args.volume;
    config.pitch  = args.pitch;

    // 4. Build transport options.
    //    Proxy flows into SynthesisOptions::proxy.  The ixwebsocket backend
    //    returns ErrorCode::unsupported if proxy is set — same as edge-tts.
    api::SynthesisOptions options;
    options.proxy = args.proxy;

    // 5. Obtain temp file paths for the synthesized media and optional subtitle.
    //    Reference: _create_temp_files() — NamedTemporaryFile(suffix=".mp3")
    //    For ".srt", the provider returns nullopt unless EDGE_PLAYBACK_SRT_FILE
    //    is set (production) or the test explicitly supplies a path.
    auto mp3_opt = temp_provider_(".mp3");
    if (!mp3_opt) {
        err_ << "error: temp file provider returned no path for .mp3\n";
        return 1;
    }
    const fs::path tmp_mp3 = *mp3_opt;

    auto srt_opt = temp_provider_(".srt");  // nullopt → no subtitle output

    // RAII cleanup: delete temp files on exit unless keep_temp_ is set.
    // This mirrors _cleanup() in __main__.py which always runs in try/finally.
    struct TempGuard {
        const fs::path&                     mp3;
        const std::optional<fs::path>&      srt;
        bool&                               keep;
        ~TempGuard() {
            if (!keep) {
                std::error_code ec;
                fs::remove(mp3, ec);
                if (srt)
                    fs::remove(*srt, ec);
            }
        }
    } guard{tmp_mp3, srt_opt, keep_temp_};

    // 6. Synthesize to the temp MP3 (and optionally SRT) file.
    api::SpeechSynthesizer synthesizer = synthesizer_factory_(*text, config, options);
    if (auto r = synthesizer.save(tmp_mp3, srt_opt); !r) {
        err_ << "error: " << format_error(r.error()) << '\n';
        return 1;
    }

    // 7. Play the temp MP3 file.
    if (auto r = converter_.play_mp3(tmp_mp3); !r) {
        err_ << "error: " << format_error(r.error()) << '\n';
        return 1;
    }

    return 0;
}

} // namespace edge_tts::cli
