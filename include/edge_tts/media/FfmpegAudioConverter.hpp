#pragma once

#include "edge_tts/media/AudioConverter.hpp"
#include "edge_tts/media/ExecutableDiscovery.hpp"
#include "edge_tts/media/ProcessRunner.hpp"

#include <string>

namespace edge_tts::media {

// Concrete IAudioConverter that delegates to external ffmpeg and ffplay
// processes.  No FFmpeg library is linked; all calls go through IProcessRunner.
//
// Reference: edge_playback/__main__.py uses shutil.which() to locate tools and
// subprocess.Popen(list_of_args) to launch them — exactly the pattern used here
// via ExecutableDiscovery + IProcessRunner.
//
// Tool names:
//   convert() — "ffmpeg"  with flags: -y -i <input> <output>
//   play_mp3() — "ffplay" with flags: -nodisp -autoexit <input>
class FfmpegAudioConverter final : public IAudioConverter {
public:
    // runner    — injectable process runner (use FakeProcessRunner in tests).
    // path_env  — colon-separated (POSIX) / semicolon-separated (Windows) PATH
    //             string used by ExecutableDiscovery to locate ffmpeg/ffplay.
    //             Pass std::getenv("PATH") (guarded for nullptr) for production.
    FfmpegAudioConverter(IProcessRunner& runner, std::string path_env);

    // IAudioConverter
    [[nodiscard]] common::Result<void>
    play_mp3(const std::filesystem::path& input) override;

    [[nodiscard]] common::Result<void>
    convert(const std::filesystem::path& input,
            const std::filesystem::path& output) override;

private:
    IProcessRunner&    runner_;
    std::string        path_env_;
    ExecutableDiscovery discovery_;
};

} // namespace edge_tts::media
