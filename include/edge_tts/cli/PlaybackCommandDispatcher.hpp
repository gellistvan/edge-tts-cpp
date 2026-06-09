#pragma once

#include "edge_tts/api/SpeechSynthesizer.hpp"
#include "edge_tts/api/SynthesisOptions.hpp"
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
//
// for playback.  This avoids spawning a child edge-tts process while
// cleanup).
//
// All dependencies are injected so the dispatcher is fully testable without
// real synthesis, a real audio player, or real temp-file creation.
class PlaybackCommandDispatcher {
public:
    // Creates an api::SpeechSynthesizer object for the given text, config, and options.
    // options carries transport settings (proxy, timeouts) from CLI flags and env
    // vars.  Inject a fake synthesizer in tests.
    using SynthesizerFactory = std::function<
        api::SpeechSynthesizer(std::string text,
                         core::TtsConfig config,
                         api::SynthesisOptions options)>;

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
        SynthesizerFactory      synthesizer_factory,
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
    SynthesizerFactory      synthesizer_factory_;
    media::IAudioConverter& converter_;
    TempFileProvider        temp_provider_;
    bool                    keep_temp_;
    std::ostream&           out_;
    std::ostream&           err_;
    std::istream&           in_;

    int dispatch_play(const PlaybackArguments& args);
};

} // namespace edge_tts::cli
