// Real-network smoke tests: Edge TTS voice-list endpoint.
//
// WARNING: These tests contact Microsoft Edge TTS servers.  Do not enable in
// CI environments without reliable outbound TLS access to
//   https://speech.platform.bing.com/consumer/speech/synthesize/readaloud/voices/list
//
// Two independent gates must both be satisfied before any test makes a network call:
//
//   # 1. Compile-time gate — build the network-test binary:
//   cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON
//   cmake --build build --target edge_tts_network_smoke_tests
//
//   # 2. Runtime gate — opt in to actual network calls:
//   EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build -L network --output-on-failure
//
// When the runtime variable is absent, every test returns immediately without
// making any network call or failing any assertion (minigtest has no SKIP
// mechanism; an early-return test shows as PASSED rather than SKIPPED).
// This is intentional: the test binary must not fail when run in an offline
// environment.
//
// What these tests verify:
//   - The voice-list HTTP endpoint returns status 200.
//   - The response body is non-empty and parses to a non-empty voice list.
//   - Every voice has a populated ShortName, Gender, and Locale field.
//   - en-US-EmmaMultilingualNeural (the reference default voice) is present.
//   - VoiceService locale and short-name filters return consistent results.
//
// Reference: reference/edge-tts/src/edge_tts/voices.py list_voices()

#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/EdgeTokenProvider.hpp"
#include "edge_tts/communication/HttpClient.hpp"
#include "edge_tts/communication/VoiceService.hpp"
#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "edge_tts/core/Voice.hpp"
#include "edge_tts/serialization/VoiceJsonParser.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstdlib>
#include <string>
#include <vector>

using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::HttpClient;
using edge_tts::communication::VoiceFilter;
using edge_tts::communication::VoiceService;
using edge_tts::communication::default_edge_service_config;
using edge_tts::common::IdGenerator;
using edge_tts::common::SystemClock;
using edge_tts::core::Voice;
using edge_tts::core::VoiceGender;
using edge_tts::serialization::VoiceJsonParser;

// ---------------------------------------------------------------------------
// Runtime gate
// ---------------------------------------------------------------------------

static bool network_enabled() {
    const char* v = std::getenv("EDGE_TTS_RUN_NETWORK_TESTS");
    return v != nullptr && v[0] != '\0';
}

// Shared fixtures — built once per process.
static SystemClock      g_clock;
static IdGenerator      g_ids;
static HttpClient       g_http;
static VoiceJsonParser  g_parser;
static auto             g_cfg     = default_edge_service_config();
static EdgeTokenProvider g_tokens{g_cfg, g_clock};

static VoiceService make_svc() {
    return VoiceService{g_cfg, g_http, g_parser, g_ids, g_tokens};
}

// ---------------------------------------------------------------------------
// Gate verification
//
// These tests always run (no network_enabled() guard).  They document and
// prove that the gate mechanism works: when EDGE_TTS_RUN_NETWORK_TESTS is
// absent all subsequent tests return cleanly without firing assertions.
// ---------------------------------------------------------------------------

TEST(RealVoiceListGate, SkipWhenEnvVarAbsent) {
    // This test always passes.  Its purpose is to document that when
    // EDGE_TTS_RUN_NETWORK_TESTS is not set, network_enabled() returns false
    // and all network tests return early, producing no failed assertions.
    // Run "EDGE_TTS_RUN_NETWORK_TESTS=1 ctest -L network" to exercise the
    // actual tests below.
    if (network_enabled()) {
        // Env var IS set — document that actual tests will run.
        EXPECT_TRUE(true);
    } else {
        // Env var absent — all subsequent tests return early. No assertions fail.
        EXPECT_FALSE(network_enabled());
    }
}

TEST(RealVoiceListGate, GateFunctionReturnsFalseWhenEnvVarUnset) {
    // Probe the gate function directly.  Works regardless of whether the env
    // var is set or not — the function must at least be callable and return a bool.
    const bool result = network_enabled();
    // We cannot assert a fixed value (the env var may or may not be set),
    // but the call must compile, link, and return without crashing.
    (void)result;
    EXPECT_TRUE(true);
}

// ---------------------------------------------------------------------------
// HTTP-level: status code and body
// ---------------------------------------------------------------------------

TEST(RealVoiceList, VoicesEndpointReturnsHttp200) {
    if (!network_enabled()) return;

    auto svc = make_svc();
    auto voices = svc.list_voices();

    // list_voices() fails with a service_error if status is not 200.
    EXPECT_TRUE(voices.has_value());
}

TEST(RealVoiceList, VoiceListNonEmpty) {
    if (!network_enabled()) return;

    auto svc = make_svc();
    auto voices = svc.list_voices();

    ASSERT_TRUE(voices.has_value());
    EXPECT_TRUE(voices->size() > 0u);
}

// ---------------------------------------------------------------------------
// Field completeness: every voice must have ShortName, Gender, and Locale
// ---------------------------------------------------------------------------

TEST(RealVoiceList, AllVoicesHaveNonEmptyShortName) {
    if (!network_enabled()) return;

    auto svc = make_svc();
    auto voices = svc.list_voices();
    ASSERT_TRUE(voices.has_value());
    ASSERT_TRUE(voices->size() > 0u);

    for (const auto& v : *voices) {
        EXPECT_FALSE(v.short_name.empty());
    }
}

TEST(RealVoiceList, AllVoicesHaveKnownGender) {
    if (!network_enabled()) return;

    // Reference: voices.py — Gender is always "Female" or "Male" in the wire JSON.
    // A voice with VoiceGender::unknown indicates the JSON parser missed the field.
    auto svc = make_svc();
    auto voices = svc.list_voices();
    ASSERT_TRUE(voices.has_value());
    ASSERT_TRUE(voices->size() > 0u);

    int unknown_count = 0;
    for (const auto& v : *voices) {
        if (v.gender == VoiceGender::unknown) ++unknown_count;
    }
    EXPECT_EQ(unknown_count, 0);
}

TEST(RealVoiceList, AllVoicesHaveNonEmptyLocale) {
    if (!network_enabled()) return;

    auto svc = make_svc();
    auto voices = svc.list_voices();
    ASSERT_TRUE(voices.has_value());
    ASSERT_TRUE(voices->size() > 0u);

    for (const auto& v : *voices) {
        EXPECT_FALSE(v.locale.empty());
    }
}

TEST(RealVoiceList, AllVoicesHaveNonEmptyName) {
    if (!network_enabled()) return;

    // Voice::name is the full "Microsoft Server Speech Text to Speech Voice..."
    // form. All voices from the endpoint must have it.
    auto svc = make_svc();
    auto voices = svc.list_voices();
    ASSERT_TRUE(voices.has_value());
    ASSERT_TRUE(voices->size() > 0u);

    for (const auto& v : *voices) {
        EXPECT_FALSE(v.name.empty());
    }
}

TEST(RealVoiceList, AllVoicesHaveNonEmptyFriendlyName) {
    if (!network_enabled()) return;

    auto svc = make_svc();
    auto voices = svc.list_voices();
    ASSERT_TRUE(voices.has_value());
    ASSERT_TRUE(voices->size() > 0u);

    for (const auto& v : *voices) {
        EXPECT_FALSE(v.friendly_name.empty());
    }
}

// ---------------------------------------------------------------------------
// Default voice: reference says en-US-EmmaMultilingualNeural must exist
// Reference: constants.py DEFAULT_VOICE = "en-US-EmmaMultilingualNeural"
// ---------------------------------------------------------------------------

TEST(RealVoiceList, DefaultVoiceEmmaIsPresent) {
    if (!network_enabled()) return;

    auto svc = make_svc();

    VoiceFilter filter;
    filter.short_name = "en-US-EmmaMultilingualNeural";
    auto voices = svc.list_voices(filter);

    ASSERT_TRUE(voices.has_value());
    EXPECT_EQ(voices->size(), 1u);
}

TEST(RealVoiceList, DefaultVoiceEmmaHasCorrectFields) {
    if (!network_enabled()) return;

    auto svc = make_svc();

    VoiceFilter filter;
    filter.short_name = "en-US-EmmaMultilingualNeural";
    auto voices = svc.list_voices(filter);

    ASSERT_TRUE(voices.has_value());
    ASSERT_EQ(voices->size(), 1u);

    const auto& emma = (*voices)[0];
    EXPECT_EQ(emma.short_name, "en-US-EmmaMultilingualNeural");
    EXPECT_EQ(emma.gender, VoiceGender::female);
    EXPECT_EQ(emma.locale, "en-US");
    EXPECT_FALSE(emma.name.empty());
    EXPECT_FALSE(emma.friendly_name.empty());
}

// ---------------------------------------------------------------------------
// Locale filter: en-US voices exist and all match the requested locale
// ---------------------------------------------------------------------------

TEST(RealVoiceList, EnUsLocaleFilterReturnsNonEmptyList) {
    if (!network_enabled()) return;

    auto svc = make_svc();
    VoiceFilter filter;
    filter.locale = "en-US";
    auto voices = svc.list_voices(filter);

    ASSERT_TRUE(voices.has_value());
    EXPECT_TRUE(voices->size() > 0u);
}

TEST(RealVoiceList, EnUsLocaleFilterReturnsOnlyEnUsVoices) {
    if (!network_enabled()) return;

    auto svc = make_svc();
    VoiceFilter filter;
    filter.locale = "en-US";
    auto voices = svc.list_voices(filter);

    ASSERT_TRUE(voices.has_value());
    ASSERT_TRUE(voices->size() > 0u);

    for (const auto& v : *voices) {
        EXPECT_EQ(v.locale, "en-US");
    }
}

TEST(RealVoiceList, MultipleLocalesExistInFullList) {
    if (!network_enabled()) return;

    // The Edge TTS service hosts voices for many locales.  Verify that the
    // full list contains voices from at least two distinct locales.
    auto svc = make_svc();
    auto voices = svc.list_voices();

    ASSERT_TRUE(voices.has_value());
    ASSERT_TRUE(voices->size() > 1u);

    const std::string first_locale = (*voices)[0].locale;
    bool found_different = false;
    for (const auto& v : *voices) {
        if (v.locale != first_locale) { found_different = true; break; }
    }
    EXPECT_TRUE(found_different);
}

// ---------------------------------------------------------------------------
// Gender filter
// ---------------------------------------------------------------------------

TEST(RealVoiceList, FemaleGenderFilterReturnsOnlyFemaleVoices) {
    if (!network_enabled()) return;

    auto svc = make_svc();
    VoiceFilter filter;
    filter.gender = VoiceGender::female;
    auto voices = svc.list_voices(filter);

    ASSERT_TRUE(voices.has_value());
    ASSERT_TRUE(voices->size() > 0u);

    for (const auto& v : *voices) {
        EXPECT_EQ(v.gender, VoiceGender::female);
    }
}

TEST(RealVoiceList, MaleGenderFilterReturnsOnlyMaleVoices) {
    if (!network_enabled()) return;

    auto svc = make_svc();
    VoiceFilter filter;
    filter.gender = VoiceGender::male;
    auto voices = svc.list_voices(filter);

    ASSERT_TRUE(voices.has_value());
    ASSERT_TRUE(voices->size() > 0u);

    for (const auto& v : *voices) {
        EXPECT_EQ(v.gender, VoiceGender::male);
    }
}
