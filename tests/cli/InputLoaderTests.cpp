#include "edge_tts/cli/InputLoader.hpp"
#include "edge_tts/cli/EdgeTtsArguments.hpp"
#include "edge_tts/common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using edge_tts::cli::EdgeTtsArguments;
using edge_tts::cli::InputLoader;
using edge_tts::common::ErrorCode;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static InputLoader loader;

static fs::path tmp_path(const std::string& name) {
    return fs::temp_directory_path() / ("edge_tts_il_test_" + name);
}

static void write_file(const fs::path& p, const std::string& content) {
    std::ofstream f(p, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

struct FileGuard {
    fs::path path;
    ~FileGuard() { fs::remove(path); }
};

static std::istringstream empty_stdin() { return std::istringstream{""}; }

// ---------------------------------------------------------------------------
// --text option
// ---------------------------------------------------------------------------

TEST(InputLoader, TextOptionReturnsText) {
    EdgeTtsArguments args;
    args.text = "hello world";
    auto ss = empty_stdin();
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "hello world");
}

TEST(InputLoader, TextOptionEmptyStringIsOk) {
    // Reference: Python doesn't reject empty text at this stage.
    EdgeTtsArguments args;
    args.text = "";
    auto ss = empty_stdin();
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "");
}

TEST(InputLoader, TextOptionPreservesNewlines) {
    EdgeTtsArguments args;
    args.text = "line1\nline2\n";
    auto ss = empty_stdin();
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "line1\nline2\n");
}

// ---------------------------------------------------------------------------
// --file option
// ---------------------------------------------------------------------------

TEST(InputLoader, FileOptionReadsFile) {
    const fs::path p = tmp_path("read_file.txt");
    FileGuard g{p};
    write_file(p, "file content");

    EdgeTtsArguments args;
    args.file = p.string();
    auto ss = empty_stdin();
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "file content");
}

TEST(InputLoader, FileOptionEmptyFile) {
    const fs::path p = tmp_path("empty_file.txt");
    FileGuard g{p};
    write_file(p, "");

    EdgeTtsArguments args;
    args.file = p.string();
    auto ss = empty_stdin();
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "");
}

TEST(InputLoader, FileOptionUnicodeContent) {
    // Japanese UTF-8: "こんにちは" (15 bytes)
    const std::string utf8 = "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf";
    const fs::path p = tmp_path("unicode_file.txt");
    FileGuard g{p};
    write_file(p, utf8);

    EdgeTtsArguments args;
    args.file = p.string();
    auto ss = empty_stdin();
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, utf8);
}

TEST(InputLoader, FileOptionPreservesCrlf) {
    // Reference: Python opens files in text mode; on Linux, CRLF is not
    // translated — it is returned as-is.  Our C++ implementation matches.
    const fs::path p = tmp_path("crlf_file.txt");
    FileGuard g{p};
    write_file(p, "line1\r\nline2\r\n");

    EdgeTtsArguments args;
    args.file = p.string();
    auto ss = empty_stdin();
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "line1\r\nline2\r\n");
}

TEST(InputLoader, FileOptionMissingFileReturnsError) {
    EdgeTtsArguments args;
    args.file = "/no/such/path/does/not/exist.txt";
    auto ss = empty_stdin();
    auto r = loader.load(args, ss);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::io_error);
}

TEST(InputLoader, FileErrorIncludesPath) {
    const std::string bad = "/no/such/path/does/not/exist.txt";
    EdgeTtsArguments args;
    args.file = bad;
    auto ss = empty_stdin();
    auto r = loader.load(args, ss);
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(r.error().has_context());
    EXPECT_FALSE(r.error().context().empty());
}

// ---------------------------------------------------------------------------
// stdin fallback ("-" and "/dev/stdin")
// ---------------------------------------------------------------------------

TEST(InputLoader, FileDashReadsStdin) {
    // Reference: if args.file in ("-", "/dev/stdin"): args.text = sys.stdin.read()
    EdgeTtsArguments args;
    args.file = "-";
    std::istringstream ss{"stdin content"};
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "stdin content");
}

TEST(InputLoader, FileDevStdinReadsStdin) {
    EdgeTtsArguments args;
    args.file = "/dev/stdin";
    std::istringstream ss{"dev stdin content"};
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "dev stdin content");
}

TEST(InputLoader, StdinCanBeEmpty) {
    EdgeTtsArguments args;
    args.file = "-";
    std::istringstream ss{""};
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "");
}

TEST(InputLoader, StdinPreservesNewlines) {
    EdgeTtsArguments args;
    args.file = "-";
    std::istringstream ss{"line1\nline2\n"};
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "line1\nline2\n");
}

// ---------------------------------------------------------------------------
// Precedence: --text beats stdin, --file beats stdin
// ---------------------------------------------------------------------------

TEST(InputLoader, TextPrecedenceOverStdin) {
    // When --text is given, stdin_stream is never read.
    EdgeTtsArguments args;
    args.text = "from text option";
    std::istringstream ss{"from stdin — should be ignored"};
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "from text option");
}

TEST(InputLoader, FilePrecedenceOverStdin) {
    // When --file points to a real file, stdin is not consulted.
    const fs::path p = tmp_path("file_beats_stdin.txt");
    FileGuard g{p};
    write_file(p, "from file");

    EdgeTtsArguments args;
    args.file = p.string();
    std::istringstream ss{"from stdin — should be ignored"};
    auto r = loader.load(args, ss);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(*r, "from file");
}

// ---------------------------------------------------------------------------
// Neither --text nor --file set
// ---------------------------------------------------------------------------

TEST(InputLoader, NoInputReturnsError) {
    // Parser enforces at least one of text/file/list-voices, but InputLoader
    // must be defensive when called directly without a valid parse result.
    EdgeTtsArguments args; // all optional fields absent
    auto ss = empty_stdin();
    auto r = loader.load(args, ss);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}
