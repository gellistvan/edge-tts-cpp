#include "subtitles/SrtComposer.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <string>
#include <vector>

namespace edge_tts::subtitles {
namespace {

// Normalize subtitle text:
//   1. Strip leading/trailing newlines.
//   2. Collapse consecutive blank lines to a single newline.
std::string make_legal_content(const std::string& text)
{
    const auto first = text.find_first_not_of('\n');
    if (first == std::string::npos) return "";
    const auto last = text.find_last_not_of('\n');
    const std::string stripped = text.substr(first, last - first + 1);

    std::string out;
    out.reserve(stripped.size());
    bool prev_nl = false;
    for (const char c : stripped) {
        if (c == '\n') {
            if (!prev_nl) out += c;
            prev_nl = true;
        } else {
            prev_nl = false;
            out += c;
        }
    }
    return out;
}

// Returns true if text is empty or contains only whitespace.
bool is_blank(const std::string& text)
{
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

} // namespace

common::Result<std::string>
SrtComposer::compose(std::span<const SubtitleCue> cues) const
{
    // Build a sorted index by (start, end) ascending.
    std::vector<std::size_t> order(cues.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto sa = cues[a].start.milliseconds();
        const auto sb = cues[b].start.milliseconds();
        if (sa != sb) return sa < sb;
        return cues[a].end.milliseconds() < cues[b].end.milliseconds();
    });

    std::string out;
    int idx = 1;

    for (const auto i : order) {
        const auto& cue = cues[i];

        if (cue.start.milliseconds() >= cue.end.milliseconds()) continue;

        if (is_blank(cue.text)) continue;

        const std::string content = make_legal_content(cue.text);

        // Block format: "{idx}\n{start} --> {end}\n{content}\n\n"
        out += std::to_string(idx++);
        out += '\n';
        out += cue.start.to_srt_timestamp();
        out += " --> ";
        out += cue.end.to_srt_timestamp();
        out += '\n';
        out += content;
        out += "\n\n";
    }

    return common::Result<std::string>::ok(std::move(out));
}

} // namespace edge_tts::subtitles
