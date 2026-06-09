# EdgeTtsAddTest is included from the parent CMakeLists.txt before this
# subdirectory is processed, so edge_tts_add_module_test() is available here.

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
        common/test_main.cpp
        common/ErrorTests.cpp
        common/ResultTests.cpp
        common/ClockTests.cpp
        common/HexTests.cpp
        common/IdGeneratorTests.cpp
        common/Utf8Tests.cpp
        common/Sha256Tests.cpp
    LIBRARIES edge_tts::common
)

edge_tts_add_module_test(
    NAME core
    SOURCES
        common/test_main.cpp
        core/test_tts_config.cpp
        core/test_voice.cpp
        core/OutputFormatTests.cpp
        core/TtsConfigTests.cpp
        core/VoiceTests.cpp
        core/ChunkTests.cpp
    LIBRARIES edge_tts::core
)

edge_tts_add_module_test(
    NAME subtitle
    SOURCES
        common/test_main.cpp
        subtitles/test_srt_composer.cpp
        subtitles/SubtitleTimeTests.cpp
        subtitles/SrtComposerTests.cpp
        subtitles/SubMakerTests.cpp
    LIBRARIES edge_tts::subtitle
)

edge_tts_add_module_test(
    NAME serialization
    SOURCES
        common/test_main.cpp
        serialization/XmlEscaperTests.cpp
        serialization/TextNormalizerTests.cpp
        serialization/TextChunkerTests.cpp
        serialization/SsmlBuilderTests.cpp
        serialization/VoiceJsonParserTests.cpp
        serialization/ProtocolFrameTests.cpp
        serialization/MetadataJsonParserTests.cpp
    LIBRARIES edge_tts::serialization
)

edge_tts_add_module_test(
    NAME communication
    SOURCES
        common/test_main.cpp
        communication/EdgeServiceConfigTests.cpp
        communication/EdgeTokenProviderTests.cpp
        communication/ConnectionMetadataTests.cpp
        communication/HttpClientInterfaceTests.cpp
        communication/HttpClientTests.cpp
        communication/VoiceServiceTests.cpp
        communication/EdgeRequestHeadersTests.cpp
        communication/EdgeProtocolSpeechConfigTests.cpp
        communication/EdgeProtocolSsmlFrameTests.cpp
        communication/EdgeProtocolIncomingTests.cpp
        communication/FakeWebSocketClientTests.cpp
        communication/HttpDateTests.cpp
        communication/SynthesisSessionTests.cpp
        communication/WebSocketClientTests.cpp
        communication/RetryPolicyTests.cpp
        communication/CommunicateStyleFlowTests.cpp
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
            common/test_main.cpp
            communication/HttpClientNetworkTests.cpp
            communication/WebSocketClientNetworkTests.cpp
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
        common/test_main.cpp
        media/ExecutableDiscoveryTests.cpp
        media/FfmpegAudioConverterTests.cpp
    LIBRARIES edge_tts::media edge_tts_test_support
)

# ProcessRunnerTests.cpp uses POSIX APIs (::pipe, real ProcessRunner with
# fork/execvp) and can only be compiled on POSIX platforms.
# FakeProcessRunner behavior is covered by FfmpegAudioConverterTests.cpp on all platforms.
if(NOT WIN32)
    target_sources(edge_tts_media_tests PRIVATE media/ProcessRunnerTests.cpp)
endif()

edge_tts_add_module_test(
    NAME api
    SOURCES
        common/test_main.cpp
        api/FileWriterTests.cpp
        api/CommunicateTests.cpp
        api/CommunicateOptionsTests.cpp
        api/CommunicateProductionWiringTests.cpp
        api/CommunicateEndToEndTests.cpp
        api/OfflineIntegrationTests.cpp
    LIBRARIES edge_tts::api edge_tts::cli edge_tts_test_support
)

if(EDGE_TTS_ENABLE_NETWORK_TESTS)
    edge_tts_add_module_test(
        NAME api_network
        SOURCES
            common/test_main.cpp
            api/CommunicateNetworkTests.cpp
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
            common/test_main.cpp
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
        common/test_main.cpp
        cli/test_cli.cpp
        cli/EdgeTtsArgumentParserTests.cpp
        cli/InputLoaderTests.cpp
        cli/VoiceFormatterTests.cpp
        cli/EdgeTtsCommandDispatcherTests.cpp
        cli/PlaybackCommandDispatcherTests.cpp
        cli/ListVoicesProductionPathTests.cpp
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
        common/test_main.cpp
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
# Scope: all headers installed under include/edge_tts/ — api/, core/, common/,
# subtitles/, communication/, serialization/, media/, cli/.
#
# All of these headers include only other edge_tts/ headers and C++ standard
# library headers; none pull in nlohmann/json, ixwebsocket, or POSIX headers
# directly (those external dependencies are hidden in .cpp files).  Linking
# against edge_tts::tts provides the include/  directory, which is sufficient
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
    "edge_tts/api/Communicate.hpp"
    "edge_tts/api/CommunicateOptions.hpp"
    "edge_tts/api/FileWriter.hpp"
    # core
    "edge_tts/core/Chunk.hpp"
    "edge_tts/core/TtsChunk.hpp"
    "edge_tts/core/TtsConfig.hpp"
    "edge_tts/core/Voice.hpp"
    "edge_tts/core/OutputFormat.hpp"
    # common
    "edge_tts/common/Clock.hpp"
    "edge_tts/common/Error.hpp"
    "edge_tts/common/Errors.hpp"
    "edge_tts/common/Expected.hpp"
    "edge_tts/common/Result.hpp"
    "edge_tts/common/Hex.hpp"
    "edge_tts/common/IdGenerator.hpp"
    "edge_tts/common/Sha256.hpp"
    "edge_tts/common/Utf8.hpp"
    # subtitles
    "edge_tts/subtitles/SrtComposer.hpp"
    "edge_tts/subtitles/SubMaker.hpp"
    "edge_tts/subtitles/SubtitleCue.hpp"
    "edge_tts/subtitles/Subtitle.hpp"
    "edge_tts/subtitles/SubtitleTime.hpp"
    # communication (internal transport — installed, not part of stable API)
    "edge_tts/communication/ConnectionMetadata.hpp"
    "edge_tts/communication/EdgeProtocol.hpp"
    "edge_tts/communication/EdgeRequestHeaders.hpp"
    "edge_tts/communication/EdgeServiceConfig.hpp"
    "edge_tts/communication/EdgeTokenProvider.hpp"
    "edge_tts/communication/HttpClient.hpp"
    "edge_tts/communication/HttpDate.hpp"
    "edge_tts/communication/HttpTypes.hpp"
    "edge_tts/communication/IHttpClient.hpp"
    "edge_tts/communication/IncomingMessage.hpp"
    "edge_tts/communication/IWebSocketClient.hpp"
    "edge_tts/communication/RetryPolicy.hpp"
    "edge_tts/communication/SynthesisSession.hpp"
    "edge_tts/communication/VoiceService.hpp"
    "edge_tts/communication/WebSocketClient.hpp"
    "edge_tts/communication/WebSocketMessage.hpp"
    # serialization (internal protocol — installed, not part of stable API)
    "edge_tts/serialization/MetadataJsonParser.hpp"
    "edge_tts/serialization/ProtocolMessage.hpp"
    "edge_tts/serialization/ProtocolParser.hpp"
    "edge_tts/serialization/ProtocolSerializer.hpp"
    "edge_tts/serialization/SsmlBuilder.hpp"
    "edge_tts/serialization/TextChunker.hpp"
    "edge_tts/serialization/TextNormalizer.hpp"
    "edge_tts/serialization/VoiceJsonParser.hpp"
    "edge_tts/serialization/XmlEscaper.hpp"
    # media (app-layer — installed, not part of stable API)
    "edge_tts/media/AudioConverter.hpp"
    "edge_tts/media/ExecutableDiscovery.hpp"
    "edge_tts/media/FfmpegAudioConverter.hpp"
    "edge_tts/media/ProcessRunner.hpp"
    # cli (app-layer — installed, not part of stable API)
    "edge_tts/cli/EdgeTtsArgumentParser.hpp"
    "edge_tts/cli/EdgeTtsArguments.hpp"
    "edge_tts/cli/EdgeTtsCommandDispatcher.hpp"
    "edge_tts/cli/InputLoader.hpp"
    "edge_tts/cli/PlaybackArguments.hpp"
    "edge_tts/cli/PlaybackCommandDispatcher.hpp"
    "edge_tts/cli/VoiceFormatter.hpp"
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
        common/test_main.cpp
        ${_SELFCONTAINMENT_SOURCES}
    LIBRARIES edge_tts::tts
)
