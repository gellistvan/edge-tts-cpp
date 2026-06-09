# edge_tts_add_module_test — registers one test executable.
#
# Usage:
#   edge_tts_add_module_test(
#     NAME       <name>          # produces edge_tts_<name>_tests
#     SOURCES    file ...        # test sources (include common/test_main.cpp)
#     [LIBRARIES target ...]     # module(s) under test
#   )
#
# The vendor/minigtest include path is added automatically.
# edge_tts_compile_options is linked PRIVATE so tests see the same warning set.

function(edge_tts_add_module_test)
    cmake_parse_arguments(ARG "" "NAME" "SOURCES;LIBRARIES" ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "edge_tts_add_module_test: NAME is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "edge_tts_add_module_test: SOURCES is required")
    endif()

    set(target "edge_tts_${ARG_NAME}_tests")

    add_executable(${target} ${ARG_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_link_libraries(${target} PRIVATE
        ${ARG_LIBRARIES}
        edge_tts_compile_options
    )
    # Expose tests/vendor/minigtest and tests/ for #include "vendor/minigtest/..."
    target_include_directories(${target} PRIVATE
        "${EDGE_TTS_SOURCE_DIR}/tests"
    )
    add_test(NAME ${target} COMMAND ${target})
endfunction()
