cmake_minimum_required(VERSION 3.24)

# This project verifies that edge_tts::tts is a usable consumer target:
#   1. edge_tts::tts is available after add_subdirectory.
#   2. A consumer can compile against it without including cli or playback targets.
#   3. CMAKE_SOURCE_DIR is not corrupted by edge-tts-cpp's add_subdirectory.

project(consumer_tts_target_project LANGUAGES CXX)

add_subdirectory(
    "${EDGE_TTS_CPP_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/edge-tts-cpp"
    EXCLUDE_FROM_ALL
)

add_executable(consumer_tts_app main.cpp)
target_compile_features(consumer_tts_app PRIVATE cxx_std_20)
target_link_libraries(consumer_tts_app PRIVATE edge_tts::tts)

# Verify edge_tts::tts does not directly expose edge_tts::cli.
get_target_property(_tts_iface edge_tts_tts INTERFACE_LINK_LIBRARIES)
if(_tts_iface MATCHES "edge_tts_cli|edge_tts::cli")
    message(FATAL_ERROR
        "edge_tts::tts must not expose edge_tts::cli in its INTERFACE_LINK_LIBRARIES.\n"
        "Actual INTERFACE_LINK_LIBRARIES: ${_tts_iface}"
    )
endif()

# Verify CMAKE_SOURCE_DIR was not corrupted.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR
        "CMAKE_SOURCE_DIR was corrupted by edge-tts-cpp's add_subdirectory.\n"
        "Expected: ${CMAKE_CURRENT_SOURCE_DIR}\n"
        "Got:      ${CMAKE_SOURCE_DIR}")
endif()
