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
        src/core/TtsConfig.cpp
        src/core/OutputFormat.cpp
        src/core/Voice.cpp
    PUBLIC_DEPS edge_tts::common
)

edge_tts_add_module(
    NAME serialization
    SOURCES
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
        src/media/ExecutableDiscovery.cpp
        src/media/FfmpegAudioConverter.cpp
    PUBLIC_DEPS edge_tts::common
)

# ProcessRunner.cpp uses POSIX APIs (fork/execvp/pipe/waitpid).
# Only compile it on platforms where those are available.
if(NOT WIN32)
    target_sources(edge_tts_media PRIVATE src/media/ProcessRunner.cpp)
endif()

edge_tts_add_module(
    NAME communication
    SOURCES
        src/communication/ConnectionMetadata.cpp
        src/communication/EdgeProtocol.cpp
        src/communication/EdgeProtocolIncoming.cpp
        src/communication/HttpDate.cpp
        src/communication/SynthesisSession.cpp
        src/communication/HttpClient.cpp
        src/communication/VoiceService.cpp
        src/communication/EdgeRequestHeaders.cpp
        src/communication/EdgeServiceConfig.cpp
        src/communication/EdgeTokenProvider.cpp
        src/communication/WebSocketClient.cpp
        src/communication/RetryPolicy.cpp
    PUBLIC_DEPS
        edge_tts::core
        edge_tts::serialization
        edge_tts::common
)

edge_tts_add_module(
    NAME api
    SOURCES
        src/api/Communicate.cpp
        src/api/FileWriter.cpp
    PUBLIC_DEPS
        edge_tts::communication
        edge_tts::serialization
        edge_tts::subtitle
)

edge_tts_add_module(
    NAME cli
    SOURCES
        src/cli/EdgeTtsArgumentParser.cpp
        src/cli/EdgeTtsCommandDispatcher.cpp
        src/cli/InputLoader.cpp
        src/cli/PlaybackArgumentParser.cpp
        src/cli/PlaybackCommandDispatcher.cpp
        src/cli/VoiceFormatter.cpp
    PUBLIC_DEPS
        edge_tts::api
        edge_tts::media
    PRIVATE_DEPS
        edge_tts::subtitle
)

# ---------------------------------------------------------------------------
# Third-party dependency wiring.
# nlohmann/json is REQUIRED (Dependencies.cmake guarantees the target exists
# or aborts with FATAL_ERROR).  ixwebsocket is optional when
# EDGE_TTS_REQUIRE_NETWORKING=OFF (stub code compiles without it).
# ---------------------------------------------------------------------------

# nlohmann/json: unconditional — serialization sources always need it.
target_link_libraries(edge_tts_serialization PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(edge_tts_communication PUBLIC nlohmann_json::nlohmann_json)

# ixwebsocket: conditional — present only when real networking is available.
if(TARGET ixwebsocket)
    target_link_libraries(edge_tts_communication PRIVATE ixwebsocket)
    # Signal to HttpClient.cpp / WebSocketClient.cpp that ixwebsocket is available.
    # PRIVATE so the definition does not leak into downstream consumers.
    target_compile_definitions(edge_tts_communication PRIVATE EDGE_TTS_HAVE_IXWEBSOCKET=1)
endif()

# Guard: apps must not silently build against stub networking when real
# networking was explicitly required.
if((EDGE_TTS_BUILD_APPS OR EDGE_TTS_BUILD_PLAYBACK_APP) AND EDGE_TTS_REQUIRE_NETWORKING AND NOT TARGET ixwebsocket)
    message(FATAL_ERROR
        "EDGE_TTS_BUILD_APPS=ON or EDGE_TTS_BUILD_PLAYBACK_APP=ON and "
        "EDGE_TTS_REQUIRE_NETWORKING=ON but ixwebsocket target is not available. "
        "Apps cannot be built with stub networking.\n"
        "Fix: initialize the ixwebsocket submodule or set EDGE_TTS_FETCH_DEPS=ON."
    )
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
        edge_tts::api
        edge_tts::media
        edge_tts::subtitle
        edge_tts::cli
)

# ---------------------------------------------------------------------------
# CLI applications
# ---------------------------------------------------------------------------

# edge-tts: cross-platform (no POSIX process execution required).
if(EDGE_TTS_BUILD_APPS)
    add_executable(edge-tts apps/edge-tts/main.cpp)
    target_link_libraries(edge-tts PRIVATE edge_tts::cli edge_tts_compile_options)
endif()

# edge-playback: POSIX only (requires ProcessRunner → fork/execvp/pipe/waitpid).
# Defaults ON on Linux/macOS, OFF on Windows.
# Setting EDGE_TTS_BUILD_PLAYBACK_APP=ON on Windows is a fatal configure error.
if(EDGE_TTS_BUILD_PLAYBACK_APP)
    if(WIN32)
        message(FATAL_ERROR
            "EDGE_TTS_BUILD_PLAYBACK_APP=ON is not supported on Windows. "
            "ProcessRunner requires POSIX APIs (fork/execvp/pipe/waitpid) which "
            "are not available on Windows (platform: ${CMAKE_SYSTEM_NAME}).\n"
            "Options:\n"
            "  - Set EDGE_TTS_BUILD_PLAYBACK_APP=OFF to build the core library "
            "and edge-tts CLI without playback.\n"
            "  - Provide a Windows-specific IProcessRunner implementation and "
            "set EDGE_TTS_BUILD_PLAYBACK_APP=ON once it compiles."
        )
    endif()
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
        add_test(
            NAME edge_tts_dependency_config_tests
            COMMAND "${PYTHON3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/cmake/test_dependency_config.py"
        )
        add_test(
            NAME edge_tts_network_hygiene_tests
            COMMAND "${PYTHON3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/tools/test_network_hygiene.py"
        )
        add_test(
            NAME edge_tts_repository_hygiene_tests
            COMMAND "${PYTHON3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/tools/test_repository_hygiene.py"
        )
    else()
        message(WARNING "python3 not found — Python-based tests will be skipped")
    endif()
endif()
