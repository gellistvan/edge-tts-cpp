#pragma once

#include "edge_tts/common/Result.hpp"

#include <filesystem>

namespace edge_tts::media {

// Abstract audio converter/player interface.
//
// Reference: edge_playback/__main__.py uses mpv for playback on non-Windows and
// subprocess.Popen() for launching external tools — no FFmpeg library linking.
// This interface is the single abstraction point for all audio I/O so that the
// CLI and api layers never name a concrete tool directly.
class IAudioConverter {
public:
    virtual ~IAudioConverter() = default;

    // Play an MP3 file via an external player (e.g. ffplay).
    // Returns fail() if the player executable is not found on PATH or exits
    // with a non-zero status.
    [[nodiscard]] virtual common::Result<void>
    play_mp3(const std::filesystem::path& input) = 0;

    // Convert input to output using an external converter (e.g. ffmpeg).
    // The output format is inferred from the output file extension.
    // Returns fail() if ffmpeg is not found on PATH or exits with a non-zero
    // status.
    [[nodiscard]] virtual common::Result<void>
    convert(const std::filesystem::path& input,
            const std::filesystem::path& output) = 0;
};

} // namespace edge_tts::media
