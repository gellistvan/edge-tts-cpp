# EdgeTtsInstall.cmake — install rules for edge-tts-cpp.
#
# Included from the root CMakeLists.txt when EDGE_TTS_INSTALL=ON (the default
# when edge-tts-cpp is the top-level project).
#
# ── Install components ────────────────────────────────────────────────────
#
#   Development   — static archives, public headers, CMake package config files.
#                   Controlled by EDGE_TTS_INSTALL_LIBRARY (default ON).
#                   Install standalone: cmake --install <build> --component Development
#
#   Apps          — edge-tts and edge-playback CLI binaries.
#                   Controlled by EDGE_TTS_INSTALL_APPS (default OFF).
#                   edge-playback is only present when EDGE_TTS_BUILD_PLAYBACK_APP=ON.
#                   Install standalone: cmake --install <build> --component Apps
#
#   TestSupport   — Fake* test-double headers.
#                   Controlled by EDGE_TTS_INSTALL_TEST_SUPPORT (default OFF).
#                   Never installed unless explicitly requested.
#
# ── Installed layout (default GNUInstallDirs) ─────────────────────────────
#
#   <prefix>/include/edge_tts/             public headers        [Development]
#   <prefix>/include/ixwebsocket/          ixwebsocket headers   [Development]
#   <prefix>/lib/libedge_tts_common.a      compiled modules      [Development]
#   <prefix>/lib/libedge_tts_core.a
#   <prefix>/lib/libedge_tts_serialization.a
#   <prefix>/lib/libedge_tts_subtitle.a
#   <prefix>/lib/libedge_tts_communication.a
#   <prefix>/lib/libedge_tts_api.a
#   <prefix>/lib/libixwebsocket.a          (when compiled)       [Development]
#   <prefix>/lib/cmake/edge_tts_cpp/       CMake package files   [Development]
#       edge_tts_cpp-config.cmake
#       edge_tts_cpp-config-version.cmake
#       edge_tts_cpp-targets.cmake
#   <prefix>/bin/edge-tts                  CLI app               [Apps]
#   <prefix>/bin/edge-playback             CLI app (POSIX only)  [Apps]
#
# ── Consumer usage after installation ────────────────────────────────────
#
#   find_package(edge_tts_cpp CONFIG REQUIRED)
#   target_link_libraries(myapp PRIVATE edge_tts::tts)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# CMake package destination — used by both library and config file installs.
set(_edge_tts_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/edge_tts_cpp")

# ── Development component (library + headers + CMake package files) ───────

if(EDGE_TTS_INSTALL_LIBRARY)

    # Production library targets required by edge_tts::tts.
    set(_EDGE_TTS_INSTALL_TARGETS
        edge_tts_common
        edge_tts_core
        edge_tts_serialization
        edge_tts_subtitle
        edge_tts_communication
        edge_tts_api
        edge_tts_tts
    )

    install(TARGETS ${_EDGE_TTS_INSTALL_TARGETS}
        EXPORT edge_tts_cpp_targets
        ARCHIVE
            DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            COMPONENT   Development
        LIBRARY
            DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            COMPONENT   Development
        RUNTIME
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT   Development
        INCLUDES
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    # Public headers from the source tree.
    # All fake/test-support headers live under tests/ — not under include/ — so
    # this DIRECTORY install is safe: it never accidentally installs test doubles.
    install(
        DIRECTORY "${EDGE_TTS_SOURCE_DIR}/include/edge_tts"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        COMPONENT   Development
        FILES_MATCHING
            PATTERN "*.hpp"
            PATTERN "*.h"
    )

    # version.hpp is generated into the binary tree and is not in the source
    # include/ directory, so it requires a separate FILES install.
    install(FILES
        "${EDGE_TTS_BINARY_DIR}/include/edge_tts/version.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/edge_tts"
        COMPONENT   Development
    )

    # ixwebsocket: install the static archive and headers alongside edge_tts.
    # ixwebsocket's own CMakeLists.txt sets up INSTALL_INTERFACE include paths
    # correctly, but its PUBLIC_HEADER paths are relative to its own source root
    # and break when installed via the parent project.  Clear PUBLIC_HEADER and
    # use install(DIRECTORY) instead to produce include/ixwebsocket/*.h.
    if(TARGET ixwebsocket)
        set_target_properties(ixwebsocket PROPERTIES PUBLIC_HEADER "")

        install(TARGETS ixwebsocket
            EXPORT  edge_tts_cpp_targets
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Development
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Development
        )

        install(
            DIRECTORY "${EDGE_TTS_SOURCE_DIR}/submodules/ixwebsocket/ixwebsocket"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
            COMPONENT   Development
            FILES_MATCHING PATTERN "*.h"
        )
    endif()

    # Export set: targets exported WITHOUT a namespace prefix so imported names
    # match the build-tree names (edge_tts_common, etc.).  The config file
    # creates edge_tts::<name> ALIAS targets so consumers use the conventional
    # namespace form.
    install(EXPORT edge_tts_cpp_targets
        FILE        edge_tts_cpp-targets.cmake
        DESTINATION "${_edge_tts_cmake_dir}"
        COMPONENT   Development
    )

    configure_package_config_file(
        "${EDGE_TTS_SOURCE_DIR}/cmake/edge_tts_cpp-config.cmake.in"
        "${EDGE_TTS_BINARY_DIR}/edge_tts_cpp-config.cmake"
        INSTALL_DESTINATION "${_edge_tts_cmake_dir}"
    )

    write_basic_package_version_file(
        "${EDGE_TTS_BINARY_DIR}/edge_tts_cpp-config-version.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        "${EDGE_TTS_BINARY_DIR}/edge_tts_cpp-config.cmake"
        "${EDGE_TTS_BINARY_DIR}/edge_tts_cpp-config-version.cmake"
        DESTINATION "${_edge_tts_cmake_dir}"
        COMPONENT   Development
    )

endif() # EDGE_TTS_INSTALL_LIBRARY

# ── Apps component (CLI binaries) ─────────────────────────────────────────
#
# Neither edge-tts nor edge-playback is added to the export set — app binaries
# are standalone install artifacts, not CMake-importable library targets.
# The installed CMake package config never exposes app targets to consumers.
#
# edge-playback is skipped when EDGE_TTS_BUILD_PLAYBACK_APP is OFF (Windows or
# an explicit opt-out) because the target is never created in that case.

if(EDGE_TTS_INSTALL_APPS)
    if(TARGET edge-tts)
        install(TARGETS edge-tts
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT Apps
        )
    endif()

    # edge-playback requires POSIX; it is only built when
    # EDGE_TTS_BUILD_PLAYBACK_APP=ON and the target actually exists.
    if(TARGET edge-playback AND EDGE_TTS_BUILD_PLAYBACK_APP)
        install(TARGETS edge-playback
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT Apps
        )
    endif()
endif() # EDGE_TTS_INSTALL_APPS
