cmake_minimum_required(VERSION 3.24)

# This project simulates a parent project consuming edge-tts-cpp via
# add_subdirectory().  The key invariant: after add_subdirectory, the consumer's
# CMAKE_SOURCE_DIR still points here (the parent), not inside edge-tts-cpp.

project(consumer_project LANGUAGES CXX)

add_subdirectory(
    "${EDGE_TTS_CPP_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/edge-tts-cpp"
    EXCLUDE_FROM_ALL
)

add_executable(consumer_app main.cpp)
target_compile_features(consumer_app PRIVATE cxx_std_20)
target_link_libraries(consumer_app PRIVATE
    edge_tts::common
    edge_tts::core
)

# The consumer's CMAKE_SOURCE_DIR must still be this file's directory.
# edge-tts-cpp must not override CMAKE_SOURCE_DIR.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR
        "CMAKE_SOURCE_DIR was corrupted by edge-tts-cpp's add_subdirectory.\n"
        "Expected: ${CMAKE_CURRENT_SOURCE_DIR}\n"
        "Got:      ${CMAKE_SOURCE_DIR}")
endif()
