# EdgeTtsInstall.cmake — install rules for edge-tts-cpp.
#
# Included from the root CMakeLists.txt when EDGE_TTS_INSTALL=ON (the default
# when edge-tts-cpp is the top-level project).
#
# Installed layout (default GNUInstallDirs):
#
#   <prefix>/include/edge_tts/             public headers
#   <prefix>/lib/libedge_tts_common.a      compiled modules
#   <prefix>/lib/libedge_tts_core.a
#   <prefix>/lib/libedge_tts_serialization.a
#   <prefix>/lib/libedge_tts_subtitle.a
#   <prefix>/lib/libedge_tts_communication.a
#   <prefix>/lib/libedge_tts_api.a
#   <prefix>/lib/cmake/edge_tts_cpp/       CMake package files
#       edge_tts_cpp-config.cmake
#       edge_tts_cpp-config-version.cmake
#       edge_tts_cpp-targets.cmake
#   <prefix>/bin/                          CLI apps (EDGE_TTS_INSTALL_APPS=ON only)
#
# Consumer usage after installation:
#
#   find_package(edge_tts_cpp REQUIRED)
#   target_link_libraries(myapp PRIVATE edge_tts::tts)
#
# Note on ixwebsocket: the communication module is compiled with ixwebsocket
# when the submodule is present.  The resulting static archive references
# ixwebsocket symbols that consumers must satisfy at link time.  Consumers
# who build from the release archive or submodule automatically have ixwebsocket
# available.  Consumers who install from a pre-built package must either have
# ixwebsocket installed system-wide or link it explicitly.
# See docs/DEPENDENCIES.md for details.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Production library targets installed by default.
# These are exactly the targets required by edge_tts::tts.
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
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# Public headers.
# All fake/test-support headers live under tests/ — not under include/ — so
# this DIRECTORY install is safe: it never accidentally installs test doubles.
install(
    DIRECTORY "${EDGE_TTS_SOURCE_DIR}/include/edge_tts"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING
        PATTERN "*.hpp"
        PATTERN "*.h"
)

# ixwebsocket: install the static archive and headers alongside the edge_tts
# modules.  ixwebsocket's own CMakeLists.txt sets up the INSTALL_INTERFACE
# include path ($<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ixwebsocket>)
# correctly, but its PUBLIC_HEADER paths are relative to its own source root
# and would break when installed via the parent project's install script.
# Clear PUBLIC_HEADER before the install call and use install(DIRECTORY) instead.
if(TARGET ixwebsocket)
    # Suppress the "no PUBLIC_HEADER DESTINATION" warning by clearing the
    # property; the headers are installed explicitly via DIRECTORY below.
    set_target_properties(ixwebsocket PROPERTIES PUBLIC_HEADER "")

    install(TARGETS ixwebsocket
        EXPORT edge_tts_cpp_targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )

    # Install headers: installs the ixwebsocket/ directory into include/,
    # producing include/ixwebsocket/*.h — matching the INSTALL_INTERFACE path.
    install(
        DIRECTORY "${EDGE_TTS_SOURCE_DIR}/submodules/ixwebsocket/ixwebsocket"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        FILES_MATCHING PATTERN "*.h"
    )
endif()

# CLI apps (opt-in).
if(EDGE_TTS_INSTALL_APPS)
    if(TARGET edge-tts)
        install(TARGETS edge-tts RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
    if(TARGET edge-playback)
        install(TARGETS edge-playback RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
endif()

# CMake package destination.
set(_edge_tts_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/edge_tts_cpp")

# Export set: targets are exported WITHOUT a namespace prefix so that the
# imported names match the build-tree names (edge_tts_common, edge_tts_tts,
# etc.).  The config file creates edge_tts::<name> ALIAS targets on top of
# these so consumers can use the conventional edge_tts:: namespace.
install(EXPORT edge_tts_cpp_targets
    FILE edge_tts_cpp-targets.cmake
    DESTINATION "${_edge_tts_cmake_dir}"
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
)
