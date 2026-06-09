cmake_minimum_required(VERSION 3.24)

# Example: consume edge-tts-cpp via find_package after cmake --install.
#
# This pattern is recommended when edge-tts-cpp is installed system-wide or
# distributed via a package manager (vcpkg, Conan, Homebrew, apt, etc.) or
# when using pre-built CI artifacts.
#
# ── Install edge-tts-cpp first ────────────────────────────────────────────
#
#   cmake -S path/to/edge-tts-cpp -B build_edge_tts \
#       -DEDGE_TTS_INSTALL=ON \
#       -DCMAKE_INSTALL_PREFIX=/usr/local
#   cmake --build build_edge_tts
#   cmake --install build_edge_tts
#
#   EDGE_TTS_INSTALL is ON by default when edge-tts-cpp is the top-level project.
#
# ── Configure & build your project ───────────────────────────────────────
#
#   cmake -S my_project -B build \
#       -DCMAKE_PREFIX_PATH=/usr/local
#   cmake --build build
#
# If installed to a standard prefix (e.g. /usr/local) that is already in
# CMake's default search path, -DCMAKE_PREFIX_PATH is optional.
#
# ── Notes ─────────────────────────────────────────────────────────────────
#   • Use CONFIG mode (find_package … CONFIG) to avoid FindModule fallback.
#   • Link only edge_tts::tts — it carries all transitive dependencies.
#   • ixwebsocket is installed alongside edge-tts-cpp; no separate package
#     needed when using the official release archive.

project(consumer_find_package_example LANGUAGES CXX)

# ── Locate the installed package ──────────────────────────────────────────
# Point CMake at the install prefix with -DCMAKE_PREFIX_PATH=<prefix>
# or by installing to a default search path (e.g. /usr/local).
find_package(edge_tts_cpp CONFIG REQUIRED)

# ── Your application ──────────────────────────────────────────────────────
add_executable(tts_hello_world main.cpp)

# Link only the public entry-point target.  All transitive dependencies
# (static archives, ixwebsocket, cxx_std_20, include paths) are handled
# automatically — no need to list them here.
target_link_libraries(tts_hello_world PRIVATE edge_tts::tts)
