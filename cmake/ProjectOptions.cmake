function(edge_tts_setup_options)
    option(EDGE_TTS_BUILD_APPS      "Build the edge-tts and edge-playback CLI apps" ON)
    option(EDGE_TTS_BUILD_TESTS     "Build the per-module test suites" ON)
    option(EDGE_TTS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
    option(EDGE_TTS_ENABLE_NETWORK_TESTS "Enable tests that call the live Edge TTS service" OFF)
    option(EDGE_TTS_ENABLE_SANITIZERS   "Enable address/UB sanitizers in supported compilers" OFF)
    option(EDGE_TTS_ENABLE_CLANG_TIDY   "Run clang-tidy on all compiled sources" OFF)

    # EDGE_TTS_FETCH_DEPS — when ON, CMake may use FetchContent to download
    # dependencies that are neither present as submodules nor installed system-wide.
    # Useful for source archives / CI environments without network-accessible
    # submodules.  Default ON so clean checkouts work out of the box.
    option(EDGE_TTS_FETCH_DEPS
        "Allow FetchContent to download missing dependencies automatically" ON)

    # EDGE_TTS_REQUIRE_NETWORKING — when ON, configuring without ixwebsocket is a
    # fatal error.  Default: ON when EDGE_TTS_BUILD_APPS=ON (apps need real
    # networking), OFF otherwise (stub HttpClient/WebSocketClient compile fine).
    if(NOT DEFINED EDGE_TTS_REQUIRE_NETWORKING)
        if(EDGE_TTS_BUILD_APPS)
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
