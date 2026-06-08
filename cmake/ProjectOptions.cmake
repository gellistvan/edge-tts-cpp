function(edge_tts_setup_options)
    option(EDGE_TTS_BUILD_APPS      "Build the edge-tts CLI app" ON)
    option(EDGE_TTS_BUILD_TESTS     "Build the per-module test suites" ON)

    # EDGE_TTS_INSTALL — generate install() rules for headers and library targets.
    # Defaults ON when edge-tts-cpp is the top-level project so a standalone
    # build gets an install tree.  Defaults OFF when consumed via add_subdirectory
    # so the parent project's install tree is not polluted.
    # PROJECT_IS_TOP_LEVEL was added in CMake 3.21; our minimum is 3.24.
    option(EDGE_TTS_INSTALL
        "Generate cmake --install rules for public headers and library targets"
        "${PROJECT_IS_TOP_LEVEL}")

    # EDGE_TTS_INSTALL_APPS — include edge-tts and edge-playback CLI binaries
    # in the install tree.  Off by default; most library consumers do not want
    # the CLI apps installed alongside the library.
    option(EDGE_TTS_INSTALL_APPS
        "Install CLI app binaries (edge-tts, edge-playback) to CMAKE_INSTALL_BINDIR"
        OFF)

    # EDGE_TTS_INSTALL_TEST_SUPPORT — install test-support headers and targets
    # (FakeWebSocketClient, FakeHttpClient, FakeProcessRunner).  These are test
    # doubles; they must never be installed by default.
    option(EDGE_TTS_INSTALL_TEST_SUPPORT
        "Install test-support (Fake*) headers and targets — test-only; off by default"
        OFF)
    option(EDGE_TTS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
    option(EDGE_TTS_ENABLE_NETWORK_TESTS "Enable tests that call the live Edge TTS service" OFF)
    option(EDGE_TTS_ENABLE_SANITIZERS   "Enable address/UB sanitizers in supported compilers" OFF)
    option(EDGE_TTS_ENABLE_CLANG_TIDY   "Run clang-tidy on all compiled sources" OFF)

    # EDGE_TTS_BUILD_PLAYBACK_APP — build the edge-playback CLI app.
    # Requires POSIX (fork/execvp/pipe/waitpid via ProcessRunner).
    # Defaults ON on Linux/macOS, OFF on Windows.
    # Setting it ON on Windows is a configure-time FATAL_ERROR.
    if(NOT DEFINED EDGE_TTS_BUILD_PLAYBACK_APP)
        if(WIN32)
            set(EDGE_TTS_BUILD_PLAYBACK_APP OFF CACHE BOOL
                "Build the edge-playback CLI app (POSIX only; default OFF on Windows)" FORCE)
        else()
            set(EDGE_TTS_BUILD_PLAYBACK_APP ON CACHE BOOL
                "Build the edge-playback CLI app (POSIX only; default OFF on Windows)" FORCE)
        endif()
    else()
        option(EDGE_TTS_BUILD_PLAYBACK_APP
            "Build the edge-playback CLI app (POSIX only; default OFF on Windows)"
            ON)
    endif()

    # EDGE_TTS_FETCH_DEPS — when ON, CMake may use FetchContent to download
    # dependencies that are neither present as submodules nor installed system-wide.
    # Default OFF so a missing dependency fails at configure time with a clear,
    # actionable message rather than a confusing git/network error.
    # Set ON for the "developer" preset or any environment with reliable internet.
    option(EDGE_TTS_FETCH_DEPS
        "Allow FetchContent to download missing dependencies automatically" OFF)

    # EDGE_TTS_REQUIRE_NETWORKING — when ON, configuring without ixwebsocket is a
    # fatal error.  Default: ON when any app that needs networking is enabled
    # (EDGE_TTS_BUILD_APPS=ON or EDGE_TTS_BUILD_PLAYBACK_APP=ON), OFF otherwise
    # (stub HttpClient/WebSocketClient compile fine for library-only builds).
    if(NOT DEFINED EDGE_TTS_REQUIRE_NETWORKING)
        if(EDGE_TTS_BUILD_APPS OR EDGE_TTS_BUILD_PLAYBACK_APP)
            set(EDGE_TTS_REQUIRE_NETWORKING ON CACHE BOOL
                "Require ixwebsocket for real networking (fatal if missing)" FORCE)
        else()
            set(EDGE_TTS_REQUIRE_NETWORKING OFF CACHE BOOL
                "Require ixwebsocket for real networking (fatal if missing)" FORCE)
        endif()
    else()
        option(EDGE_TTS_REQUIRE_NETWORKING
            "Require ixwebsocket for real networking (fatal if missing)"
            "${EDGE_TTS_REQUIRE_NETWORKING}")
    endif()
endfunction()
