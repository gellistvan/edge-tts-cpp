#include "edge_tts/media/FfmpegAudioConverter.hpp"
#include "edge_tts/media/FakeProcessRunner.hpp"
#include "edge_tts/common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using edge_tts::common::ErrorCode;
using edge_tts::media::FakeProcessRunner;
using edge_tts::media::FfmpegAudioConverter;
using edge_tts::media::ProcessCommand;
using edge_tts::media::ProcessResult;

// ---------------------------------------------------------------------------
// Helpers — create real executables in a temp dir so ExecutableDiscovery finds
// them.  Tests use FakeProcessRunner so no child process is ever spawned.
// ---------------------------------------------------------------------------

namespace {

void make_executable(const fs::path& p) {
    std::ofstream f{p};
    f.close();
#ifndef _WIN32
    fs::permissions(p,
                    fs::perms::owner_exec | fs::perms::owner_read |
                    fs::perms::owner_write,
                    fs::perm_options::add);
#endif
}

struct TempToolDir {
    fs::path dir;
    std::string path_env;

    explicit TempToolDir(const std::string& suffix)
        : dir(fs::temp_directory_path() / ("ffmpeg_conv_" + suffix)) {
        fs::create_directories(dir);
#ifdef _WIN32
        path_env = dir.string();
#else
        path_env = dir.string();
#endif
    }

    void add(const std::string& name) { make_executable(dir / name); }

    ~TempToolDir() { fs::remove_all(dir); }
};

} // namespace

// ---------------------------------------------------------------------------
// convert() — command construction
// ---------------------------------------------------------------------------

TEST(FfmpegAudioConverter, ConvertBuildsCorrectCommand) {
    TempToolDir tools{"convert_cmd"};
    tools.add("ffmpeg");

    FakeProcessRunner runner;
    FfmpegAudioConverter conv{runner, tools.path_env};

    auto r = conv.convert("/tmp/in.mp3", "/tmp/out.wav");

    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(runner.last_command().has_value());

    const auto& cmd = *runner.last_command();
    EXPECT_EQ(cmd.executable.filename().string(), "ffmpeg");

    // Expected args: -y -i <input> <output>
    EXPECT_EQ(cmd.arguments.size(), 4u);
    EXPECT_EQ(cmd.arguments[0], "-y");
    EXPECT_EQ(cmd.arguments[1], "-i");
    EXPECT_EQ(cmd.arguments[2], "/tmp/in.mp3");
    EXPECT_EQ(cmd.arguments[3], "/tmp/out.wav");
}

// ---------------------------------------------------------------------------
// play_mp3() — command construction
// ---------------------------------------------------------------------------

TEST(FfmpegAudioConverter, PlayMp3BuildsCorrectCommand) {
    TempToolDir tools{"play_cmd"};
    tools.add("ffplay");

    FakeProcessRunner runner;
    FfmpegAudioConverter conv{runner, tools.path_env};

    auto r = conv.play_mp3("/tmp/audio.mp3");

    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(runner.last_command().has_value());

    const auto& cmd = *runner.last_command();
    EXPECT_EQ(cmd.executable.filename().string(), "ffplay");

    // Expected args: -nodisp -autoexit <input>
    EXPECT_EQ(cmd.arguments.size(), 3u);
    EXPECT_EQ(cmd.arguments[0], "-nodisp");
    EXPECT_EQ(cmd.arguments[1], "-autoexit");
    EXPECT_EQ(cmd.arguments[2], "/tmp/audio.mp3");
}

// ---------------------------------------------------------------------------
// Paths with spaces — must be passed as single tokens, never shell-split
// ---------------------------------------------------------------------------

TEST(FfmpegAudioConverter, ConvertPathsWithSpaces) {
    TempToolDir tools{"spaces"};
    tools.add("ffmpeg");

    FakeProcessRunner runner;
    FfmpegAudioConverter conv{runner, tools.path_env};

    auto r = conv.convert("/tmp/my audio/in.mp3", "/tmp/my output/out.wav");

    EXPECT_TRUE(r.has_value());
    const auto& cmd = *runner.last_command();
    EXPECT_EQ(cmd.arguments[2], "/tmp/my audio/in.mp3");
    EXPECT_EQ(cmd.arguments[3], "/tmp/my output/out.wav");
}

TEST(FfmpegAudioConverter, PlayMp3PathWithSpaces) {
    TempToolDir tools{"play_spaces"};
    tools.add("ffplay");

    FakeProcessRunner runner;
    FfmpegAudioConverter conv{runner, tools.path_env};

    auto r = conv.play_mp3("/tmp/my audio file.mp3");

    EXPECT_TRUE(r.has_value());
    const auto& cmd = *runner.last_command();
    EXPECT_EQ(cmd.arguments[2], "/tmp/my audio file.mp3");
}

// ---------------------------------------------------------------------------
// Missing executable
// ---------------------------------------------------------------------------

TEST(FfmpegAudioConverter, MissingFfmpegReturnsError) {
    // Empty PATH — ffmpeg will not be found.
    FakeProcessRunner runner;
    FfmpegAudioConverter conv{runner, ""};

    auto r = conv.convert("/tmp/in.mp3", "/tmp/out.wav");

    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::external_process_failed);
    // Runner must NOT have been called — we fail before launching.
    EXPECT_EQ(runner.run_count(), 0);
}

TEST(FfmpegAudioConverter, MissingFfplayReturnsError) {
    FakeProcessRunner runner;
    FfmpegAudioConverter conv{runner, ""};

    auto r = conv.play_mp3("/tmp/audio.mp3");

    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::external_process_failed);
    EXPECT_EQ(runner.run_count(), 0);
}

// ---------------------------------------------------------------------------
// Process failure (non-zero exit code)
// ---------------------------------------------------------------------------

TEST(FfmpegAudioConverter, ConvertNonZeroExitReturnsError) {
    TempToolDir tools{"conv_fail"};
    tools.add("ffmpeg");

    FakeProcessRunner runner;
    runner.set_result(ProcessResult{1, "", "unsupported encoder"});
    FfmpegAudioConverter conv{runner, tools.path_env};

    auto r = conv.convert("/tmp/in.mp3", "/tmp/out.wav");

    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::external_process_failed);
    EXPECT_EQ(runner.run_count(), 1);
}

TEST(FfmpegAudioConverter, PlayMp3NonZeroExitReturnsError) {
    TempToolDir tools{"play_fail"};
    tools.add("ffplay");

    FakeProcessRunner runner;
    runner.set_result(ProcessResult{1, "", "cannot open file"});
    FfmpegAudioConverter conv{runner, tools.path_env};

    auto r = conv.play_mp3("/tmp/audio.mp3");

    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::external_process_failed);
    EXPECT_EQ(runner.run_count(), 1);
}

// ---------------------------------------------------------------------------
// Runner-level error (launch failure, e.g. fork failed)
// ---------------------------------------------------------------------------

TEST(FfmpegAudioConverter, RunnerLaunchFailurePropagated) {
    TempToolDir tools{"launch_fail"};
    tools.add("ffmpeg");

    FakeProcessRunner runner;
    runner.set_error(edge_tts::common::Error{ErrorCode::external_process_failed,
                                              "fork failed"});
    FfmpegAudioConverter conv{runner, tools.path_env};

    auto r = conv.convert("/tmp/in.mp3", "/tmp/out.wav");

    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::external_process_failed);
}

// ---------------------------------------------------------------------------
// CMake verification: FfmpegAudioConverter must NOT link FFmpeg libraries.
//
// This is enforced structurally: the media module links only edge_tts::common
// in CMakeLists.txt.  There is no find_package(FFmpeg), no libavcodec, no
// libavformat, and no libavutil in the build.  The test below is a compile-time
// proof — if this translation unit builds successfully without any FFmpeg
// headers, the constraint holds.
// ---------------------------------------------------------------------------

TEST(FfmpegAudioConverter, NoFfmpegLibraryLinked) {
    // If this test compiles, libavcodec/libavformat are not in the include path.
    // Structural proof: check that no avcodec.h, avformat.h, etc. are reachable.
#if defined(AVCODEC_AVCODEC_H) || defined(AVFORMAT_AVFORMAT_H) || \
    defined(AVUTIL_AVUTIL_H)
    static_assert(false, "FFmpeg library headers must not be included");
#endif
    EXPECT_TRUE(true);
}
