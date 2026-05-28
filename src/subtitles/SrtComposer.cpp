#include "edge_tts/subtitles/SrtComposer.hpp"

#include <iomanip>
#include <sstream>

namespace edge_tts::subtitles {
namespace {
std::string format_time(std::chrono::milliseconds value) {
    const auto total = value.count();
    const auto hours = total / 3'600'000;
    const auto minutes = (total / 60'000) % 60;
    const auto seconds = (total / 1'000) % 60;
    const auto millis = total % 1'000;

    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << hours << ':'
        << std::setw(2) << minutes << ':'
        << std::setw(2) << seconds << ','
        << std::setw(3) << millis;
    return out.str();
}
} // namespace

std::string SrtComposer::compose(const std::vector<SubtitleEntry>& entries) {
    std::ostringstream out;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        out << (i + 1) << '\n'
            << format_time(entries[i].start) << " --> " << format_time(entries[i].end) << '\n'
            << entries[i].text << "\n\n";
    }
    return out.str();
}

} // namespace edge_tts::subtitles
