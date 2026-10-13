cmake_minimum_required(VERSION 3.25)
project(freshgram_transcription_checks LANGUAGES C CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Threads REQUIRED)
set(RECOGNITION_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Production SourceFiles directory")
add_library(transcription_under_test STATIC
    "${RECOGNITION_SOURCE_DIR}/ayu/features/recognition/transcription_config.cpp"
    "${RECOGNITION_SOURCE_DIR}/ayu/features/recognition/voice_asr_engine.cpp")
target_include_directories(transcription_under_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${RECOGNITION_SOURCE_DIR}"
    "${RECOGNITION_SOURCE_DIR}/../ThirdParty/voice-asr/include"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(transcription_under_test PRIVATE
    -Wall -Wextra
    -include "${CMAKE_CURRENT_SOURCE_DIR}/stubs/qt/string_literals.h")
target_link_libraries(transcription_under_test PRIVATE Qt6::Core)

add_executable(loader_tests loader_tests.cpp)
target_include_directories(loader_tests PRIVATE
    "${RECOGNITION_SOURCE_DIR}"
    "${RECOGNITION_SOURCE_DIR}/../ThirdParty/voice-asr/include")
target_link_libraries(loader_tests PRIVATE transcription_under_test Qt6::Core Threads::Threads)
foreach(flavor cloud local)
    add_library(legacy_${flavor} SHARED legacy_module.cpp)
    target_include_directories(legacy_${flavor} PRIVATE "${RECOGNITION_SOURCE_DIR}/../ThirdParty/voice-asr/include")
    if (flavor STREQUAL "local")
        target_compile_definitions(legacy_${flavor} PRIVATE LEGACY_CAPABILITIES=1)
    else()
        target_compile_definitions(legacy_${flavor} PRIVATE LEGACY_CAPABILITIES=0)
    endif()
    add_test(NAME transcription_legacy_${flavor}
        COMMAND loader_tests $<TARGET_FILE:legacy_${flavor}> unused legacy-${flavor})
endforeach()
set(VOICE_ASR_TEST_LIBRARY "" CACHE FILEPATH "Real cloud-only voice-asr library")
if (VOICE_ASR_TEST_LIBRARY)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME transcription_real_loader
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/loader_integration.py
            $<TARGET_FILE:loader_tests> ${VOICE_ASR_TEST_LIBRARY})
    set_tests_properties(transcription_real_loader PROPERTIES TIMEOUT 30)
endif()
