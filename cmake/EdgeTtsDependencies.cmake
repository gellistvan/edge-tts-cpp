# EdgeTtsDependencies.cmake
#
# Registers third-party submodule dependencies for edge-tts-cpp.
# Included by cmake/Dependencies.cmake inside edge_tts_setup_dependencies().
#
# Each dependency:
#   1. Guards on EXISTS to allow building without the submodule checked out.
#   2. Disables the dependency's own install/test/example targets.
#   3. Uses EXCLUDE_FROM_ALL so the dependency is only built when consumed.
#   4. Promotes the dependency's include directories to SYSTEM so our aggressive
#      warning flags (-Wall -Wextra -Wpedantic …) are silenced on those headers.
#
# Suppression mechanism:
#   CMake propagates INTERFACE_SYSTEM_INCLUDE_DIRECTORIES with -isystem (GCC/Clang)
#   or /external:I (MSVC /experimental:external).  Setting this property on the
#   third-party target is sufficient — consuming targets inherit the SYSTEM status
#   automatically and never need an explicit target_include_directories(SYSTEM …).
#
# ---------------------------------------------------------------------------
# ixwebsocket — WebSocket client + HTTP client
# ---------------------------------------------------------------------------
#
# Why ixwebsocket:
#   • The project already planned this submodule (see submodules/README.md and
#     the Planned table in DEPENDENCIES.md).
#   • A single submodule covers BOTH needs:
#       communication::WebSocketTransport (IWebSocketClient implementation)
#       communication::IHttpClient implementation for the voice-list endpoint
#   • Header-only–style C++ library; no system installation required.
#   • Aligns with the reference Python implementation: the edge-tts package uses
#     aiohttp for both HTTP and WebSocket, so one library covering both is natural.
#   • MIT licensed, compatible with the project.
#
# Submodule: submodules/ixwebsocket
# URL: https://github.com/machinezone/IXWebSocket
# CMake target produced: ixwebsocket
#
# Lookup order:
#   1. submodules/ixwebsocket/CMakeLists.txt present → add_subdirectory
#   2. find_package(ixwebsocket CONFIG QUIET)        → system/vcpkg install
#   3. EDGE_TTS_FETCH_DEPS=ON                        → FetchContent
#   4. Not found + EDGE_TTS_REQUIRE_NETWORKING=ON    → FATAL_ERROR
#   5. Not found + EDGE_TTS_REQUIRE_NETWORKING=OFF   → silently skip (stubs compile)

# --- TLS: enable by default on non-Windows --------------------------------
# The Edge TTS voices endpoint (https://) and WebSocket endpoint (wss://)
# both require TLS.  ixwebsocket uses the variable name USE_TLS (not
# IXWEBSOCKET_USE_TLS).  Default ON so production builds have HTTPS;
# the caller can set USE_TLS=OFF before including this file to opt out.
if(NOT DEFINED USE_TLS)
    set(USE_TLS ON CACHE BOOL "Enable TLS in ixwebsocket" FORCE)
endif()

# --- Shared ixwebsocket CMake settings (applied before add_subdirectory) --
macro(_edge_tts_configure_ixwebsocket_options)
    set(IXWEBSOCKET_INSTALL OFF CACHE INTERNAL "Disable ixwebsocket install")
    # Disable googletest inside ixwebsocket so it does not conflict with ours.
    set(BUILD_TESTING OFF CACHE BOOL "Disable ixwebsocket test targets" FORCE)
endmacro()

# --- Suppress warnings on ixwebsocket headers -----------------------------
macro(_edge_tts_suppress_ixwebsocket_warnings)
    if(TARGET ixwebsocket)
        get_target_property(_ix_includes ixwebsocket INTERFACE_INCLUDE_DIRECTORIES)
        if(_ix_includes)
            set_target_properties(ixwebsocket PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_ix_includes}")
        endif()
        unset(_ix_includes)
    endif()
endmacro()

if(EXISTS "${EDGE_TTS_SOURCE_DIR}/submodules/ixwebsocket/CMakeLists.txt")

    _edge_tts_configure_ixwebsocket_options()

    add_subdirectory(
        "${EDGE_TTS_SOURCE_DIR}/submodules/ixwebsocket"
        "${EDGE_TTS_BINARY_DIR}/_deps/ixwebsocket"
        EXCLUDE_FROM_ALL)

    _edge_tts_suppress_ixwebsocket_warnings()

    message(STATUS "ixwebsocket: using submodule at submodules/ixwebsocket")

else()

    find_package(ixwebsocket CONFIG QUIET)
    if(ixwebsocket_FOUND AND TARGET ixwebsocket)
        message(STATUS "ixwebsocket: using system/installed package")
        _edge_tts_suppress_ixwebsocket_warnings()
    elseif(EDGE_TTS_FETCH_DEPS)
        message(STATUS "ixwebsocket: submodule not present, fetching via FetchContent (tag v11.4.5)")
        include(FetchContent)
        _edge_tts_configure_ixwebsocket_options()
        FetchContent_Declare(
            ixwebsocket
            GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
            GIT_TAG        v11.4.5
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(ixwebsocket)
        _edge_tts_suppress_ixwebsocket_warnings()
    elseif(EDGE_TTS_REQUIRE_NETWORKING)
        message(FATAL_ERROR
            "ixwebsocket not found and EDGE_TTS_REQUIRE_NETWORKING=ON.\n"
            "Options to fix this:\n"
            "  1. Initialize the submodule:  git submodule update --init submodules/ixwebsocket\n"
            "  2. Install system package:    see https://github.com/machinezone/IXWebSocket\n"
            "  3. Enable auto-download:      cmake -DEDGE_TTS_FETCH_DEPS=ON ...\n"
            "  4. Build without apps/networking: cmake -DEDGE_TTS_BUILD_APPS=OFF ..."
        )
    else()
        message(STATUS
            "ixwebsocket: not found and EDGE_TTS_REQUIRE_NETWORKING=OFF — "
            "stub HttpClient/WebSocketClient will be used (no real networking)")
    endif()

endif()
