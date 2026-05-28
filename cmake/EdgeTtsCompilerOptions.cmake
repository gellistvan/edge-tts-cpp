# Creates the edge_tts_compile_options INTERFACE target that carries all
# project-wide compiler flags.  Every compiled target links it PRIVATE so
# consumers are never forced to inherit the same warning set.
#
# Call once from the root CMakeLists.txt:
#   include(EdgeTtsCompilerOptions)
#   edge_tts_configure_compile_options()

function(edge_tts_configure_compile_options)
    add_library(edge_tts_compile_options INTERFACE)

    if(MSVC)
        target_compile_options(edge_tts_compile_options INTERFACE /W4)
        if(EDGE_TTS_WARNINGS_AS_ERRORS)
            target_compile_options(edge_tts_compile_options INTERFACE /WX)
        endif()
    else()
        target_compile_options(edge_tts_compile_options INTERFACE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        )
        if(EDGE_TTS_WARNINGS_AS_ERRORS)
            target_compile_options(edge_tts_compile_options INTERFACE -Werror)
        endif()
    endif()

    if(EDGE_TTS_ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(edge_tts_compile_options INTERFACE
            -fsanitize=address,undefined
        )
        target_link_options(edge_tts_compile_options INTERFACE
            -fsanitize=address,undefined
        )
    endif()
endfunction()
