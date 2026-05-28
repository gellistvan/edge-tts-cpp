cmake_minimum_required(VERSION 3.24)

project(edge_tts_cpp
    VERSION 0.1.0
    DESCRIPTION "Modern C++ client for Microsoft Edge text-to-speech service"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ProjectOptions)
include(CompilerWarnings)
include(Dependencies)

edge_tts_setup_options()
edge_tts_setup_warnings(edge_tts_warnings)
edge_tts_setup_dependencies()

function(edge_tts_configure_module target)
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_link_libraries(${target} PRIVATE edge_tts_warnings)
endfunction()

add_library(edge_tts_common INTERFACE)
add_library(edge_tts::common ALIAS edge_tts_common)
target_include_directories(edge_tts_common
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(edge_tts_common INTERFACE cxx_std_20)

add_library(edge_tts_core
    src/core/TextChunker.cpp
    src/core/TtsConfig.cpp
)
add_library(edge_tts::core ALIAS edge_tts_core)
edge_tts_configure_module(edge_tts_core)
target_link_libraries(edge_tts_core PUBLIC edge_tts::common)

add_library(edge_tts_subtitles
    src/subtitles/SrtComposer.cpp
    src/subtitles/SubMaker.cpp
)
add_library(edge_tts::subtitles ALIAS edge_tts_subtitles)
edge_tts_configure_module(edge_tts_subtitles)
target_link_libraries(edge_tts_subtitles PUBLIC edge_tts::core)

add_library(edge_tts_serialization
    src/serialization/EdgeProtocol.cpp
    src/serialization/EdgeToken.cpp
)
add_library(edge_tts::serialization ALIAS edge_tts_serialization)
edge_tts_configure_module(edge_tts_serialization)
target_link_libraries(edge_tts_serialization PUBLIC edge_tts::core edge_tts::common)

add_library(edge_tts_media
    src/media/AudioConverter.cpp
)
add_library(edge_tts::media ALIAS edge_tts_media)
edge_tts_configure_module(edge_tts_media)
target_link_libraries(edge_tts_media PUBLIC edge_tts::common)

add_library(edge_tts_communication
    src/communication/Communicate.cpp
    src/communication/HttpVoiceService.cpp
    src/communication/VoicesManager.cpp
    src/communication/WebSocketTransport.cpp
)
add_library(edge_tts::communication ALIAS edge_tts_communication)
edge_tts_configure_module(edge_tts_communication)
target_link_libraries(edge_tts_communication
    PUBLIC
        edge_tts::core
        edge_tts::serialization
        edge_tts::subtitles
        edge_tts::media
        edge_tts::common
)

# Aggregate target for consumers that want the whole library surface.
add_library(edge_tts INTERFACE)
add_library(edge_tts::edge_tts ALIAS edge_tts)
target_link_libraries(edge_tts
    INTERFACE
        edge_tts::common
        edge_tts::core
        edge_tts::serialization
        edge_tts::communication
        edge_tts::media
        edge_tts::subtitles
)

# Dependency targets are created in cmake/Dependencies.cmake when submodules exist.
# Keep these optional while the skeleton is dependency-free/buildable.
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(edge_tts_serialization PUBLIC nlohmann_json::nlohmann_json)
    target_link_libraries(edge_tts_communication PUBLIC nlohmann_json::nlohmann_json)
endif()
if(TARGET ixwebsocket)
    target_link_libraries(edge_tts_communication PRIVATE ixwebsocket)
endif()

add_executable(edge-tts apps/edge-tts/main.cpp)
target_link_libraries(edge-tts PRIVATE edge_tts::edge_tts edge_tts_warnings)

add_executable(edge-playback apps/edge-playback/main.cpp)
target_link_libraries(edge-playback PRIVATE edge_tts::edge_tts edge_tts_warnings)

option(EDGE_TTS_BUILD_EXAMPLES "Build examples" ON)
if(EDGE_TTS_BUILD_EXAMPLES)
    add_executable(edge_tts_save_file_example examples/save_file.cpp)
    target_link_libraries(edge_tts_save_file_example PRIVATE edge_tts::edge_tts edge_tts_warnings)
endif()

option(EDGE_TTS_BUILD_TESTS "Build tests" ON)
if(EDGE_TTS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
