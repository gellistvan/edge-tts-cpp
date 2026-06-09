# edge_tts_add_module — registers one library module.
#
# Usage:
#   edge_tts_add_module(
#     NAME        <name>              # produces edge_tts_<name> / edge_tts::<name>
#     [SOURCES    file ...]           # omit for header-only (INTERFACE) modules
#     [PUBLIC_DEPS  target ...]       # propagated to consumers
#     [PRIVATE_DEPS target ...]       # not propagated
#   )
#
# The function sets the public include root to <repo>/include and requires C++20.
# Compiled (non-INTERFACE) modules link edge_tts_compile_options PRIVATE so
# warning flags are not inherited by consumers.

function(edge_tts_add_module)
    cmake_parse_arguments(ARG "" "NAME" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "edge_tts_add_module: NAME is required")
    endif()

    set(target "edge_tts_${ARG_NAME}")

    if(ARG_SOURCES)
        # Always STATIC: edge-tts-cpp only supports static library builds.
        # BUILD_SHARED_LIBS is intentionally ignored for all edge_tts_* modules.
        # See docs/CONSUMING.md — "Linkage mode".
        add_library(${target} STATIC ${ARG_SOURCES})
        target_include_directories(${target}
            PUBLIC
                $<BUILD_INTERFACE:${EDGE_TTS_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>
        )
        target_compile_features(${target} PUBLIC cxx_std_20)
        # Apply warning/sanitizer flags directly via TARGET_PROPERTY genex rather
        # than target_link_libraries so edge_tts_compile_options does not appear
        # in LINK_LIBRARIES and need not be included in the install export set.
        target_compile_options(${target} PRIVATE
            $<TARGET_PROPERTY:edge_tts_compile_options,INTERFACE_COMPILE_OPTIONS>)
        target_link_options(${target} PRIVATE
            $<TARGET_PROPERTY:edge_tts_compile_options,INTERFACE_LINK_OPTIONS>)

        if(ARG_PUBLIC_DEPS)
            target_link_libraries(${target} PUBLIC ${ARG_PUBLIC_DEPS})
        endif()
        if(ARG_PRIVATE_DEPS)
            target_link_libraries(${target} PRIVATE ${ARG_PRIVATE_DEPS})
        endif()
    else()
        # Header-only module: everything is INTERFACE.
        add_library(${target} INTERFACE)
        target_include_directories(${target}
            INTERFACE
                $<BUILD_INTERFACE:${EDGE_TTS_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>
        )
        target_compile_features(${target} INTERFACE cxx_std_20)

        if(ARG_PUBLIC_DEPS)
            target_link_libraries(${target} INTERFACE ${ARG_PUBLIC_DEPS})
        endif()
        # PRIVATE_DEPS are meaningless for INTERFACE libraries; warn if provided.
        if(ARG_PRIVATE_DEPS)
            message(WARNING
                "edge_tts_add_module(${ARG_NAME}): PRIVATE_DEPS ignored for INTERFACE module")
        endif()
    endif()

    add_library("edge_tts::${ARG_NAME}" ALIAS ${target})
endfunction()
