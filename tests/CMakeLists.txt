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
    LIBRARIES edge_tts::communication
)

edge_tts_add_module_test(
    NAME media
    SOURCES common/test_main.cpp
    LIBRARIES edge_tts::media
)

edge_tts_add_module_test(
    NAME cli
    SOURCES
        common/test_main.cpp
        cli/test_cli.cpp
    LIBRARIES edge_tts::cli
)
