# Dependency policy:
# - Third-party projects live in submodules/ and are added here with add_subdirectory.
# - Keep edge_tts core code independent from concrete libraries via interfaces/adapters.
# - Prefer small, focused dependencies over umbrella frameworks.
# - Dependency selection rationale and update/init instructions: docs/DEPENDENCIES.md

function(edge_tts_setup_dependencies)
    # nlohmann/json — JSON parsing
    if(EXISTS "${CMAKE_SOURCE_DIR}/submodules/json/CMakeLists.txt")
        set(JSON_BuildTests OFF CACHE INTERNAL "")
        add_subdirectory("${CMAKE_SOURCE_DIR}/submodules/json" EXCLUDE_FROM_ALL)
    endif()

    # googletest — optional test runner
    if(EXISTS "${CMAKE_SOURCE_DIR}/submodules/googletest/CMakeLists.txt")
        add_subdirectory("${CMAKE_SOURCE_DIR}/submodules/googletest" EXCLUDE_FROM_ALL)
    endif()

    # ixwebsocket and other submodule dependencies
    include(EdgeTtsDependencies)
endfunction()
