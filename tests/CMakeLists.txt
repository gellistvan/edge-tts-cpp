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
        communication/SynthesisSessionTests.cpp
        communication/WebSocketClientTests.cpp
        communication/RetryPolicyTests.cpp
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
    LIBRARIES edge_tts::api edge_tts::cli
)

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
    LIBRARIES edge_tts::cli
)
