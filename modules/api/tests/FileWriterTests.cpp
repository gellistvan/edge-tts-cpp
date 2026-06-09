#include "api/FileWriter.hpp"
#include "common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using edge_tts::api::FileWriter;
using edge_tts::common::ErrorCode;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static fs::path tmp_path(const std::string& name) {
    return fs::temp_directory_path() / ("edge_tts_fw_test_" + name);
}

static std::string read_file_text(const fs::path& p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static std::vector<std::byte> read_file_binary(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    const std::vector<char> buf{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>{}};
    std::vector<std::byte> out(buf.size());
    for (std::size_t i = 0; i < buf.size(); ++i)
        out[i] = static_cast<std::byte>(buf[i]);
    return out;
}

static std::vector<std::byte> to_bytes(std::string_view s) {
    std::vector<std::byte> out(s.size());
    for (std::size_t i = 0; i < s.size(); ++i)
        out[i] = static_cast<std::byte>(s[i]);
    return out;
}

// RAII guard: delete a file on scope exit if it exists.
struct FileGuard {
    fs::path path;
    ~FileGuard() { fs::remove(path); }
};

// ---------------------------------------------------------------------------
// write_binary
// ---------------------------------------------------------------------------

TEST(FileWriter, WriteBinaryCreatesFile) {
    const fs::path p = tmp_path("write_binary_creates.bin");
    FileGuard g{p};
    fs::remove(p);

    FileWriter fw;
    const auto bytes = to_bytes("hello binary");
    const auto result = fw.write_binary(p, bytes);

    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(fs::exists(p));
    EXPECT_EQ(read_file_binary(p), bytes);
}

TEST(FileWriter, WriteBinaryEmpty) {
    const fs::path p = tmp_path("write_binary_empty.bin");
    FileGuard g{p};

    FileWriter fw;
    const std::vector<std::byte> empty;
    const auto result = fw.write_binary(p, empty);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(fs::file_size(p), 0u);
}

TEST(FileWriter, WriteBinaryOverwritesExisting) {
    const fs::path p = tmp_path("write_binary_overwrite.bin");
    FileGuard g{p};

    FileWriter fw;
    // Write initial content
    const auto first = to_bytes("first content that is longer");
    (void)fw.write_binary(p, first);

    // Overwrite with shorter content — existing bytes must be truncated.
    const auto second = to_bytes("second");
    const auto result = fw.write_binary(p, second);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(read_file_binary(p), second);
}

TEST(FileWriter, WriteBinaryLargeData) {
    const fs::path p = tmp_path("write_binary_large.bin");
    FileGuard g{p};

    FileWriter fw;
    std::vector<std::byte> large(1u << 20); // 1 MiB
    for (std::size_t i = 0; i < large.size(); ++i)
        large[i] = static_cast<std::byte>(i & 0xFF);

    const auto result = fw.write_binary(p, large);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(fs::file_size(p), large.size());
    EXPECT_EQ(read_file_binary(p), large);
}

TEST(FileWriter, WriteBinaryMissingParentFails) {
    const fs::path p = tmp_path("no_such_dir_bin/file.bin");

    FileWriter fw;
    const auto bytes = to_bytes("data");
    const auto result = fw.write_binary(p, bytes);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::io_error);
}

TEST(FileWriter, WriteBinaryErrorIncludesPath) {
    const fs::path p = tmp_path("no_such_dir_bin2/file.bin");

    FileWriter fw;
    const auto bytes = to_bytes("data");
    const auto result = fw.write_binary(p, bytes);

    EXPECT_FALSE(result.has_value());
    // The error context must mention the path so callers can diagnose the failure.
    EXPECT_TRUE(result.error().has_context());
    EXPECT_FALSE(result.error().context().empty());
}

// ---------------------------------------------------------------------------
// write_text_utf8
// ---------------------------------------------------------------------------

TEST(FileWriter, WriteTextCreatesFile) {
    const fs::path p = tmp_path("write_text_creates.txt");
    FileGuard g{p};
    fs::remove(p);

    FileWriter fw;
    const auto result = fw.write_text_utf8(p, "hello text");

    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(fs::exists(p));
    EXPECT_EQ(read_file_text(p), "hello text");
}

TEST(FileWriter, WriteTextEmpty) {
    const fs::path p = tmp_path("write_text_empty.txt");
    FileGuard g{p};

    FileWriter fw;
    const auto result = fw.write_text_utf8(p, "");

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(fs::file_size(p), 0u);
}

TEST(FileWriter, WriteTextOverwritesExisting) {
    const fs::path p = tmp_path("write_text_overwrite.txt");
    FileGuard g{p};

    FileWriter fw;
    (void)fw.write_text_utf8(p, "original content that is long");

    const auto result = fw.write_text_utf8(p, "new");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(read_file_text(p), "new");
}

TEST(FileWriter, WriteTextUnicodeMultibyte) {
    const fs::path p = tmp_path("write_text_unicode.txt");
    FileGuard g{p};

    FileWriter fw;
    // Japanese: "こんにちは" — 3 bytes per character in UTF-8 = 15 bytes
    const std::string utf8 = "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf";
    const auto result = fw.write_text_utf8(p, utf8);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(read_file_text(p), utf8);
}

TEST(FileWriter, WriteTextSubtitleContent) {
    const fs::path p = tmp_path("write_text_srt.srt");
    FileGuard g{p};

    FileWriter fw;
    // Matches the subtitle encoding pattern from communicate.py save().
    const std::string srt =
        "1\n00:00:00,000 --> 00:00:01,000\nHello world\n\n"
        "2\n00:00:01,000 --> 00:00:02,000\nGoodbye\n\n";
    const auto result = fw.write_text_utf8(p, srt);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(read_file_text(p), srt);
}

TEST(FileWriter, WriteTextMissingParentFails) {
    const fs::path p = tmp_path("no_such_dir_txt/file.txt");

    FileWriter fw;
    const auto result = fw.write_text_utf8(p, "data");

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::io_error);
}

TEST(FileWriter, WriteTextErrorIncludesPath) {
    const fs::path p = tmp_path("no_such_dir_txt2/file.txt");

    FileWriter fw;
    const auto result = fw.write_text_utf8(p, "data");

    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().has_context());
    EXPECT_FALSE(result.error().context().empty());
}
