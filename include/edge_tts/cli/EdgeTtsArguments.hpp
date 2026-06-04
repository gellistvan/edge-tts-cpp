#pragma once

#include <optional>
#include <string>

namespace edge_tts::cli {

// Parsed arguments for the edge-tts command.
//
// Reference: util.py UtilArgs / amain() argparse setup, constants.py DEFAULT_VOICE.
//
// Exactly one of text, file, or list_voices must be set after a successful
// parse — they form the mutually exclusive required group.
struct EdgeTtsArguments {
    // Reference default voice (constants.py DEFAULT_VOICE).
    static constexpr const char* kDefaultVoice = "en-US-EmmaMultilingualNeural";

    // --- Mutually exclusive required group ----------------------------------
    std::optional<std::string> text;               // --text / -t
    std::optional<std::string> file;               // --file / -f (path string; not opened here)
    bool                       list_voices{false}; // --list-voices / -l

    // --- TTS configuration --------------------------------------------------
    std::string                voice{kDefaultVoice}; // --voice / -v
    std::string                rate{"+0%"};           // --rate
    std::string                volume{"+0%"};         // --volume
    std::string                pitch{"+0Hz"};         // --pitch

    // --- Output paths -------------------------------------------------------
    // Absent → default behavior (audio → stdout, subtitles → none).
    // "-" → stdout (media) or stderr (subtitles), per CLI_COMPATIBILITY.md.
    std::optional<std::string> write_media;      // --write-media
    std::optional<std::string> write_subtitles;  // --write-subtitles

    // --- Network ------------------------------------------------------------
    std::optional<std::string> proxy;            // --proxy
};

} // namespace edge_tts::cli
