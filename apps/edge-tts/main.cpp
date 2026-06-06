#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"
#include "edge_tts/cli/EdgeTtsArgumentParser.hpp"
#include "edge_tts/cli/EdgeTtsCommandDispatcher.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/HttpClient.hpp"
#include "edge_tts/communication/VoiceService.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/serialization/VoiceJsonParser.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    using namespace edge_tts;

    // Parse arguments first so --proxy is available when constructing clients.
    cli::EdgeTtsArgumentParser parser;
    auto result = parser.parse(argc, argv);

    // Production dependencies.
    auto svc_config = communication::default_edge_service_config();
    common::IdGenerator           ids;
    serialization::VoiceJsonParser voice_parser;

    // Forward --proxy from CLI into the HTTP client.
    // http_timeout comes from CommunicateOptions defaults (30 s).
    communication::HttpClientOptions http_opts;
    http_opts.proxy   = result.arguments.proxy;
    http_opts.timeout = api::CommunicateOptions{}.http_timeout;
    communication::HttpClient http{std::move(http_opts)};

    communication::VoiceService voice_svc{svc_config, http, voice_parser, ids};

    cli::EdgeTtsCommandDispatcher dispatcher{
        [&voice_svc]() { return voice_svc.list_voices(); },
        [](std::string text, core::TtsConfig cfg, api::CommunicateOptions opts) {
            return api::Communicate{std::move(text), std::move(cfg), std::move(opts)};
        },
        std::cout,
        std::cerr,
        std::cin
    };

    return dispatcher.dispatch(result);
}
