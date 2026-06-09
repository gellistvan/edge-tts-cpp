#pragma once

#include "edge_tts/api/SpeechSynthesizer.hpp"
#include "edge_tts/api/SynthesisOptions.hpp"
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
// voice listing or TTS synthesis based on the parsed action.
//
// All dependencies are injected so the dispatcher is fully testable without
// real network I/O, real filesystem operations, or real process streams.
//
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

    // Creates an api::SpeechSynthesizer object for the given text, speech config,
    // and transport options (proxy, timeouts).  Inject a fake synthesizer
    // in tests; the factory receives options so --proxy reaches the client.
    using SynthesizerFactory = std::function<
        api::SpeechSynthesizer(std::string text, core::TtsConfig config,
                         api::SynthesisOptions options)>;

    // Returns true when both stdin and stdout are interactive TTYs.
    //
    //
    // Inject a custom implementation in tests (e.g. always-true or always-false).
    // When the function is empty (default), TTY detection is skipped entirely
    // and the interactive warning is never shown — safe for automated environments.
    using TtyCheckFn = std::function<bool()>;

    // Inject streams so tests can capture or supply input without touching
    // the real process stdin/stdout/stderr.
    //
    // tty_check: optional TTY detector.  Omit (or pass {}) to disable the
    //   interactive warning; pass real_tty_check() in production main.cpp.
    EdgeTtsCommandDispatcher(
        VoiceServiceFn     voice_service,
        SynthesizerFactory synthesizer_factory,
        std::ostream&      out,       // stdout equivalent
        std::ostream&      err,       // stderr equivalent
        std::istream&      in,        // stdin equivalent
        TtyCheckFn         tty_check = {});  // TTY detector; empty = no warning

    // Dispatch the parsed result.
    //
    //   0  — success (including --help, --version, list-voices, synthesis)
    //   1  — runtime error (service, synthesis, or file I/O failure)
    //   2  — argument error (invalid parse result)
    int dispatch(const ParseResult& result);

private:
    VoiceServiceFn     voice_service_;
    SynthesizerFactory synthesizer_factory_;
    std::ostream&      out_;
    std::ostream&      err_;
    std::istream&      in_;
    TtyCheckFn         tty_check_;

    int dispatch_list_voices();
    int dispatch_synthesize(const EdgeTtsArguments& args);
};

} // namespace edge_tts::cli
