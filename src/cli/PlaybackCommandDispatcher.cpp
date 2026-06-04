#include "edge_tts/cli/PlaybackCommandDispatcher.hpp"

#include "edge_tts/cli/InputLoader.hpp"
#include "edge_tts/core/TtsConfig.hpp"

#include <filesystem>

namespace edge_tts::cli {

namespace fs = std::filesystem;

PlaybackCommandDispatcher::PlaybackCommandDispatcher(
    CommunicateFactory      communicate_factory,
    media::IAudioConverter& converter,
    TempFileProvider        temp_provider,
    bool                    keep_temp,
    std::ostream&           out,
    std::ostream&           err,
    std::istream&           in)
    : communicate_factory_(std::move(communicate_factory))
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
    // 1. Load text.
    //    Reference: edge-playback forwards text to edge-tts which reads it.
    //    Here we load it directly for the library call.
    InputLoader loader;
    auto text = loader.load_playback(args, in_);
    if (!text) {
        err_ << "error: " << text.error().what() << '\n';
        return 1;
    }

    // 2. Build TtsConfig.
    core::TtsConfig config;
    config.voice  = args.voice;
    config.rate   = args.rate;
    config.volume = args.volume;
    config.pitch  = args.pitch;

    // 3. Obtain a temp file path for the synthesized MP3.
    //    Reference: _create_temp_files() — NamedTemporaryFile(suffix=".mp3", delete=False)
    const fs::path tmp_mp3 = temp_provider_(".mp3");

    // RAII cleanup: delete the temp file on exit unless keep_temp_ is set.
    // This mirrors _cleanup() in __main__.py which always runs in try/finally.
    struct TempGuard {
        const fs::path& path;
        bool&           keep;
        ~TempGuard() {
            if (!keep) {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        }
    } guard{tmp_mp3, keep_temp_};

    // 4. Synthesize to the temp MP3 file.
    //    Reference: _run_edge_tts(mp3_fname, ...) writes MP3 bytes to the file.
    api::Communicate communicate = communicate_factory_(*text, config);
    if (auto r = communicate.save(tmp_mp3); !r) {
        err_ << "error: " << r.error().what() << '\n';
        return 1;
    }

    // 5. Play the temp MP3 file.
    //    Reference: _play_media(use_mpv, mp3_fname, srt_fname)
    if (auto r = converter_.play_mp3(tmp_mp3); !r) {
        err_ << "error: " << r.error().what() << '\n';
        return 1;
    }

    return 0;
}

} // namespace edge_tts::cli
