#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "cli/EdgeTtsArgumentParser.hpp"
#include "cli/EdgeTtsCommandDispatcher.hpp"
#include "common/Clock.hpp"
#include "common/IdGenerator.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/HttpClient.hpp"
#include "communication/VoiceService.hpp"
#include "core/TtsConfig.hpp"
#include "serialization/VoiceJsonParser.hpp"

#include <iostream>
#include <unistd.h>

int main(int argc, char* argv[]) {
    using namespace edge_tts;

    cli::EdgeTtsArgumentParser parser;
    auto result = parser.parse(argc, argv);

    // Proxy is not supported — reject before constructing any client.
    if (result.arguments.proxy.has_value()) {
        std::cerr << "Error: proxy is not supported\n";
        return 1;
    }

    // Production dependencies.
    auto svc_config = communication::default_edge_service_config();
    common::SystemClock            clock;
    common::IdGenerator            ids;
    serialization::VoiceJsonParser voice_parser;
    communication::EdgeTokenProvider tokens{svc_config, clock};

    communication::HttpClientOptions http_opts;
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
        []{ return ::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0; }
    };

    return dispatcher.dispatch(result);
}
