#pragma once

#include "cli/EdgeTtsArguments.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace edge_tts::cli {

// What the parser concluded from the argument list.
// Reference: util.py amain() — special cases for --help, --version,
// --list-voices vs. proceeding to _run_tts().
enum class ParseAction {
    synthesize,  // --text or --file given; proceed to TTS
    list_voices, // --list-voices given; fetch and print voices then exit 0
    help,        // --help / -h given; print help then exit 0
    version,     // --version given; print version then exit 0
    error,       // invalid arguments; print error to stderr then exit 2
};

// Result of EdgeTtsArgumentParser::parse().
struct ParseResult {
    ParseAction      action{ParseAction::error};
    EdgeTtsArguments arguments;  // meaningful when action == synthesize
    std::string      message;    // help text, version string, or error description
    int              exit_code{2};
};

// Stateless, testable argument parser for the edge-tts command.
//
// Does NOT perform synthesis, open files, make network calls, print anything,
// or call exit().  All side-effects belong in the caller (main.cpp).
//
// Reference: util.py amain() argparse configuration.
//
// Supported options:
//   -t / --text TEXT           what TTS will say
//   -f / --file PATH           same as --text but read from file
//   -l / --list-voices         list voices and exit
//   -v / --voice VOICE         voice name (default: en-US-EmmaMultilingualNeural)
//       --rate RATE            speech rate (default: +0%)
//       --volume VOL           speech volume (default: +0%)
//       --pitch PITCH          speech pitch (default: +0Hz)
//       --write-media PATH     write MP3 to file instead of stdout
//       --write-subtitles PATH write SRT to file instead of stderr
//       --proxy URL            HTTP proxy for TTS and voice list
//   -h / --help                show help and exit
//       --version              show version and exit
class EdgeTtsArgumentParser {
public:
    // Parse a C-style argument list.  argv[0] (program name) is skipped.
    [[nodiscard]] ParseResult parse(int argc, const char* const* argv) const;

    // Parse a token list (no argv[0]).  Preferred in unit tests.
    [[nodiscard]] ParseResult parse(const std::vector<std::string>& args) const;

    // Full help text, formatted to match the Python reference help output.
    [[nodiscard]] std::string help_text(
        std::string_view program_name = "edge-tts") const;

    // Version string: "edge-tts-cpp 0.1.0"
    // Deviation from Python "edge-tts 7.2.8" — see CLI_COMPATIBILITY.md.
    [[nodiscard]] static std::string version_string();
};

} // namespace edge_tts::cli
