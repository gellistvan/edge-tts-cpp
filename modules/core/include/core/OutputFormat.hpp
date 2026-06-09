#pragma once

#include "common/Result.hpp"

#include <string>
#include <string_view>

namespace edge_tts::core {

// The audio output format sent in the WebSocket speech.config message.
//
//   "outputFormat":"audio-24khz-48kbitrate-mono-mp3"
//
// constructor parameter.  from_string() therefore accepts only values from the
// known supported set and rejects everything else.
//
// If Microsoft adds additional supported formats they should be added to
// OutputFormat::known_formats() and documented in REFERENCE_BEHAVIOR.md.
class OutputFormat {
public:
    // Returns the default output format:
    // "audio-24khz-48kbitrate-mono-mp3"
    [[nodiscard]] static OutputFormat default_format();

    // Constructs an OutputFormat from a string value.
    // Returns an error if value is empty or not in the known supported set.
    [[nodiscard]] static common::Result<OutputFormat> from_string(std::string_view value);

    // Returns the wire-format string, e.g. "audio-24khz-48kbitrate-mono-mp3".
    [[nodiscard]] std::string_view value() const noexcept;

    [[nodiscard]] bool operator==(const OutputFormat& other) const noexcept;
    [[nodiscard]] bool operator!=(const OutputFormat& other) const noexcept;

private:
    explicit OutputFormat(std::string v) noexcept;

    std::string value_;
};

} // namespace edge_tts::core
