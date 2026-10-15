cmake_minimum_required(VERSION 3.20)
project(FreshGramOcrLinkCheck LANGUAGES C CXX)

get_filename_component(freshgram_root ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
set(third_party_loc ${freshgram_root}/Telegram/ThirdParty)
include(${freshgram_root}/Telegram/cmake/lib_tesseract.cmake)

add_executable(ocr_link_check main.cpp)
target_link_libraries(ocr_link_check PRIVATE tdesktop::lib_tesseract)

enable_testing()
add_test(NAME ocr_engine_lifecycle COMMAND ocr_link_check)
