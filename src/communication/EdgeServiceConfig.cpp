#include "edge_tts/communication/EdgeServiceConfig.hpp"

// All string literals in this file are derived verbatim from the Python reference.
// Reference files:
//   constants.py   — BASE_URL, TRUSTED_CLIENT_TOKEN, WSS_URL, VOICE_LIST,
//                    CHROMIUM_FULL_VERSION, SEC_MS_GEC_VERSION,
//                    BASE_HEADERS, WSS_HEADERS
//   communicate.py — protocol frame Path values, speech.config JSON structure
//   drm.py         — token algorithm uses TRUSTED_CLIENT_TOKEN
//
// Keep this file as the ONLY place in the C++ codebase that hard-codes these
// strings.  Networking and serialization code must receive them via
// EdgeServiceConfig, never inline them.

namespace edge_tts::communication {

EdgeServiceConfig default_edge_service_config()
{
    // Reference: constants.py
    //   BASE_URL              = "speech.platform.bing.com/consumer/speech/synthesize/readaloud"
    //   TRUSTED_CLIENT_TOKEN  = "6A5AA1D4EAFF4E9FB37E23D68491D6F4"
    //   WSS_URL               = f"wss://{BASE_URL}/edge/v1?TrustedClientToken={TRUSTED_CLIENT_TOKEN}"
    //   VOICE_LIST            = f"https://{BASE_URL}/voices/list?trustedclienttoken={TRUSTED_CLIENT_TOKEN}"
    //   CHROMIUM_FULL_VERSION = "143.0.3650.75"
    //   CHROMIUM_MAJOR_VERSION = "143"   (first segment of CHROMIUM_FULL_VERSION)
    //   SEC_MS_GEC_VERSION    = f"1-{CHROMIUM_FULL_VERSION}"
    //   BASE_HEADERS["User-Agent"] = (see below)
    //   WSS_HEADERS["Origin"] = "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold"

    EdgeServiceConfig cfg;

    cfg.websocket_endpoint =
        "wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud"
        "/edge/v1?TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4";

    cfg.voices_endpoint =
        "https://speech.platform.bing.com/consumer/speech/synthesize/readaloud"
        "/voices/list?trustedclienttoken=6A5AA1D4EAFF4E9FB37E23D68491D6F4";

    cfg.trusted_client_token = "6A5AA1D4EAFF4E9FB37E23D68491D6F4";

    // SEC_MS_GEC_VERSION = "1-143.0.3650.75"
    cfg.sec_ms_gec_version = "1-143.0.3650.75";

    // WSS_HEADERS["Origin"]
    cfg.origin = "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold";

    // BASE_HEADERS["User-Agent"]
    // Python: f"Mozilla/5.0 ... Chrome/{CHROMIUM_MAJOR_VERSION}.0.0.0 Safari/537.36 Edg/{CHROMIUM_MAJOR_VERSION}.0.0.0"
    // With CHROMIUM_MAJOR_VERSION = "143"
    cfg.user_agent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
        " (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36 Edg/143.0.0.0";

    // Protocol frame Path header values (communicate.py)
    cfg.speech_config_path  = "speech.config";
    cfg.ssml_path           = "ssml";
    cfg.audio_metadata_path = "audio.metadata";
    cfg.turn_end_path       = "turn.end";

    return cfg;
}

} // namespace edge_tts::communication
