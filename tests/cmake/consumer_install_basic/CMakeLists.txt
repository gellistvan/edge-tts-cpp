cmake_minimum_required(VERSION 3.24)

# Verifies that edge-tts-cpp's installed CMake package is discoverable and
# provides the expected targets via find_package().
#
# This project is configured against an install prefix passed via:
#   cmake -DCMAKE_PREFIX_PATH=<install_prefix> ...
#
# Checks performed at configure time:
#   1. find_package(edge_tts_cpp REQUIRED) succeeds.
#   2. edge_tts::tts INTERFACE target exists.
#   3. edge_tts::api, edge_tts::communication, edge_tts::common targets exist.
#   4. edge_tts::tts does not directly expose edge_tts::cli in its link interface.

project(consumer_install_project LANGUAGES CXX)

find_package(edge_tts_cpp REQUIRED)

# 2. Verify the recommended consumer target is importable.
if(NOT TARGET edge_tts::tts)
    message(FATAL_ERROR
        "find_package(edge_tts_cpp) succeeded but edge_tts::tts target is missing.")
endif()

# 3. Verify core module targets are importable.
foreach(_tgt edge_tts::api edge_tts::communication edge_tts::core edge_tts::common)
    if(NOT TARGET "${_tgt}")
        message(FATAL_ERROR
            "find_package(edge_tts_cpp) succeeded but target ${_tgt} is missing.")
    endif()
endforeach()

# 4. edge_tts::tts must not directly link edge_tts::cli.
get_target_property(_tts_iface edge_tts_tts INTERFACE_LINK_LIBRARIES)
if(_tts_iface MATCHES "edge_tts_cli|edge_tts::cli")
    message(FATAL_ERROR
        "Installed edge_tts::tts must not expose edge_tts::cli.\n"
        "Actual INTERFACE_LINK_LIBRARIES: ${_tts_iface}")
endif()

add_executable(consumer_install_app main.cpp)
target_compile_features(consumer_install_app PRIVATE cxx_std_20)
target_link_libraries(consumer_install_app PRIVATE edge_tts::tts)
