#include "edge_tts/api/SpeechSynthesizer.hpp"
#include "edge_tts/api/SynthesisOptions.hpp"
#include "edge_tts/cli/EdgeTtsArgumentParser.hpp"
#include "edge_tts/cli/EdgeTtsCommandDispatcher.hpp"
#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/EdgeTokenProvider.hpp"
#include "edge_tts/communication/HttpClient.hpp"
#include "edge_tts/communication/VoiceService.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/serialization/VoiceJsonParser.hpp"

#include <iostream>
#include <unistd.h>

int main(int argc, char* argv[]) {
    using namespace edge_tts;

    // Parse arguments first so --proxy is available when constructing clients.
    cli::EdgeTtsArgumentParser parser;
    auto result = parser.parse(argc, argv);

    // Production dependencies.
    auto svc_config = communication::default_edge_service_config();
    common::SystemClock            clock;
    common::IdGenerator            ids;
    serialization::VoiceJsonParser voice_parser;
    communication::EdgeTokenProvider tokens{svc_config, clock};

    // Forward --proxy from CLI into the HTTP client.
    // http_timeout comes from SynthesisOptions defaults (30 s).
    communication::HttpClientOptions http_opts;
    http_opts.proxy   = result.arguments.proxy;
    http_opts.timeout = api::SynthesisOptions{}.http_timeout;
    communication::HttpClient http{std::move(http_opts)};

    communication::VoiceService voice_svc{svc_config, http, voice_parser, ids, tokens};

    cli::EdgeTtsCommandDispatcher dispatcher{
        [&voice_svc]() { return voice_svc.list_voices(); },
        [](std::string text, core::TtsConfig cfg, api::SynthesisOptions opts) {
            return api::SpeechSynthesizer{std::move(text), std::move(cfg), std::move(opts)};
        },
        std::cout,
        std::cerr,
        std::cin,
        // Real TTY check: both stdin and stdout must be interactive terminals.
        // Reference: util.py _run_tts() — sys.stdin.isatty() and sys.stdout.isatty()
        []{ return ::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0; }
    };

    return dispatcher.dispatch(result);
}
