cmake_minimum_required(VERSION 3.25)
project(freshgram_transcription_checks LANGUAGES C CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Threads REQUIRED)
set(RECOGNITION_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Production SourceFiles directory")
add_library(transcription_under_test STATIC
    "${RECOGNITION_SOURCE_DIR}/ayu/features/recognition/cloud_transcribe.cpp")
target_include_directories(transcription_under_test PRIVATE
    "${RECOGNITION_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(transcription_under_test PRIVATE
    -Wall -Wextra
    -include "${CMAKE_CURRENT_SOURCE_DIR}/stubs/qt/string_literals.h")
target_link_libraries(transcription_under_test PRIVATE Qt6::Core)
add_executable(transcription_tests transcription_tests.cpp)
target_include_directories(transcription_tests PRIVATE
    "${RECOGNITION_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(transcription_tests PRIVATE -Wall -Wextra)
target_link_libraries(transcription_tests PRIVATE
    transcription_under_test Qt6::Core Threads::Threads)
foreach(case groq_request groq_parse gemini_interaction gemini_extract mime error)
    add_test(
        NAME transcription_${case}
        COMMAND transcription_tests ${case})
endforeach()
