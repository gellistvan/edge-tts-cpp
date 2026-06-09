cmake_minimum_required(VERSION 3.24)

# Fixture: consumer_strict_warnings
#
# Verifies two independent properties of the edge_tts::tts public target:
#
#   1. No warning-flag leakage — the consumer's own strict -Wall/-Wextra/-Werror
#      flags must not trigger errors caused by edge-tts-cpp's internal includes
#      (e.g. ixwebsocket headers leaking as non-SYSTEM includes) or compile
#      definitions.
#
#   2. No manual internal-dep linking — the consumer links ONLY edge_tts::tts.
#      It does NOT list nlohmann_json, ixwebsocket, or any edge_tts_* sub-target
#      explicitly.  The build must succeed regardless.
#
# This fixture supports two integration modes, selected by cache variables:
#
#   add_subdirectory mode (default):
#     cmake -DEDGE_TTS_CPP_SOURCE_DIR=<repo_root> ...
#
#   find_package mode:
#     cmake -DCMAKE_PREFIX_PATH=<install_prefix> ...
#     (EDGE_TTS_CPP_SOURCE_DIR must NOT be set)

project(consumer_strict_warnings LANGUAGES CXX)

if(DEFINED EDGE_TTS_CPP_SOURCE_DIR)
    # add_subdirectory mode: consume edge-tts-cpp directly from source.
    add_subdirectory(
        "${EDGE_TTS_CPP_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/edge-tts-cpp"
        EXCLUDE_FROM_ALL
    )
else()
    # find_package mode: consume from an installed prefix.
    find_package(edge_tts_cpp REQUIRED)
endif()

add_executable(consumer_strict_app main.cpp)

# Link ONLY the public entry-point target.  No nlohmann_json, no ixwebsocket,
# no internal edge_tts_* sub-targets.
target_link_libraries(consumer_strict_app PRIVATE edge_tts::tts)

# Apply strict warning flags to the consumer's OWN target.
# If edge-tts-cpp leaks any of its internal warning-as-error flags, or if
# ixwebsocket headers are not properly marked as SYSTEM, this target would
# fail to compile.
if(MSVC)
    target_compile_options(consumer_strict_app PRIVATE /W4 /WX)
else()
    target_compile_options(consumer_strict_app PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()
