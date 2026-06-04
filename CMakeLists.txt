cmake_minimum_required(VERSION 3.24)

project(edge_tts_cpp
    VERSION 0.1.0
    DESCRIPTION "Modern C++ client for Microsoft Edge text-to-speech service"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ProjectOptions)
include(EdgeTtsCompilerOptions)
include(EdgeTtsAddModule)
include(Dependencies)
include(StaticAnalysis)

edge_tts_setup_options()
edge_tts_configure_compile_options()   # creates edge_tts_compile_options
edge_tts_setup_dependencies()
edge_tts_setup_static_analysis()

# ---------------------------------------------------------------------------
# Library modules — explicit dependency graph; no module may depend upward.
# See docs/DEPENDENCY_RULES.md for the allowed dependency matrix.
# ---------------------------------------------------------------------------

edge_tts_add_module(
    NAME common
    SOURCES
        src/common/Error.cpp
        src/common/Clock.cpp
        src/common/Hex.cpp
        src/common/IdGenerator.cpp
        src/common/Utf8.cpp
        src/common/Sha256.cpp
)

edge_tts_add_module(
    NAME core
    SOURCES
        src/core/Chunk.cpp
        src/core/TextChunker.cpp
        src/core/TtsConfig.cpp
        src/core/OutputFormat.cpp
        src/core/Voice.cpp
    PUBLIC_DEPS edge_tts::common
)

edge_tts_add_module(
    NAME serialization
    SOURCES
        src/serialization/EdgeProtocol.cpp
        src/serialization/EdgeToken.cpp
        src/serialization/XmlEscaper.cpp
        src/serialization/TextNormalizer.cpp
        src/serialization/TextChunker.cpp
        src/serialization/SsmlBuilder.cpp
        src/serialization/VoiceJsonParser.cpp
        src/serialization/ProtocolMessage.cpp
        src/serialization/ProtocolParser.cpp
        src/serialization/ProtocolSerializer.cpp
        src/serialization/MetadataJsonParser.cpp
    PUBLIC_DEPS
        edge_tts::core
        edge_tts::common
)

edge_tts_add_module(
    NAME subtitle
    SOURCES
        src/subtitles/SrtComposer.cpp
        src/subtitles/SubMaker.cpp
        src/subtitles/SubtitleTime.cpp
    PUBLIC_DEPS edge_tts::core
)

edge_tts_add_module(
    NAME media
    SOURCES
        src/media/AudioConverter.cpp
    PUBLIC_DEPS edge_tts::common
)

edge_tts_add_module(
    NAME communication
    SOURCES
        src/communication/Communicate.cpp
        src/communication/ConnectionMetadata.cpp
        src/communication/EdgeProtocol.cpp
        src/communication/FakeHttpClient.cpp
        src/communication/VoiceService.cpp
        src/communication/EdgeServiceConfig.cpp
        src/communication/EdgeTokenProvider.cpp
        src/communication/HttpVoiceService.cpp
        src/communication/VoicesManager.cpp
        src/communication/WebSocketTransport.cpp
    PUBLIC_DEPS
        edge_tts::core
        edge_tts::serialization
        edge_tts::subtitle
        edge_tts::media
        edge_tts::common
)

edge_tts_add_module(
    NAME cli
    SOURCES
        src/cli/CliPlaceholder.cpp
    PUBLIC_DEPS
        edge_tts::communication
)

# ---------------------------------------------------------------------------
# Optional third-party dependencies (wired when submodules are present).
# ---------------------------------------------------------------------------
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(edge_tts_serialization PUBLIC nlohmann_json::nlohmann_json)
    target_link_libraries(edge_tts_communication PUBLIC nlohmann_json::nlohmann_json)
endif()
if(TARGET ixwebsocket)
    target_link_libraries(edge_tts_communication PRIVATE ixwebsocket)
endif()

# ---------------------------------------------------------------------------
# Aggregate convenience target — for examples and external consumers only.
# Apps and tests should link specific module targets, not this.
# ---------------------------------------------------------------------------
add_library(edge_tts INTERFACE)
add_library(edge_tts::edge_tts ALIAS edge_tts)
target_link_libraries(edge_tts
    INTERFACE
        edge_tts::common
        edge_tts::core
        edge_tts::serialization
        edge_tts::communication
        edge_tts::media
        edge_tts::subtitle
        edge_tts::cli
)

# ---------------------------------------------------------------------------
# CLI applications
# ---------------------------------------------------------------------------
if(EDGE_TTS_BUILD_APPS)
    add_executable(edge-tts apps/edge-tts/main.cpp)
    target_link_libraries(edge-tts PRIVATE edge_tts::cli edge_tts_compile_options)

    add_executable(edge-playback apps/edge-playback/main.cpp)
    target_link_libraries(edge-playback PRIVATE edge_tts::cli edge_tts_compile_options)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
option(EDGE_TTS_BUILD_EXAMPLES "Build example programs" OFF)
if(EDGE_TTS_BUILD_EXAMPLES)
    add_executable(edge_tts_save_file_example examples/save_file.cpp)
    target_link_libraries(edge_tts_save_file_example
        PRIVATE edge_tts::edge_tts edge_tts_compile_options)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(EDGE_TTS_BUILD_TESTS)
    include(EdgeTtsAddTest)
    enable_testing()
    add_subdirectory(tests)

    find_program(PYTHON3_EXECUTABLE NAMES python3 python)
    if(PYTHON3_EXECUTABLE)
        add_test(
            NAME edge_tts_docs_tests
            COMMAND "${PYTHON3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/docs/test_required_reference_docs.py"
        )
        add_test(
            NAME edge_tts_cli_compat_docs_tests
            COMMAND "${PYTHON3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/docs/test_cli_compatibility_doc.py"
        )
        add_test(
            NAME edge_tts_module_boundary_tests
            COMMAND "${PYTHON3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/tools/test_module_boundaries.py"
        )
    else()
        message(WARNING "python3 not found — Python-based tests will be skipped")
    endif()
endif()
