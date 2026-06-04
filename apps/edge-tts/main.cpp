#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/cli/EdgeTtsArgumentParser.hpp"
#include "edge_tts/cli/EdgeTtsCommandDispatcher.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/FakeHttpClient.hpp"
#include "edge_tts/communication/VoiceService.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/serialization/VoiceJsonParser.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    using namespace edge_tts;

    // Parse arguments.
    cli::EdgeTtsArgumentParser parser;
    auto result = parser.parse(argc, argv);

    // Wire production dependencies.
    //
    // TODO: replace FakeHttpClient with a real HTTP client once transport is ready.
    // VoiceService::list_voices() will return a network_error until then.
    communication::FakeHttpClient      http;
    serialization::VoiceJsonParser     voice_parser;
    auto svc_config = communication::default_edge_service_config();
    communication::VoiceService        voice_svc{svc_config, http, voice_parser};

    cli::EdgeTtsCommandDispatcher dispatcher{
        [&voice_svc]() { return voice_svc.list_voices(); },
        [](std::string text, core::TtsConfig cfg) {
            return api::Communicate{std::move(text), std::move(cfg)};
        },
        std::cout,
        std::cerr,
        std::cin
    };

    return dispatcher.dispatch(result);
}
