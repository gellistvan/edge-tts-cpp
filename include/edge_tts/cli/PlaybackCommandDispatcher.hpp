#pragma once

#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"
#include "edge_tts/cli/PlaybackArguments.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/media/AudioConverter.hpp"

#include <filesystem>
#include <functional>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace edge_tts::cli {

// Executes a parsed edge-playback command.
//
// Reference: edge_playback/__main__.py _main():
//   1. Verify deps on PATH (_check_deps).
//   2. Create temp MP3 (and SRT) file(s).
//   3. Synthesize to temp MP3 via _run_edge_tts().
//   4. Play via _play_media().
//   5. Clean up temp files (_cleanup).
//
// In this C++ implementation the subprocess calls are replaced with direct
// library calls: Communicate::save() for synthesis, IAudioConverter::play_mp3()
// for playback.  This avoids spawning a child edge-tts process while
// preserving the same observable lifecycle (temp file → synthesize → play →
// cleanup).
//
// All dependencies are injected so the dispatcher is fully testable without
// real synthesis, a real audio player, or real temp-file creation.
class PlaybackCommandDispatcher {
public:
    // Creates an api::Communicate object for the given text, config, and options.
    // options carries transport settings (proxy, timeouts) from CLI flags and env
    // vars.  Inject a fake synthesizer in tests.
    using CommunicateFactory = std::function<
        api::Communicate(std::string text,
                         core::TtsConfig config,
                         api::CommunicateOptions options)>;

    // Provides a temporary file path with the given suffix.
    // Returns nullopt when the caller should skip generating that file type
    // (e.g. ".srt" when subtitle output is not requested).
    // Production default: creates a uniquely-named path in the OS temp dir for
    // ".mp3" and returns nullopt for ".srt" unless EDGE_PLAYBACK_SRT_FILE is set.
    // Inject a known path (or nullopt) in tests to control behavior.
    using TempFileProvider =
        std::function<std::optional<std::filesystem::path>(std::string_view suffix)>;

    // converter    — audio player (FfmpegAudioConverter in production).
    // keep_temp    — when true, temp files are NOT deleted after playback.
    //                Set from EDGE_PLAYBACK_KEEP_TEMP env var in main.cpp.
    PlaybackCommandDispatcher(
        CommunicateFactory      communicate_factory,
        media::IAudioConverter& converter,
        TempFileProvider        temp_provider,
        bool                    keep_temp,
        std::ostream&           out,
        std::ostream&           err,
        std::istream&           in);

    // Dispatch the parsed result.
    //
    // Return values:
    //   0  — success
    //   1  — runtime error (synthesis, playback, or file I/O failure)
    //   2  — argument error (invalid parse result or missing --text/--file)
    int dispatch(const PlaybackParseResult& result);

private:
    CommunicateFactory      communicate_factory_;
    media::IAudioConverter& converter_;
    TempFileProvider        temp_provider_;
    bool                    keep_temp_;
    std::ostream&           out_;
    std::ostream&           err_;
    std::istream&           in_;

    int dispatch_play(const PlaybackArguments& args);
};

} // namespace edge_tts::cli
