set(MINIGTEST_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

function(edge_tts_add_module_test target module_target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE ${module_target} edge_tts_warnings)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE ${MINIGTEST_INCLUDE_DIR})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

edge_tts_add_module_test(edge_tts_common_tests edge_tts::common
    common/test_main.cpp
)

edge_tts_add_module_test(edge_tts_core_tests edge_tts::core
    common/test_main.cpp
    core/test_tts_config.cpp
    core/test_voice.cpp
    core/test_text_chunker.cpp
)

edge_tts_add_module_test(edge_tts_subtitles_tests edge_tts::subtitles
    common/test_main.cpp
    subtitles/test_srt_composer.cpp
)

edge_tts_add_module_test(edge_tts_serialization_tests edge_tts::serialization
    common/test_main.cpp
)

edge_tts_add_module_test(edge_tts_communication_tests edge_tts::communication
    common/test_main.cpp
)

edge_tts_add_module_test(edge_tts_media_tests edge_tts::media
    common/test_main.cpp
)
