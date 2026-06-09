cmake_minimum_required(VERSION 3.24)

# Example: consume edge-tts-cpp via add_subdirectory.
#
# This pattern is recommended when edge-tts-cpp is vendored as a git submodule
# inside your own repository.
#
# ── Typical project layout ────────────────────────────────────────────────
#
#   my_project/
#     CMakeLists.txt       ← your project (copy from this file)
#     src/
#       main.cpp           ← your application
#     vendor/
#       edge-tts-cpp/      ← git submodule: https://github.com/…/edge-tts-cpp
#
# ── Add the submodule ─────────────────────────────────────────────────────
#
#   git submodule add https://github.com/…/edge-tts-cpp vendor/edge-tts-cpp
#   git submodule update --init --recursive vendor/edge-tts-cpp
#
# ── Configure & build ─────────────────────────────────────────────────────
#
#   cmake -S my_project -B build
#   cmake --build build
#
# If the ixwebsocket submodule inside edge-tts-cpp is not populated, let
# CMake download it automatically:
#
#   cmake -S my_project -B build -DEDGE_TTS_FETCH_DEPS=ON
#
# ── Notes ─────────────────────────────────────────────────────────────────
#   • EDGE_TTS_INSTALL defaults OFF when consumed via add_subdirectory,
#     so no install() rules pollute your own install tree.
#   • Pass EXCLUDE_FROM_ALL so edge-tts-cpp targets are built only when your
#     app actually links them.
#   • Link only edge_tts::tts — it carries all transitive dependencies.
#     Do not manually list ixwebsocket, nlohmann_json, or internal sub-targets.

project(consumer_add_subdirectory_example LANGUAGES CXX)

# ── Locate edge-tts-cpp ───────────────────────────────────────────────────
# In a real project, replace EDGE_TTS_CPP_SOURCE_DIR with the path to your
# vendored copy, e.g. "${CMAKE_CURRENT_SOURCE_DIR}/vendor/edge-tts-cpp".
# The automated test sets this from the command line.
if(NOT DEFINED EDGE_TTS_CPP_SOURCE_DIR)
    message(FATAL_ERROR
        "Set -DEDGE_TTS_CPP_SOURCE_DIR=<path> to the edge-tts-cpp source root.\n"
        "In a real project, replace this with a hard-coded relative path such as:\n"
        "  \"\${CMAKE_CURRENT_SOURCE_DIR}/vendor/edge-tts-cpp\"")
endif()

# ── Build-option overrides (set before add_subdirectory) ─────────────────
# Suppress install rules, CLI apps, and unit tests from the sub-project so
# only the library itself is built.
set(EDGE_TTS_INSTALL     OFF CACHE BOOL "Disable install from sub-project" FORCE)
set(EDGE_TTS_BUILD_APPS  OFF CACHE BOOL "No CLI apps needed by this consumer" FORCE)
set(EDGE_TTS_BUILD_TESTS OFF CACHE BOOL "No unit tests needed by this consumer" FORCE)

add_subdirectory(
    "${EDGE_TTS_CPP_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/edge-tts-cpp"
    EXCLUDE_FROM_ALL
)

# ── Your application ──────────────────────────────────────────────────────
add_executable(tts_hello_world main.cpp)

# Link only the public entry-point target.  All transitive dependencies
# (static archives, ixwebsocket, cxx_std_20, include paths) are handled
# automatically — no need to list them here.
target_link_libraries(tts_hello_world PRIVATE edge_tts::tts)
