option(FRESHGRAM_BUNDLE_CLOUD_ASR "Bundle the independent cloud ASR component" ON)
set(FRESHGRAM_ASR_TARGET "" CACHE STRING "Rust target triple for the ASR component")

if (FRESHGRAM_BUNDLE_CLOUD_ASR)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(FRESHGRAM_ASR_CARGO cargo REQUIRED)
    set(asr_target_args)
    set(asr_environment)
    if (APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND asr_environment MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
    endif()
    if (FRESHGRAM_ASR_TARGET)
        list(APPEND asr_target_args --target ${FRESHGRAM_ASR_TARGET})
    elseif (APPLE AND CMAKE_OSX_ARCHITECTURES)
        foreach (arch IN LISTS CMAKE_OSX_ARCHITECTURES)
            if (arch STREQUAL "arm64")
                list(APPEND asr_target_args --target aarch64-apple-darwin)
            elseif (arch STREQUAL "x86_64")
                list(APPEND asr_target_args --target x86_64-apple-darwin)
            else()
                message(FATAL_ERROR "Set FRESHGRAM_ASR_TARGET for ${arch}")
            endif()
        endforeach()
    elseif (WIN32)
        if (CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64"
            OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
            list(APPEND asr_target_args --target aarch64-pc-windows-msvc)
        elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
            list(APPEND asr_target_args --target i686-pc-windows-msvc)
        else()
            list(APPEND asr_target_args --target x86_64-pc-windows-msvc)
        endif()
    elseif (CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Set FRESHGRAM_ASR_TARGET when cross-compiling")
    endif()
    get_filename_component(asr_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    add_custom_target(voice_asr_cloud
        COMMAND ${CMAKE_COMMAND} -E env ${asr_environment}
            ${Python3_EXECUTABLE} ${asr_root}/tools/install_voice_asr.py
            --cargo ${FRESHGRAM_ASR_CARGO}
            --flavor cloud
            --profile $<IF:$<CONFIG:Debug>,dev,release>
            --build-dir ${CMAKE_CURRENT_BINARY_DIR}/voice-asr-cloud
            --destination $<TARGET_FILE_DIR:Telegram>/voice-asr/cloud
            ${asr_target_args}
        VERBATIM)
    add_dependencies(Telegram voice_asr_cloud)
endif()
