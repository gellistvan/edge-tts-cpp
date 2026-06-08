function(edge_tts_setup_static_analysis)
    if(NOT EDGE_TTS_ENABLE_CLANG_TIDY)
        return()
    endif()

    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(NOT CLANG_TIDY_EXE)
        message(WARNING "EDGE_TTS_ENABLE_CLANG_TIDY requested but clang-tidy not found.")
        return()
    endif()

    set(CMAKE_CXX_CLANG_TIDY
        "${CLANG_TIDY_EXE}"
        "--config-file=${EDGE_TTS_SOURCE_DIR}/.clang-tidy"
        PARENT_SCOPE
    )
    message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
endfunction()
