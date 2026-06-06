# Dependency policy:
# - Third-party projects live in submodules/ and are added here with add_subdirectory.
# - Keep edge_tts core code independent from concrete libraries via interfaces/adapters.
# - Prefer small, focused dependencies over umbrella frameworks.
# - Dependency selection rationale and update/init instructions: docs/DEPENDENCIES.md
#
# Lookup order for each dependency:
#   1. submodules/<name>/CMakeLists.txt present  → add_subdirectory (preferred)
#   2. find_package(... CONFIG QUIET)            → system / vcpkg / conan install
#   3. EDGE_TTS_FETCH_DEPS=ON                   → FetchContent download
#   4. none of the above                        → FATAL_ERROR with actionable message

include(FetchContent)

function(edge_tts_setup_dependencies)
    # -------------------------------------------------------------------------
    # nlohmann/json — JSON parsing (REQUIRED: serialization sources use it)
    # -------------------------------------------------------------------------
    if(EXISTS "${CMAKE_SOURCE_DIR}/submodules/json/CMakeLists.txt")
        set(JSON_BuildTests OFF CACHE INTERNAL "")
        add_subdirectory("${CMAKE_SOURCE_DIR}/submodules/json" EXCLUDE_FROM_ALL)
        message(STATUS "nlohmann/json: using submodule at submodules/json")
    else()
        find_package(nlohmann_json CONFIG QUIET)
        if(nlohmann_json_FOUND)
            message(STATUS "nlohmann/json: using system/installed package (${nlohmann_json_VERSION})")
        elseif(EDGE_TTS_FETCH_DEPS)
            message(STATUS "nlohmann/json: submodule not present, fetching via FetchContent (tag v3.11.3)")
            FetchContent_Declare(
                nlohmann_json
                GIT_REPOSITORY https://github.com/nlohmann/json.git
                GIT_TAG        v3.11.3
                GIT_SHALLOW    TRUE
            )
            set(JSON_BuildTests OFF CACHE INTERNAL "")
            FetchContent_MakeAvailable(nlohmann_json)
        else()
            message(FATAL_ERROR
                "nlohmann/json not found. edge_tts_serialization requires it.\n"
                "Options to fix this:\n"
                "  1. Initialize the submodule:  git submodule update --init submodules/json\n"
                "  2. Install system package:    sudo apt install nlohmann-json3-dev\n"
                "  3. Enable auto-download:      cmake -DEDGE_TTS_FETCH_DEPS=ON ..."
            )
        endif()
    endif()

    # -------------------------------------------------------------------------
    # googletest — optional test runner
    # -------------------------------------------------------------------------
    if(EXISTS "${CMAKE_SOURCE_DIR}/submodules/googletest/CMakeLists.txt")
        add_subdirectory("${CMAKE_SOURCE_DIR}/submodules/googletest" EXCLUDE_FROM_ALL)
    endif()

    # ixwebsocket and other submodule dependencies
    include(EdgeTtsDependencies)
endfunction()
