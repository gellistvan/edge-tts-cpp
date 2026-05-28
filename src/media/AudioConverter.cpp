#include "edge_tts/media/AudioConverter.hpp"
#include "edge_tts/common/Errors.hpp"

namespace edge_tts::media {

void FfmpegProcessConverter::convert_mp3_to_wav(const std::filesystem::path&, const std::filesystem::path&) {
    throw common::AudioError{"FFmpeg conversion is not implemented yet"};
}

void FfmpegProcessConverter::play_mp3(const std::filesystem::path&) {
    throw common::AudioError{"FFmpeg playback is not implemented yet"};
}

} // namespace edge_tts::media
