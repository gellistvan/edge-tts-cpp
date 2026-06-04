#pragma once

#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/cli/EdgeTtsArgumentParser.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/core/Voice.hpp"
#include "edge_tts/common/Result.hpp"

#include <functional>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace edge_tts::cli {

// Executes a parsed edge-tts command.
//
// Reference: util.py _print_voices() and _run_tts() — dispatches to either
// voice listing or TTS synthesis based on the parsed action.
//
// All dependencies are injected so the dispatcher is fully testable without
// real network I/O, real filesystem operations, or real process streams.
//
// Reference audio/subtitle routing (util.py _run_tts()):
//   write_media   absent or "-"   → audio bytes written to out (stdout)
//   write_media   non-dash path   → audio bytes written to file
//   write_subtitles absent        → no SRT output
//   write_subtitles "-"           → SRT written to err (stderr)
//   write_subtitles non-dash path → SRT written to file
class EdgeTtsCommandDispatcher {
public:
    // Returns Result<vector<Voice>>; wraps communication::VoiceService::list_voices().
    using VoiceServiceFn = std::function<
        common::Result<std::vector<core::Voice>>()>;

    // Creates an api::Communicate object for the given text and config.
    // Inject a fake synthesizer here in tests.
    using CommunicateFactory = std::function<
        api::Communicate(std::string text, core::TtsConfig config)>;

    // Inject streams so tests can capture or supply input without touching
    // the real process stdin/stdout/stderr.
    EdgeTtsCommandDispatcher(
        VoiceServiceFn     voice_service,
        CommunicateFactory communicate_factory,
        std::ostream&      out,   // stdout equivalent
        std::ostream&      err,   // stderr equivalent
        std::istream&      in);   // stdin equivalent

    // Dispatch the parsed result.
    //
    // Return values match the Python reference exit codes:
    //   0  — success (including --help, --version, list-voices, synthesis)
    //   1  — runtime error (service, synthesis, or file I/O failure)
    //   2  — argument error (invalid parse result)
    int dispatch(const ParseResult& result);

private:
    VoiceServiceFn     voice_service_;
    CommunicateFactory communicate_factory_;
    std::ostream&      out_;
    std::ostream&      err_;
    std::istream&      in_;

    int dispatch_list_voices();
    int dispatch_synthesize(const EdgeTtsArguments& args);
};

} // namespace edge_tts::cli
