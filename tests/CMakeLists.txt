# EdgeTtsAddTest is included from the parent CMakeLists.txt before this
# subdirectory is processed, so edge_tts_add_module_test() is available here.

# Shorthand variables for each module's test directory.
set(_M "${EDGE_TTS_SOURCE_DIR}/modules")
set(_TEST_MAIN "${_M}/common/tests/test_main.cpp")

# ---------------------------------------------------------------------------
# Test-support library: fake in-memory implementations of production interfaces.
# Only test targets may link against this.  Production modules must not.
# ---------------------------------------------------------------------------
add_library(edge_tts_test_support STATIC
    support/FakeWebSocketClient.cpp
    support/FakeHttpClient.cpp
    support/FakeProcessRunner.cpp
)
target_compile_features(edge_tts_test_support PUBLIC cxx_std_20)
# tests/support/ shadows include/ for fake headers — resolved before installed headers.
target_include_directories(edge_tts_test_support PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
)
target_link_libraries(edge_tts_test_support PUBLIC
    edge_tts::communication
    edge_tts::media
)

edge_tts_add_module_test(
    NAME common
    SOURCES
        ${_TEST_MAIN}
        ${_M}/common/tests/ErrorTests.cpp
        ${_M}/common/tests/ResultTests.cpp
        ${_M}/common/tests/ClockTests.cpp
        ${_M}/common/tests/HexTests.cpp
        ${_M}/common/tests/IdGeneratorTests.cpp
        ${_M}/common/tests/Utf8Tests.cpp
        ${_M}/common/tests/Sha256Tests.cpp
    LIBRARIES edge_tts::common
)

edge_tts_add_module_test(
    NAME core
    SOURCES
        ${_TEST_MAIN}
        ${_M}/core/tests/test_tts_config.cpp
        ${_M}/core/tests/test_voice.cpp
        ${_M}/core/tests/OutputFormatTests.cpp
        ${_M}/core/tests/TtsConfigTests.cpp
        ${_M}/core/tests/VoiceTests.cpp
        ${_M}/core/tests/ChunkTests.cpp
    LIBRARIES edge_tts::core
)

edge_tts_add_module_test(
    NAME subtitle
    SOURCES
        ${_TEST_MAIN}
        ${_M}/subtitle/tests/test_srt_composer.cpp
        ${_M}/subtitle/tests/SubtitleTimeTests.cpp
        ${_M}/subtitle/tests/SrtComposerTests.cpp
        ${_M}/subtitle/tests/SubMakerTests.cpp
    LIBRARIES edge_tts::subtitle
)

edge_tts_add_module_test(
    NAME serialization
    SOURCES
        ${_TEST_MAIN}
        ${_M}/serialization/tests/XmlEscaperTests.cpp
        ${_M}/serialization/tests/TextNormalizerTests.cpp
        ${_M}/serialization/tests/TextChunkerTests.cpp
        ${_M}/serialization/tests/SsmlBuilderTests.cpp
        ${_M}/serialization/tests/VoiceJsonParserTests.cpp
        ${_M}/serialization/tests/ProtocolFrameTests.cpp
        ${_M}/serialization/tests/MetadataJsonParserTests.cpp
    LIBRARIES edge_tts::serialization
)

edge_tts_add_module_test(
    NAME communication
    SOURCES
        ${_TEST_MAIN}
        ${_M}/communication/tests/EdgeServiceConfigTests.cpp
        ${_M}/communication/tests/EdgeTokenProviderTests.cpp
        ${_M}/communication/tests/ConnectionMetadataTests.cpp
        ${_M}/communication/tests/HttpClientInterfaceTests.cpp
        ${_M}/communication/tests/HttpClientTests.cpp
        ${_M}/communication/tests/VoiceServiceTests.cpp
        ${_M}/communication/tests/EdgeRequestHeadersTests.cpp
        ${_M}/communication/tests/EdgeProtocolSpeechConfigTests.cpp
        ${_M}/communication/tests/EdgeProtocolSsmlFrameTests.cpp
        ${_M}/communication/tests/EdgeProtocolIncomingTests.cpp
        ${_M}/communication/tests/FakeWebSocketClientTests.cpp
        ${_M}/communication/tests/HttpDateTests.cpp
        ${_M}/communication/tests/SynthesisSessionTests.cpp
        ${_M}/communication/tests/SessionCancellationTests.cpp
        ${_M}/communication/tests/WebSocketClientTests.cpp
        ${_M}/communication/tests/RetryPolicyTests.cpp
        ${_M}/communication/tests/CommunicateStyleFlowTests.cpp
    LIBRARIES edge_tts::communication edge_tts_test_support
)

# Propagate ixwebsocket availability to the offline communication tests so
# the #ifdef EDGE_TTS_HAVE_IXWEBSOCKET guards in HttpClientTests.cpp and
# WebSocketClientTests.cpp fire correctly.
if(TARGET ixwebsocket)
    target_compile_definitions(edge_tts_communication_tests
        PRIVATE EDGE_TTS_HAVE_IXWEBSOCKET=1)
endif()

if(EDGE_TTS_ENABLE_NETWORK_TESTS)
    edge_tts_add_module_test(
        NAME communication_network
        SOURCES
            ${_TEST_MAIN}
            ${_M}/communication/tests/HttpClientNetworkTests.cpp
            ${_M}/communication/tests/WebSocketClientNetworkTests.cpp
        LIBRARIES edge_tts::communication
    )
    # Propagate the ixwebsocket availability flag to network tests as well.
    if(TARGET ixwebsocket)
        target_compile_definitions(edge_tts_communication_network_tests
            PRIVATE EDGE_TTS_HAVE_IXWEBSOCKET=1)
    endif()
endif()

edge_tts_add_module_test(
    NAME media
    SOURCES
        ${_TEST_MAIN}
        ${_M}/media/tests/ExecutableDiscoveryTests.cpp
        ${_M}/media/tests/FfmpegAudioConverterTests.cpp
    LIBRARIES edge_tts::media edge_tts_test_support
)

# ProcessRunnerTests.cpp uses POSIX APIs (::pipe, real ProcessRunner with
# fork/execvp) and can only be compiled on POSIX platforms.
# FakeProcessRunner behavior is covered by FfmpegAudioConverterTests.cpp on all platforms.
if(NOT WIN32)
    target_sources(edge_tts_media_tests PRIVATE ${_M}/media/tests/ProcessRunnerTests.cpp)
endif()

edge_tts_add_module_test(
    NAME api
    SOURCES
        ${_TEST_MAIN}
        ${_M}/api/tests/FileWriterTests.cpp
        ${_M}/api/tests/SpeechSynthesizerTests.cpp
        ${_M}/api/tests/SynthesisOptionsTests.cpp
        ${_M}/api/tests/SpeechSynthesizerWiringTests.cpp
        ${_M}/api/tests/SpeechSynthesizerEndToEndTests.cpp
        ${_M}/api/tests/OfflineIntegrationTests.cpp
        ${_M}/api/tests/PublicApiContractTests.cpp
        ${_M}/api/tests/CancellationTests.cpp
        ${_M}/api/tests/StreamingApiTests.cpp
        ${_M}/api/tests/TimeoutCancellationEdgeCaseTests.cpp
    LIBRARIES edge_tts::api edge_tts::cli edge_tts_test_support
)

if(EDGE_TTS_ENABLE_NETWORK_TESTS)
    edge_tts_add_module_test(
        NAME api_network
        SOURCES
            ${_TEST_MAIN}
            ${_M}/api/tests/SpeechSynthesizerNetworkTests.cpp
        LIBRARIES edge_tts::api
    )
    # Network tests require ixwebsocket for real WebSocket/HTTP connections.
    if(TARGET ixwebsocket)
        target_compile_definitions(edge_tts_api_network_tests
            PRIVATE EDGE_TTS_HAVE_IXWEBSOCKET=1)
    endif()

    # ---------------------------------------------------------------------------
    # Dedicated smoke-test target for real-service confidence checks.
    # Labels: "network" and "integration" so ctest -L network selects them.
    # ---------------------------------------------------------------------------
    edge_tts_add_module_test(
        NAME network_smoke
        SOURCES
            ${_TEST_MAIN}
            network/RealVoiceListTests.cpp
            network/RealSynthesisSmokeTests.cpp
        LIBRARIES edge_tts::api edge_tts::communication
    )
    if(TARGET ixwebsocket)
        target_compile_definitions(edge_tts_network_smoke_tests
            PRIVATE EDGE_TTS_HAVE_IXWEBSOCKET=1)
    endif()
    set_tests_properties(edge_tts_network_smoke_tests
        PROPERTIES LABELS "network;integration")
endif()

# Add network and integration labels to all other network test targets so that
# "ctest -L network" selects the complete set of real-service tests.
if(EDGE_TTS_ENABLE_NETWORK_TESTS)
    set_tests_properties(edge_tts_communication_network_tests
        PROPERTIES LABELS "network;integration")
    set_tests_properties(edge_tts_api_network_tests
        PROPERTIES LABELS "network;integration")
endif()

edge_tts_add_module_test(
    NAME cli
    SOURCES
        ${_TEST_MAIN}
        ${_M}/cli/tests/test_cli.cpp
        ${_M}/cli/tests/EdgeTtsArgumentParserTests.cpp
        ${_M}/cli/tests/InputLoaderTests.cpp
        ${_M}/cli/tests/VoiceFormatterTests.cpp
        ${_M}/cli/tests/EdgeTtsCommandDispatcherTests.cpp
        ${_M}/cli/tests/PlaybackCommandDispatcherTests.cpp
        ${_M}/cli/tests/ListVoicesProductionPathTests.cpp
    LIBRARIES edge_tts::cli edge_tts_test_support
)

# ---------------------------------------------------------------------------
# Umbrella header consumer test
#
# Verifies that <edge_tts/edge_tts.hpp> is sufficient for basic TTS usage
# when linked against edge_tts::tts.  The source file must not include any
# other edge_tts header directly.
# ---------------------------------------------------------------------------
edge_tts_add_module_test(
    NAME umbrella_header
    SOURCES
        ${_TEST_MAIN}
        headers/umbrella_consumer_test.cpp
    LIBRARIES edge_tts::tts
)

# ---------------------------------------------------------------------------
# Header self-containment tests
#
# For every installed public header, generate a tiny translation unit that
# includes it as the first (and only) project include, and add all of them to
# a single test executable.  If any header is not self-contained (missing its
# own prerequisites), compilation fails with a clear error.
#
# Scope: all headers installed under include/ — api/, core/, common/,
# subtitles/, communication/, serialization/, media/, cli/.
#
# All of these headers include only other module headers and C++ standard
# library headers; none pull in nlohmann/json, ixwebsocket, or POSIX headers
# directly (those external dependencies are hidden in .cpp files).  Linking
# against edge_tts::tts provides the include/ directory, which is sufficient
# for compilation of every header in the tree.
#
# The companion Python test (edge_tts_installed_header_selfcontainment_tests)
# exercises the same set against the *installed* prefix using the system
# C++ compiler, verifying that the install tree is complete and standalone.
# ---------------------------------------------------------------------------
set(_EDGE_TTS_ALL_PUBLIC_HEADERS
    # Umbrella + generated
    "edge_tts/edge_tts.hpp"
    "edge_tts/version.hpp"
    # api
    "api/SpeechSynthesizer.hpp"
    "api/StreamCallbacks.hpp"
    "api/SynthesisOptions.hpp"
    "api/FileWriter.hpp"
    "api/VoiceList.hpp"
    # core
    "core/Chunk.hpp"
    "core/TtsChunk.hpp"
    "core/TtsConfig.hpp"
    "core/Voice.hpp"
    "core/OutputFormat.hpp"
    # common
    "common/CancellationToken.hpp"
    "common/Clock.hpp"
    "common/Error.hpp"
    "common/Errors.hpp"
    "common/Expected.hpp"
    "common/Result.hpp"
    "common/Hex.hpp"
    "common/IdGenerator.hpp"
    "common/Sha256.hpp"
    "common/Utf8.hpp"
    # subtitles
    "subtitles/SrtComposer.hpp"
    "subtitles/SubMaker.hpp"
    "subtitles/SubtitleCue.hpp"
    "subtitles/Subtitle.hpp"
    "subtitles/SubtitleTime.hpp"
    # communication (internal transport — installed, not part of stable API)
    "communication/ConnectionMetadata.hpp"
    "communication/EdgeProtocol.hpp"
    "communication/EdgeRequestHeaders.hpp"
    "communication/EdgeServiceConfig.hpp"
    "communication/EdgeTokenProvider.hpp"
    "communication/HttpClient.hpp"
    "communication/HttpDate.hpp"
    "communication/HttpTypes.hpp"
    "communication/IHttpClient.hpp"
    "communication/IncomingMessage.hpp"
    "communication/IWebSocketClient.hpp"
    "communication/RetryPolicy.hpp"
    "communication/SynthesisSession.hpp"
    "communication/VoiceService.hpp"
    "communication/WebSocketClient.hpp"
    "communication/WebSocketMessage.hpp"
    # serialization (internal protocol — installed, not part of stable API)
    "serialization/MetadataJsonParser.hpp"
    "serialization/ProtocolMessage.hpp"
    "serialization/ProtocolParser.hpp"
    "serialization/ProtocolSerializer.hpp"
    "serialization/SsmlBuilder.hpp"
    "serialization/TextChunker.hpp"
    "serialization/TextNormalizer.hpp"
    "serialization/VoiceJsonParser.hpp"
    "serialization/XmlEscaper.hpp"
    # media (app-layer — installed, not part of stable API)
    "media/AudioConverter.hpp"
    "media/ExecutableDiscovery.hpp"
    "media/FfmpegAudioConverter.hpp"
    "media/ProcessRunner.hpp"
    # cli (app-layer — installed, not part of stable API)
    "cli/EdgeTtsArgumentParser.hpp"
    "cli/EdgeTtsArguments.hpp"
    "cli/EdgeTtsCommandDispatcher.hpp"
    "cli/InputLoader.hpp"
    "cli/PlaybackArguments.hpp"
    "cli/PlaybackCommandDispatcher.hpp"
    "cli/VoiceFormatter.hpp"
)

set(_SELFCONTAINMENT_SOURCES "")
foreach(_hdr IN LISTS _EDGE_TTS_ALL_PUBLIC_HEADERS)
    # Derive a safe filename: replace path separators and dots.
    string(REPLACE "/" "_" _safe "${_hdr}")
    string(REPLACE "." "_" _safe "${_safe}")
    set(_gen "${EDGE_TTS_BINARY_DIR}/header_selfcontainment/${_safe}.cpp")
    file(WRITE "${_gen}" "#include <${_hdr}>\n")
    list(APPEND _SELFCONTAINMENT_SOURCES "${_gen}")
endforeach()

edge_tts_add_module_test(
    NAME header_selfcontainment
    SOURCES
        ${_TEST_MAIN}
        ${_SELFCONTAINMENT_SOURCES}
    LIBRARIES
        edge_tts::tts
        edge_tts::media
        edge_tts::cli
)
