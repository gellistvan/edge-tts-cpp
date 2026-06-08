#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace edge_tts::cli {

// Parsed arguments for the edge-playback command.
//
// Reference: edge_playback/__main__.py _parse_args() / argument forwarding.
// edge-playback accepts a subset of edge-tts options (no --write-media,
// --write-subtitles, or --list-voices) plus one playback-specific flag (--mpv).
struct PlaybackArguments {
    static constexpr const char* kDefaultVoice = "en-US-EmmaMultilingualNeural";

    // --- Mutually exclusive required group -----------------------------------
    // Exactly one of text or file must be set after a successful parse.
    std::optional<std::string> text;  // -t / --text
    std::optional<std::string> file;  // -f / --file (path; not opened here)

    // --- TTS configuration (forwarded to Communicate) -----------------------
    std::string                voice{kDefaultVoice};  // --voice / -v
    std::string                rate{"+0%"};           // --rate
    std::string                volume{"+0%"};         // --volume
    std::string                pitch{"+0Hz"};         // --pitch
    std::optional<std::string> proxy;                 // --proxy

    // --- Playback -----------------------------------------------------------
    // Stored but rejected at dispatch time: the C++ implementation uses only
    // ffplay via IAudioConverter; --mpv is not supported.
    bool use_mpv{false};  // --mpv
};

// What the parser concluded.
enum class PlaybackParseAction {
    play,   // proceed to synthesis + playback
    help,   // print help and exit 0
    error,  // invalid arguments; print error to stderr and exit 2
};

// Result of PlaybackArgumentParser::parse().
struct PlaybackParseResult {
    PlaybackParseAction action{PlaybackParseAction::error};
    PlaybackArguments   arguments;  // meaningful when action == play
    std::string         message;    // help text or error description
    int                 exit_code{2};
};

// Stateless, testable argument parser for the edge-playback command.
//
// Does NOT perform synthesis, open files, make network calls, print anything,
// or call exit().  All side-effects belong in the caller (main.cpp).
//
// Supported options:
//   -t / --text TEXT       what TTS will say
//   -f / --file PATH       same as --text but read from file
//   -v / --voice VOICE     voice name (default: en-US-EmmaMultilingualNeural)
//       --rate RATE        speech rate (default: +0%)
//       --volume VOL       speech volume (default: +0%)
//       --pitch PITCH      speech pitch (default: +0Hz)
//       --proxy URL        HTTP proxy for TTS
//       --mpv              [not supported] parsed but rejected at dispatch
//   -h / --help            show help and exit
//
// Not accepted (unlike edge-tts):
//   --write-media, --write-subtitles, --list-voices / -l
class PlaybackArgumentParser {
public:
    // Parse a C-style argument list.  argv[0] (program name) is skipped.
    [[nodiscard]] PlaybackParseResult parse(int argc, const char* const* argv) const;

    // Parse a token list (no argv[0]).  Preferred in unit tests.
    [[nodiscard]] PlaybackParseResult parse(const std::vector<std::string>& args) const;

    // Full help text for edge-playback.
    [[nodiscard]] std::string help_text(
        std::string_view program_name = "edge-playback") const;
};

} // namespace edge_tts::cli
