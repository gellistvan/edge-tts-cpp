#include "core/OutputFormat.hpp"
#include "common/Error.hpp"

#include <algorithm>
#include <array>

namespace edge_tts::core {

namespace {

// Formats supported by the Edge TTS service.
// Only "audio-24khz-48kbitrate-mono-mp3" is currently used.
constexpr std::array<std::string_view, 1> KNOWN_FORMATS{
    "audio-24khz-48kbitrate-mono-mp3",
};

} // namespace

OutputFormat::OutputFormat(std::string v) noexcept : value_(std::move(v)) {}

OutputFormat OutputFormat::default_format() {
    return OutputFormat{std::string{KNOWN_FORMATS[0]}};
}

common::Result<OutputFormat> OutputFormat::from_string(std::string_view value) {
    if (value.empty()) {
        return common::Result<OutputFormat>::fail(
            common::Error{common::ErrorCode::invalid_argument,
                          "OutputFormat: value must not be empty"});
    }

    const auto it = std::find(KNOWN_FORMATS.begin(), KNOWN_FORMATS.end(), value);
    if (it == KNOWN_FORMATS.end()) {
        return common::Result<OutputFormat>::fail(
            common::Error{common::ErrorCode::unsupported,
                          "OutputFormat: unknown format",
                          std::string{value}});
    }

    return common::Result<OutputFormat>::ok(OutputFormat{std::string{value}});
}

std::string_view OutputFormat::value() const noexcept {
    return value_;
}

bool OutputFormat::operator==(const OutputFormat& other) const noexcept {
    return value_ == other.value_;
}

bool OutputFormat::operator!=(const OutputFormat& other) const noexcept {
    return !(*this == other);
}

} // namespace edge_tts::core
