#pragma once

#include "core/Voice.hpp"

#include <span>
#include <string>

namespace edge_tts::cli {

// Formats the voice list for --list-voices output.
//
// Reference: util.py _print_voices() — Python tabulate "simple" format:
//   - Sorted ascending by ShortName.
//   - Four columns: Name, Gender, ContentCategories, VoicePersonalities.
//   - ContentCategories and VoicePersonalities are comma-joined.
//   - Each column is left-aligned and padded to the maximum of the header
//     width and the widest value in the column.
//   - Columns are separated by two spaces.
//   - A separator row of dashes follows the header.
//   - Output ends with a trailing newline.
//
// This class belongs in the cli layer; formatting must not live in
// communication::VoiceService.
class VoiceFormatter {
public:
    // Returns the formatted table string.
    // The output matches the Python tabulate "simple" format as closely as
    // practical.  Column widths are computed from byte lengths — sufficient
    // for the ASCII-dominant voice names in the Edge TTS service.
    [[nodiscard]] std::string format(std::span<const core::Voice> voices) const;
};

} // namespace edge_tts::cli
