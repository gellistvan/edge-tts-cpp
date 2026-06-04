#include "edge_tts/media/ExecutableDiscovery.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using edge_tts::media::ExecutableDiscovery;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Write an empty file at path and mark it executable (POSIX chmod).
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

// Write an empty file at path without execute permission.
void make_non_executable(const fs::path& p) {
    std::ofstream f{p};
    f.close();
#ifndef _WIN32
    fs::permissions(p,
                    fs::perms::owner_exec | fs::perms::group_exec |
                    fs::perms::others_exec,
                    fs::perm_options::remove);
#endif
}

// Build a PATH string from a list of directories.
std::string build_path(std::initializer_list<fs::path> dirs) {
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::string result;
    for (const auto& d : dirs) {
        if (!result.empty()) result += sep;
        result += d.string();
    }
    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(ExecutableDiscovery, FindsExecutableInSingleDir) {
    auto tmp = fs::temp_directory_path() / "exec_disc_single";
    fs::create_directories(tmp);
    make_executable(tmp / "mpv");

    ExecutableDiscovery disc;
    auto result = disc.find_on_path("mpv", build_path({tmp}));

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->filename().string(), "mpv");

    fs::remove_all(tmp);
}

TEST(ExecutableDiscovery, ReturnsNulloptForMissingExecutable) {
    auto tmp = fs::temp_directory_path() / "exec_disc_missing";
    fs::create_directories(tmp);

    ExecutableDiscovery disc;
    auto result = disc.find_on_path("ffmpeg", build_path({tmp}));

    EXPECT_FALSE(result.has_value());

    fs::remove_all(tmp);
}

TEST(ExecutableDiscovery, RespectsPathOrder) {
    // Two directories both contain the executable — first dir must win.
    auto dir_a = fs::temp_directory_path() / "exec_disc_order_a";
    auto dir_b = fs::temp_directory_path() / "exec_disc_order_b";
    fs::create_directories(dir_a);
    fs::create_directories(dir_b);
    make_executable(dir_a / "mpv");
    make_executable(dir_b / "mpv");

    ExecutableDiscovery disc;
    auto result = disc.find_on_path("mpv", build_path({dir_a, dir_b}));

    EXPECT_TRUE(result.has_value());
    // The returned path should be inside dir_a, not dir_b.
    EXPECT_EQ(result->parent_path(), dir_a);

    fs::remove_all(dir_a);
    fs::remove_all(dir_b);
}

TEST(ExecutableDiscovery, PathWithSpaces) {
    auto tmp = fs::temp_directory_path() / "exec disc spaces";
    fs::create_directories(tmp);
    make_executable(tmp / "mpv");

    ExecutableDiscovery disc;
    auto result = disc.find_on_path("mpv", build_path({tmp}));

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->parent_path(), tmp);

    fs::remove_all(tmp);
}

#ifndef _WIN32
TEST(ExecutableDiscovery, NonExecutableFileNotReturned) {
    // A file that exists but lacks execute permission should be skipped.
    auto tmp = fs::temp_directory_path() / "exec_disc_noexec";
    fs::create_directories(tmp);
    make_non_executable(tmp / "mpv");

    ExecutableDiscovery disc;
    auto result = disc.find_on_path("mpv", build_path({tmp}));

    EXPECT_FALSE(result.has_value());

    fs::remove_all(tmp);
}
#endif

TEST(ExecutableDiscovery, EmptyPathEnvReturnsNullopt) {
    ExecutableDiscovery disc;
    auto result = disc.find_on_path("mpv", "");
    EXPECT_FALSE(result.has_value());
}

TEST(ExecutableDiscovery, SkipsEmptySegments) {
    // PATH entries like ":/usr/bin:" have empty segments that must be skipped.
    auto tmp = fs::temp_directory_path() / "exec_disc_empty_seg";
    fs::create_directories(tmp);
    make_executable(tmp / "mpv");

#ifdef _WIN32
    std::string path_env = ";" + tmp.string() + ";";
#else
    std::string path_env = ":" + tmp.string() + ":";
#endif

    ExecutableDiscovery disc;
    auto result = disc.find_on_path("mpv", path_env);

    EXPECT_TRUE(result.has_value());

    fs::remove_all(tmp);
}

#ifdef _WIN32
TEST(ExecutableDiscovery, FindsExeSuffixOnWindows) {
    // On Windows, searching for "mpv" should find "mpv.exe".
    auto tmp = fs::temp_directory_path() / "exec_disc_win_exe";
    fs::create_directories(tmp);
    make_executable(tmp / "mpv.exe");

    ExecutableDiscovery disc;
    auto result = disc.find_on_path("mpv", build_path({tmp}));

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->filename().string(), "mpv.exe");

    fs::remove_all(tmp);
}

TEST(ExecutableDiscovery, DoesNotDoubleAppendExe) {
    // If the caller already passes "mpv.exe", the search must not look for
    // "mpv.exe.exe".
    auto tmp = fs::temp_directory_path() / "exec_disc_win_nodup";
    fs::create_directories(tmp);
    make_executable(tmp / "mpv.exe");

    ExecutableDiscovery disc;
    auto result = disc.find_on_path("mpv.exe", build_path({tmp}));

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->filename().string(), "mpv.exe");

    fs::remove_all(tmp);
}
#endif
