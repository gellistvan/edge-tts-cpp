#include "media/FfmpegAudioConverter.hpp"

#include "common/Error.hpp"

#include <string>

namespace edge_tts::media {

FfmpegAudioConverter::FfmpegAudioConverter(IProcessRunner& runner, std::string path_env)
    : runner_{runner}
    , path_env_{std::move(path_env)}
{}

common::Result<void>
FfmpegAudioConverter::play_mp3(const std::filesystem::path& input) {
    auto ffplay = discovery_.find_on_path("ffplay", path_env_);
    if (!ffplay)
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::external_process_failed,
                          "ffplay not found on PATH"});

    // Reference: _play_media() in __main__.py uses mpv with -nodisp/-autoexit
    // equivalent.  ffplay uses the same semantics: suppress the video window
    // and exit automatically when playback finishes.
    ProcessCommand cmd{*ffplay, {"-nodisp", "-autoexit", input.string()}};
    auto result = runner_.run(cmd);
    if (!result)
        return common::Result<void>::fail(result.error());

    if (result->exit_code != 0)
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::external_process_failed,
                          "ffplay exited with code " + std::to_string(result->exit_code),
                          result->stderr_text});

    return common::Result<void>::ok();
}

common::Result<void>
FfmpegAudioConverter::convert(const std::filesystem::path& input,
                               const std::filesystem::path& output) {
    auto ffmpeg = discovery_.find_on_path("ffmpeg", path_env_);
    if (!ffmpeg)
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::external_process_failed,
                          "ffmpeg not found on PATH"});

    // -y  : overwrite output without asking
    // -i  : input file
    // Output format is inferred from the output file extension by ffmpeg.
    ProcessCommand cmd{*ffmpeg, {"-y", "-i", input.string(), output.string()}};
    auto result = runner_.run(cmd);
    if (!result)
        return common::Result<void>::fail(result.error());

    if (result->exit_code != 0)
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::external_process_failed,
                          "ffmpeg exited with code " + std::to_string(result->exit_code),
                          result->stderr_text});

    return common::Result<void>::ok();
}

} // namespace edge_tts::media
