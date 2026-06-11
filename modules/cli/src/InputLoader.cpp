#include "cli/InputLoader.hpp"
#include "common/Error.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace edge_tts::cli {

common::Result<std::string> InputLoader::load(
    const EdgeTtsArguments& args,
    std::istream&           stdin_stream) const
{
    if (args.text.has_value())
        return common::Result<std::string>::ok(*args.text);

    // 2. --file was given.
    if (args.file.has_value()) {
        const std::string& path = *args.file;

        // "-" or "/dev/stdin" → read from the injected stdin stream.
        if (path == "-" || path == "/dev/stdin") {
            return common::Result<std::string>::ok(
                std::string{std::istreambuf_iterator<char>(stdin_stream),
                            std::istreambuf_iterator<char>{}});
        }

        // Guard against directory paths: on Linux ifstream opens directories but
        // throws std::ios_failure on the first read — return io_error instead.
        if (std::filesystem::is_directory(path)) {
            return common::Result<std::string>::fail(
                common::Error{common::ErrorCode::io_error,
                              "Input path is a directory, not a file",
                              path});
        }

        std::ifstream file(path);
        if (!file) {
            return common::Result<std::string>::fail(
                common::Error{common::ErrorCode::io_error,
                              "Cannot open input file",
                              path});
        }

        return common::Result<std::string>::ok(
            std::string{std::istreambuf_iterator<char>(file),
                        std::istreambuf_iterator<char>{}});
    }

    // 3. Neither --text nor --file: the parser should have caught this.
    return common::Result<std::string>::fail(
        common::Error{common::ErrorCode::invalid_argument,
                      "No input: provide --text, --file, or --list-voices"});
}

common::Result<std::string> InputLoader::load_playback(
    const PlaybackArguments& args,
    std::istream&            stdin_stream) const
{
    if (args.text.has_value())
        return common::Result<std::string>::ok(*args.text);

    if (args.file.has_value()) {
        const std::string& path = *args.file;

        if (path == "-" || path == "/dev/stdin") {
            return common::Result<std::string>::ok(
                std::string{std::istreambuf_iterator<char>(stdin_stream),
                            std::istreambuf_iterator<char>{}});
        }

        if (std::filesystem::is_directory(path)) {
            return common::Result<std::string>::fail(
                common::Error{common::ErrorCode::io_error,
                              "Input path is a directory, not a file",
                              path});
        }

        std::ifstream file(path);
        if (!file) {
            return common::Result<std::string>::fail(
                common::Error{common::ErrorCode::io_error,
                              "Cannot open input file",
                              path});
        }

        return common::Result<std::string>::ok(
            std::string{std::istreambuf_iterator<char>(file),
                        std::istreambuf_iterator<char>{}});
    }

    return common::Result<std::string>::fail(
        common::Error{common::ErrorCode::invalid_argument,
                      "No input: provide --text or --file"});
}

} // namespace edge_tts::cli
