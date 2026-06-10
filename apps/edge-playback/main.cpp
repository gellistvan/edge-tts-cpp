#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "cli/PlaybackArguments.hpp"
#include "cli/PlaybackCommandDispatcher.hpp"
#include "core/TtsConfig.hpp"
#include "media/FfmpegAudioConverter.hpp"
#include "media/ProcessRunner.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

int main(int argc, char* argv[]) {
    using namespace edge_tts;

    // Parse arguments.
    cli::PlaybackArgumentParser parser;
    auto result = parser.parse(argc, argv);

    // Read environment variables.
    // Reference: edge_playback/__main__.py — EDGE_PLAYBACK_KEEP_TEMP, EDGE_PLAYBACK_DEBUG
    const bool keep_temp =
        (std::getenv("EDGE_PLAYBACK_KEEP_TEMP") != nullptr);
    const bool debug =
        (std::getenv("EDGE_PLAYBACK_DEBUG") != nullptr);

    // Production audio converter: uses real ProcessRunner + system PATH.
    const char* raw_path = std::getenv("PATH");
    std::string path_env = raw_path ? raw_path : "";
    media::ProcessRunner        runner;
    media::FfmpegAudioConverter converter{runner, path_env};

    // Temp file provider.
    // Reference: _create_temp_files() uses NamedTemporaryFile(suffix=".mp3", delete=False)
    //   For ".srt", creates a temp file only when use_mpv is true (Python always
    //   uses mpv on non-Windows).  In C++, we default to no SRT unless the user
    //   sets EDGE_PLAYBACK_SRT_FILE.
    // Honours EDGE_PLAYBACK_MP3_FILE and EDGE_PLAYBACK_SRT_FILE env var overrides.
    auto temp_provider =
        [debug](std::string_view suffix) -> std::optional<std::filesystem::path> {
        if (suffix == ".mp3") {
            const char* env_mp3 = std::getenv("EDGE_PLAYBACK_MP3_FILE");
            if (env_mp3 && *env_mp3)
                return std::filesystem::path{env_mp3};

            // Generate a unique name in the OS temp directory.
            const auto tick =
                std::chrono::system_clock::now().time_since_epoch().count();
            auto path = std::filesystem::temp_directory_path() /
                        ("edge_playback_" + std::to_string(tick) + ".mp3");

            if (debug)
                std::cout << "Media file: " << path.string() << '\n';

            return path;
        }

        if (suffix == ".srt") {
            const char* env_srt = std::getenv("EDGE_PLAYBACK_SRT_FILE");
            if (env_srt && *env_srt) {
                if (debug)
                    std::cout << "Subtitle file: "
                              << std::string(env_srt) << '\n';
                return std::filesystem::path{env_srt};
            }
            return std::nullopt;  // no subtitle output by default
        }

        return std::nullopt;
    };

    // Wire production dependencies — same pattern as edge-tts/main.cpp.
    // The factory passes SynthesisOptions (timeouts) from CLI args.
    // Proxy is rejected early by SpeechSynthesizer before any transport call.
    cli::PlaybackCommandDispatcher dispatcher{
        [](std::string text, core::TtsConfig cfg, api::SynthesisOptions opts) {
            return api::SpeechSynthesizer{std::move(text), std::move(cfg), std::move(opts)};
        },
        converter,
        temp_provider,
        keep_temp,
        std::cout,
        std::cerr,
        std::cin
    };

    return dispatcher.dispatch(result);
}
