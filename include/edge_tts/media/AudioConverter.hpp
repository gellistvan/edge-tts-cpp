#pragma once

#include <filesystem>

namespace edge_tts::media {

class AudioConverter {
public:
    virtual ~AudioConverter() = default;
    virtual void convert_mp3_to_wav(const std::filesystem::path& input,
                                    const std::filesystem::path& output) = 0;
    virtual void play_mp3(const std::filesystem::path& input) = 0;
};

class FfmpegProcessConverter final : public AudioConverter {
public:
    void convert_mp3_to_wav(const std::filesystem::path& input,
                            const std::filesystem::path& output) override;
    void play_mp3(const std::filesystem::path& input) override;
};

} // namespace edge_tts::media
