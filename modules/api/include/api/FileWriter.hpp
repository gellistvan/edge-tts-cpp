#pragma once

#include "common/Result.hpp"

#include <filesystem>
#include <span>
#include <string_view>

namespace edge_tts::api {

// Writes media and subtitle files to disk.
//
// Both methods open the file in binary mode so the bytes written to disk are
// identical on all platforms.  On Windows, the default text mode would
// silently translate \n → \r\n, making SRT output non-deterministic.
//
// SRT newline convention: LF (\n) only.  SrtComposer produces LF-only output;
// write_text_utf8 preserves those bytes unchanged.  Do not pass CRLF strings
// unless you intentionally want CR bytes in the file.
//
// Existing files are truncated; if the parent directory is absent the write
// fails with an io_error Result whose context contains the path.
//
// stdout/stderr routing is a CLI concern and does not belong here.
class FileWriter {
public:
    // Write raw bytes to path in binary mode, creating or truncating the file.
    // All byte values (including 0x00 and 0x1A) are written verbatim.
    [[nodiscard]] common::Result<void> write_binary(
        const std::filesystem::path& path,
        std::span<const std::byte> bytes) const;

    // Write UTF-8 text to path in binary mode, creating or truncating the file.
    // Newlines in the string_view are written verbatim — no translation occurs.
    // Callers supply LF-only strings; write_text_utf8 does not add or strip \r.
    [[nodiscard]] common::Result<void> write_text_utf8(
        const std::filesystem::path& path,
        std::string_view text) const;
};

} // namespace edge_tts::api
