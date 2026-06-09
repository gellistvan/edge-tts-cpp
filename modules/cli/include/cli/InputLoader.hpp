#pragma once

#include "cli/EdgeTtsArguments.hpp"
#include "cli/PlaybackArguments.hpp"
#include "common/Result.hpp"

#include <istream>
#include <string>

namespace edge_tts::cli {

// Loads the synthesis input text from the parsed CLI arguments.
//
// Reference: util.py amain() — text resolution order:
//   1. args.text is set → return it directly.
//   2. args.file is set:
//        "-" or "/dev/stdin" → read entire stdin_stream.
//        otherwise           → open the file (UTF-8) and read it entirely.
//   3. Neither set          → error (caller should not reach this via the parser).
//
// No text normalization, chunking, or synthesis here.
// The caller supplies the stdin stream so the class is fully testable
// without touching the real process stdin.
class InputLoader {
public:
    // Load input text from edge-tts arguments.
    // stdin_stream is read only when args.file is "-" or "/dev/stdin".
    [[nodiscard]] common::Result<std::string> load(
        const EdgeTtsArguments& args,
        std::istream&           stdin_stream) const;

    // Load input text from edge-playback arguments.
    // Same resolution order; stdin_stream is read only when args.file is "-"
    // or "/dev/stdin".
    [[nodiscard]] common::Result<std::string> load_playback(
        const PlaybackArguments& args,
        std::istream&            stdin_stream) const;
};

} // namespace edge_tts::cli
