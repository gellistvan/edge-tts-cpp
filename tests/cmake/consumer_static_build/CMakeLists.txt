cmake_minimum_required(VERSION 3.24)

# Verifies that edge-tts-cpp only produces static libraries and that a consumer
# links correctly against them.
#
# Accepts either:
#   -DEDGE_TTS_CPP_SOURCE_DIR=<path>  (add_subdirectory mode)
#   -DCMAKE_PREFIX_PATH=<prefix>      (find_package mode, set automatically by the test)
#
# The EDGE_TTS_LINKAGE_MODE cache variable selects the integration mode:
#   "add_subdirectory"  (default)
#   "find_package"

project(consumer_static_build_project LANGUAGES CXX)

set(EDGE_TTS_LINKAGE_MODE "add_subdirectory" CACHE STRING
    "Integration mode: add_subdirectory or find_package")

if(EDGE_TTS_LINKAGE_MODE STREQUAL "add_subdirectory")
    if(NOT DEFINED EDGE_TTS_CPP_SOURCE_DIR)
        message(FATAL_ERROR
            "EDGE_TTS_CPP_SOURCE_DIR must be set when EDGE_TTS_LINKAGE_MODE=add_subdirectory")
    endif()
    add_subdirectory(
        "${EDGE_TTS_CPP_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/edge-tts-cpp"
        EXCLUDE_FROM_ALL
    )
elseif(EDGE_TTS_LINKAGE_MODE STREQUAL "find_package")
    find_package(edge_tts_cpp REQUIRED)
else()
    message(FATAL_ERROR "Unknown EDGE_TTS_LINKAGE_MODE: ${EDGE_TTS_LINKAGE_MODE}")
endif()

# Verify edge_tts::tts is available.
if(NOT TARGET edge_tts::tts)
    message(FATAL_ERROR "edge_tts::tts target not found after integration.")
endif()

# In add_subdirectory mode: verify all compiled modules have STATIC type.
if(EDGE_TTS_LINKAGE_MODE STREQUAL "add_subdirectory")
    foreach(_mod common core serialization subtitle communication api)
        set(_tgt "edge_tts_${_mod}")
        if(TARGET "${_tgt}")
            get_target_property(_type "${_tgt}" TYPE)
            if(NOT _type STREQUAL "STATIC_LIBRARY")
                message(FATAL_ERROR
                    "edge_tts_${_mod} has type '${_type}' but must be STATIC_LIBRARY.\n"
                    "edge-tts-cpp only supports static library builds.\n"
                    "BUILD_SHARED_LIBS must not affect edge_tts_* module types.")
            endif()
        endif()
    endforeach()
    message(STATUS "All edge_tts_* compiled modules confirmed as STATIC_LIBRARY")
endif()

add_executable(consumer_static_app main.cpp)
target_compile_features(consumer_static_app PRIVATE cxx_std_20)
target_link_libraries(consumer_static_app PRIVATE edge_tts::tts)
