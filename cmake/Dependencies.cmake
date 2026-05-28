# Dependency policy:
# - Third-party projects live in submodules/ and are added here with add_subdirectory.
# - Keep edge_tts core code independent from concrete libraries via interfaces/adapters.
# - Prefer small, focused dependencies over umbrella frameworks.

function(edge_tts_setup_dependencies)
    # Expected future submodules:
    #   submodules/CLI11
    #   submodules/json
    #   submodules/ixwebsocket
    #   submodules/googletest
    #
    # Example:
    # if(EXISTS "${CMAKE_SOURCE_DIR}/submodules/json/CMakeLists.txt")
    #     add_subdirectory("${CMAKE_SOURCE_DIR}/submodules/json" EXCLUDE_FROM_ALL)
    # endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/submodules/googletest/CMakeLists.txt")
        add_subdirectory("${CMAKE_SOURCE_DIR}/submodules/googletest" EXCLUDE_FROM_ALL)
    endif()
endfunction()
