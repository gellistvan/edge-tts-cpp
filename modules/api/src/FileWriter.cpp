#include "api/FileWriter.hpp"
#include "common/Error.hpp"

#include <fstream>

namespace edge_tts::api {

common::Result<void> FileWriter::write_binary(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes) const
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::io_error,
                          "Failed to open file for binary write",
                          path.string()});
    }
    if (!bytes.empty()) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!file) {
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::io_error,
                          "Failed to write binary data",
                          path.string()});
    }
    return common::Result<void>::ok();
}

common::Result<void> FileWriter::write_text_utf8(
    const std::filesystem::path& path,
    std::string_view text) const
{
    // Open in binary mode to suppress platform newline translation.
    // SRT output from SrtComposer uses LF (\n) only; binary mode guarantees
    // the bytes written to disk match the bytes in the string_view on every
    // platform, including Windows where text mode would silently convert
    // \n → \r\n and produce CRLF files that differ from the Linux/macOS output.
    //
    // Convention: all text written through this function uses LF (\n) only.
    // Callers must not pass CRLF (\r\n) strings unless they intentionally want
    // CR bytes in the file.
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::io_error,
                          "Failed to open file for text write",
                          path.string()});
    }
    if (!text.empty()) {
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    if (!file) {
        return common::Result<void>::fail(
            common::Error{common::ErrorCode::io_error,
                          "Failed to write text data",
                          path.string()});
    }
    return common::Result<void>::ok();
}

} // namespace edge_tts::api
