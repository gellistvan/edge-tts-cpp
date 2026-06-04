#include "edge_tts/api/FileWriter.hpp"
#include "edge_tts/common/Error.hpp"

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
    // Open in text mode; the caller supplies a UTF-8 string_view.
    // Reference: communicate.py open(metadata_fname, "w", encoding="utf-8")
    std::ofstream file(path, std::ios::trunc);
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
