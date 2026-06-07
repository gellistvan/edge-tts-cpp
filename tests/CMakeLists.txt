# EdgeTtsAddTest is included from the parent CMakeLists.txt before this
# subdirectory is processed, so edge_tts_add_module_test() is available here.

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
        core/test_text_chunker.cpp
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
    LIBRARIES edge_tts::communication
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
        media/ProcessRunnerTests.cpp
    LIBRARIES edge_tts::media
)

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
    LIBRARIES edge_tts::api edge_tts::cli
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
    LIBRARIES edge_tts::cli
)
