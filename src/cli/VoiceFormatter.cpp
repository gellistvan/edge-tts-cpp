#include "edge_tts/cli/VoiceFormatter.hpp"
#include "edge_tts/core/Voice.hpp"

#include <algorithm>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace edge_tts::cli {

namespace {

// Join a vector of strings with ", " — matches Python's ", ".join(...).
std::string join(const std::vector<std::string>& parts) {
    if (parts.empty()) return {};
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += ", ";
        out += parts[i];
    }
    return out;
}

// Left-pad str to exactly width bytes (appending spaces).
// No truncation — strings wider than width are returned unchanged.
std::string pad(const std::string& str, std::size_t width) {
    if (str.size() >= width) return str;
    return str + std::string(width - str.size(), ' ');
}

// Produce one output line: cells joined by two spaces, no trailing spaces.
// Reference: tabulate uses two spaces between columns and does NOT add
// trailing spaces after the last column.
std::string make_row(const std::vector<std::string>& cells,
                     const std::vector<std::size_t>& widths,
                     bool last_col_raw = false) {
    std::string line;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (i > 0) line += "  ";
        // Last column: no trailing pad (tabulate omits trailing whitespace).
        if (last_col_raw && i + 1 == cells.size())
            line += cells[i];
        else
            line += pad(cells[i], widths[i]);
    }
    return line;
}

} // namespace

std::string VoiceFormatter::format(std::span<const core::Voice> voices) const {
    // --- 1. Sort by ShortName ascending. -----------------------------------
    // Reference: voices = sorted(voices, key=lambda voice: voice["ShortName"])
    std::vector<std::size_t> order(voices.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return voices[a].short_name < voices[b].short_name;
    });

    // --- 2. Build cell rows. -----------------------------------------------
    // Reference columns: ShortName, Gender, ContentCategories, VoicePersonalities
    const std::vector<std::string> headers = {
        "Name", "Gender", "ContentCategories", "VoicePersonalities"};

    std::vector<std::vector<std::string>> rows;
    rows.reserve(voices.size());
    for (std::size_t idx : order) {
        const auto& v = voices[idx];
        rows.push_back({
            v.short_name,
            std::string{core::to_string(v.gender)},
            join(v.content_categories),
            join(v.voice_personalities),
        });
    }

    // --- 3. Compute column widths. -----------------------------------------
    const std::size_t ncols = headers.size();
    std::vector<std::size_t> widths(ncols);
    for (std::size_t c = 0; c < ncols; ++c)
        widths[c] = headers[c].size();
    for (const auto& row : rows)
        for (std::size_t c = 0; c < ncols; ++c)
            widths[c] = std::max(widths[c], row[c].size());

    // --- 4. Render. --------------------------------------------------------
    std::ostringstream out;

    // Header row.
    out << make_row(headers, widths, true) << '\n';

    // Separator: one dash string per column width, joined by two spaces.
    {
        std::vector<std::string> sep(ncols);
        for (std::size_t c = 0; c < ncols; ++c)
            sep[c] = std::string(widths[c], '-');
        out << make_row(sep, widths, true) << '\n';
    }

    // Data rows.
    for (const auto& row : rows)
        out << make_row(row, widths, true) << '\n';

    return out.str();
}

} // namespace edge_tts::cli
