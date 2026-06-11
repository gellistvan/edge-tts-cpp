#include "communication/EdgeServiceConfig.hpp"

// Keep this file as the ONLY place in the C++ codebase that hard-codes Edge TTS
// service constants.  Networking and serialization code must receive them via
// EdgeServiceConfig, never inline them.

namespace edge_tts::communication {

EdgeServiceConfig default_edge_service_config()
{
    EdgeServiceConfig cfg;

    cfg.websocket_endpoint =
        "wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud"
        "/edge/v1?TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4";

    cfg.voices_endpoint =
        "https://speech.platform.bing.com/consumer/speech/synthesize/readaloud"
        "/voices/list?trustedclienttoken=6A5AA1D4EAFF4E9FB37E23D68491D6F4";

    cfg.trusted_client_token = "6A5AA1D4EAFF4E9FB37E23D68491D6F4";

    cfg.sec_ms_gec_version = "1-143.0.3650.75";
    cfg.origin = "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold";
    cfg.user_agent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
        " (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36 Edg/143.0.0.0";

    cfg.speech_config_path  = "speech.config";
    cfg.ssml_path           = "ssml";
    cfg.audio_metadata_path = "audio.metadata";
    cfg.turn_end_path       = "turn.end";

    return cfg;
}

} // namespace edge_tts::communication
