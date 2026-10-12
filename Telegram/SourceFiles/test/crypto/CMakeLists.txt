cmake_minimum_required(VERSION 3.25)
project(freshgram_crypto_checks LANGUAGES CXX)

enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(SQLite3 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(TARGET SQLite3::SQLite3)
    set(sqlite_target SQLite3::SQLite3)
else()
    set(sqlite_target SQLite::SQLite3)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(FRESHGRAM_CRYPTO_SANITIZERS "" CACHE STRING "Example: address,undefined")
get_filename_component(src_loc "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${src_loc}/core/utils.cpp"
    "${src_loc}/mtproto/mtproto_auth_key.cpp"
    "${src_loc}/mtproto/mtproto_auth_key.h"
    "${src_loc}/storage/details/storage_file_utilities.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/extract_ayv1_decoder.py"
)
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/extract_ayv1_decoder.py"
        "${src_loc}"
        "${CMAKE_CURRENT_BINARY_DIR}/tdesktop_ayv1_decoder.inc"
    COMMAND_ERROR_IS_FATAL ANY
)

add_library(crypto_under_test STATIC
    "${src_loc}/ayu/secret/secret_crypto.cpp"
    "${src_loc}/ayu/secret/secret_vault.cpp"
    "${src_loc}/ayu/data/ayu_vault.cpp"
    "${src_loc}/ayu/features/recognition/transcription_secrets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs/storage_file_utilities.cpp"
)
target_include_directories(crypto_under_test PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${src_loc}"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(crypto_under_test PUBLIC
    Qt6::Core OpenSSL::Crypto ${sqlite_target}
)
target_precompile_headers(crypto_under_test PUBLIC <cstddef>)
if(MSVC)
    target_compile_options(crypto_under_test PUBLIC /W4)
else()
    target_compile_options(crypto_under_test PUBLIC
        -Wall -Wextra -Wno-deprecated-declarations
    )
    if(FRESHGRAM_CRYPTO_SANITIZERS)
        target_compile_options(crypto_under_test PUBLIC
            "-fsanitize=${FRESHGRAM_CRYPTO_SANITIZERS}"
            -fno-omit-frame-pointer
        )
        target_link_options(crypto_under_test PUBLIC
            "-fsanitize=${FRESHGRAM_CRYPTO_SANITIZERS}"
        )
    endif()
endif()

add_executable(crypto_tests crypto_tests.cpp)
add_executable(credential_tests credential_tests.cpp)
target_link_libraries(credential_tests PRIVATE crypto_under_test)
add_test(NAME transcription_credential_persistence COMMAND credential_tests)
add_executable(secret_protocol_tests
    secret_protocol_test.cpp
    "${src_loc}/ayu/secret/secret_protocol.cpp"
    "${src_loc}/ayu/secret/secret_model.cpp")
target_link_libraries(secret_protocol_tests PRIVATE crypto_under_test)
add_test(NAME secret_protocol COMMAND secret_protocol_tests)
add_executable(secret_rekey_tests secret_rekey_test.cpp)
target_link_libraries(secret_rekey_tests PRIVATE crypto_under_test)
add_test(NAME secret_rekey COMMAND secret_rekey_tests)
add_executable(crypto_bench crypto_bench.cpp)
target_link_libraries(crypto_bench PRIVATE crypto_under_test)

add_library(database_under_test STATIC
    "${src_loc}/ayu/data/ayu_database.cpp"
    "${src_loc}/ayu/data/ayu_database_backup.cpp"
)
target_link_libraries(database_under_test PUBLIC crypto_under_test)
target_precompile_headers(database_under_test PRIVATE
    <memory> <optional> <vector> <QtCore/QFile> <QtCore/QJsonObject>
)
target_link_libraries(crypto_tests PRIVATE database_under_test)
add_executable(crypto_database_tests crypto_database_tests.cpp)
target_link_libraries(crypto_database_tests PRIVATE database_under_test)
foreach(case metadata strict_replace sync_dialogs search_ties search_limits
        search_filters message_search backfill locked_start legacy_regex regex_identity)
    add_test(NAME "crypto_db_${case}" COMMAND crypto_database_tests "${case}")
    set_tests_properties("crypto_db_${case}" PROPERTIES TIMEOUT 90)
endforeach()
if(WIN32)
    target_link_libraries(crypto_bench PRIVATE psapi)
endif()

foreach(suite vault rows archive backup regression)
    add_test(NAME "crypto_${suite}" COMMAND crypto_tests --suite "${suite}")
    set_tests_properties("crypto_${suite}" PROPERTIES TIMEOUT 90)
endforeach()
if(UNIX AND NOT FRESHGRAM_CRYPTO_SANITIZERS)
    add_test(NAME crypto_small_stack
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/small_stack.py"
            "$<TARGET_FILE:crypto_tests>"
    )
    set_tests_properties(crypto_small_stack PROPERTIES TIMEOUT 30)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT FRESHGRAM_CRYPTO_SANITIZERS)
    find_package(Threads REQUIRED)
    add_library(publication_fault SHARED publication_fault.cpp)
    target_link_libraries(publication_fault PRIVATE Qt6::Core ${CMAKE_DL_LIBS} Threads::Threads)
    add_executable(crypto_io_faults crypto_io_faults.cpp)
    target_link_libraries(crypto_io_faults PRIVATE database_under_test ${CMAKE_DL_LIBS} Threads::Threads)
    foreach(operation seal open tag backup_race backup_fail backup_clear_race
            snapshot_clear_race)
        add_test(NAME "crypto_atomic_${operation}"
            COMMAND "${CMAKE_COMMAND}" -E env
                "LD_PRELOAD=$<TARGET_FILE:publication_fault>"
                "$<TARGET_FILE:crypto_io_faults>" "${operation}"
        )
        set_tests_properties("crypto_atomic_${operation}" PROPERTIES TIMEOUT 30)
    endforeach()
endif()
