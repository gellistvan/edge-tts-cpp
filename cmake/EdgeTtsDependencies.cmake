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

if(EXISTS "${CMAKE_SOURCE_DIR}/submodules/ixwebsocket/CMakeLists.txt")

    # --- Disable everything except the library itself -----------------------
    set(IXWEBSOCKET_INSTALL       OFF CACHE INTERNAL "Disable ixwebsocket install")
    set(IXWEBSOCKET_USE_TLS       OFF CACHE INTERNAL "TLS controlled separately")
    # Do not override USE_TLS/USE_OPEN_SSL/USE_MBEDTLS here; let the user set
    # them before including this file if TLS support is needed.
    # Disable googletest inside ixwebsocket so it does not conflict with ours.
    set(BUILD_TESTING OFF CACHE BOOL "Disable ixwebsocket test targets" FORCE)

    add_subdirectory(
        "${CMAKE_SOURCE_DIR}/submodules/ixwebsocket"
        "${CMAKE_BINARY_DIR}/_deps/ixwebsocket"
        EXCLUDE_FROM_ALL)

    # --- Suppress warnings on ixwebsocket headers ---------------------------
    # After add_subdirectory() the target exists; read its INTERFACE include
    # directories and re-register them as SYSTEM so downstream consumers
    # compile those headers without our project warnings.
    if(TARGET ixwebsocket)
        get_target_property(_ix_includes ixwebsocket INTERFACE_INCLUDE_DIRECTORIES)
        if(_ix_includes)
            set_target_properties(ixwebsocket PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_ix_includes}")
        endif()
        unset(_ix_includes)
    endif()

endif()
