#pragma once

#include "edge_tts/common/Result.hpp"

#include <filesystem>
#include <span>
#include <string_view>

namespace edge_tts::api {

// Writes media and subtitle files to disk.
//

// Existing files are truncated; if the parent directory is absent the write
// fails with an io_error Result whose context contains the path.
//
// stdout/stderr routing is a CLI concern and does not belong here.
class FileWriter {
public:
    // Write raw bytes to path, creating or truncating the file.
    [[nodiscard]] common::Result<void> write_binary(
        const std::filesystem::path& path,
        std::span<const std::byte> bytes) const;

    // Write UTF-8 text to path, creating or truncating the file.
    [[nodiscard]] common::Result<void> write_text_utf8(
        const std::filesystem::path& path,
        std::string_view text) const;
};

} // namespace edge_tts::api
