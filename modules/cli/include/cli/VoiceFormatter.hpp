#pragma once

#include "core/Voice.hpp"

#include <span>
#include <string>

namespace edge_tts::cli {

// Formats the voice list for --list-voices output.
//
//   - Sorted ascending by ShortName.
//   - Four columns: Name, Gender, ContentCategories, VoicePersonalities.
//   - ContentCategories and VoicePersonalities are comma-joined.
//   - Each column is left-aligned and padded to the maximum of header and data widths.
//   - Columns are separated by two spaces.
//   - A separator row of dashes follows the header.
//   - Output ends with a trailing newline.
//
// This class belongs in the cli layer; formatting must not live in
// communication::VoiceService.
class VoiceFormatter {
public:
    [[nodiscard]] std::string format(std::span<const core::Voice> voices) const;
};

} // namespace edge_tts::cli
