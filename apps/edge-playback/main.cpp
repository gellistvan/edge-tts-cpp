#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/cli/PlaybackArguments.hpp"
#include "edge_tts/cli/PlaybackCommandDispatcher.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/media/FfmpegAudioConverter.hpp"
#include "edge_tts/media/ProcessRunner.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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
    // Honours EDGE_PLAYBACK_MP3_FILE env var override.
    auto temp_provider = [debug](std::string_view suffix) -> std::filesystem::path {
        // Check env override first.
        if (suffix == ".mp3") {
            const char* env_mp3 = std::getenv("EDGE_PLAYBACK_MP3_FILE");
            if (env_mp3 && *env_mp3)
                return std::filesystem::path{env_mp3};
        }

        // Generate a unique name in the OS temp directory.
        const auto tick = std::chrono::system_clock::now().time_since_epoch().count();
        auto path = std::filesystem::temp_directory_path() /
                    ("edge_playback_" + std::to_string(tick) + std::string(suffix));

        if (debug)
            std::cout << "Media file: " << path.string() << '\n';

        return path;
    };

    // Wire production dependencies — same pattern as edge-tts/main.cpp.
    cli::PlaybackCommandDispatcher dispatcher{
        [](std::string text, core::TtsConfig cfg) {
            return api::Communicate{std::move(text), std::move(cfg)};
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
