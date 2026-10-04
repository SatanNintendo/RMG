cmake_minimum_required(VERSION 3.15)

project(mupen64plus-audio-aziaudio LANGUAGES C CXX)

# Isolated AziAudio-Plus Mupen64Plus plugin for RMG.
# No CMAKE_BUILD_TYPE or global compiler flags are changed here.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# RMG already uses SDL3.  Do not pull SDL2 into the same process by default.
# On non-Windows systems the archive's SDL2 backend remains available as an
# explicit opt-in for users who know their runtime environment supports it.
option(AZIAUDIO_ENABLE_SDL2
    "Build AziAudio's SDL2 backend on non-Windows systems"
    OFF
)

set(AZI_COMMON_SOURCES
    src/MinimalAlloc.cpp
    src/main.cpp
    src/Configuration.cpp
    src/SoundDriver.cpp
    src/SoundDriverFactory.cpp
    src/NoSoundDriver.cpp
    src/HLEMain.cpp
    src/HLEUtils.cpp
    src/ABI1.cpp
    src/ABI2.cpp
    src/ABI3.cpp
    src/ABI3mp3.cpp
    src/ABI_Adpcm.cpp
    src/ABI_Buffers.cpp
    src/ABI_Envmixer.cpp
    src/ABI_Filters.cpp
    src/ABI_MixerInterleave.cpp
    src/ABI_Resample.cpp
)

set(AZI_HLE_C_SOURCES
    src/Mupen64plusHLE/musyx.c
    src/Mupen64plusHLE/audio.c
    src/Mupen64plusHLE/memory.c
    src/Mupen64plusHLE/Mupen64Support.c
)

if(WIN32)
    enable_language(RC)
    list(APPEND AZI_COMMON_SOURCES
        src/DirectSoundDriver.cpp
        src/XAudio2SoundDriver.cpp
        src/XAudio2SoundDriverModern.cpp
        src/WASAPISoundDriver.cpp
        src/WaveOutSoundDriver.cpp
        src/ConfigDialog.cpp
        src/AziAudioPlus.rc
    )
else()
    if(AZIAUDIO_ENABLE_SDL2)
        list(APPEND AZI_COMMON_SOURCES
            src/SDL/SDL2SoundDriver.cpp
        )
        find_package(SDL2 REQUIRED)
    endif()
endif()

add_library(mupen64plus-audio-aziaudio MODULE
    ${AZI_COMMON_SOURCES}
    ${AZI_HLE_C_SOURCES}
)

set_target_properties(mupen64plus-audio-aziaudio PROPERTIES
    PREFIX ""
    OUTPUT_NAME "mupen64plus-audio-aziaudio"
)

target_include_directories(mupen64plus-audio-aziaudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mupen64plus-api
)

if(WIN32)
    target_include_directories(mupen64plus-audio-aziaudio PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/3rdParty/directx/include
    )

    target_compile_definitions(mupen64plus-audio-aziaudio PRIVATE
        _WIN32_WINNT=0x0602
    )

    target_link_libraries(mupen64plus-audio-aziaudio PRIVATE
        dsound
        winmm
        ole32
        comctl32
        psapi
        uuid
    )

    if(MSVC)
        target_link_options(mupen64plus-audio-aziaudio PRIVATE /STACK:4194304)
    endif()
else()
    target_compile_definitions(mupen64plus-audio-aziaudio PRIVATE
        M64P_NONWIN_BUILD
    )

    if(NOT AZIAUDIO_ENABLE_SDL2)
        target_compile_definitions(mupen64plus-audio-aziaudio PRIVATE
            AZI_DISABLE_SDL2=1
        )
    endif()

    find_package(Threads REQUIRED)
    target_link_libraries(mupen64plus-audio-aziaudio PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )

    if(AZIAUDIO_ENABLE_SDL2)
        target_link_libraries(mupen64plus-audio-aziaudio PRIVATE SDL2::SDL2)
        target_include_directories(mupen64plus-audio-aziaudio PRIVATE ${SDL2_INCLUDE_DIRS})
    endif()
endif()

if(MSVC)
    target_compile_options(mupen64plus-audio-aziaudio PRIVATE
        /W4 /permissive- /Zc:__cplusplus
        /wd4996
        /wd26812
    )
else()
    target_compile_options(mupen64plus-audio-aziaudio PRIVATE
        -Wall
        -Wno-unknown-pragmas
        -Wno-unused-result
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
    )
endif()

# Preserve the archive plugin's non-CPU size/symbol-discipline optimisations
# without exporting those settings to the rest of RMG.  No -march/-mtune flags
# are imported from any archive build profile.
if(WIN32 AND (MINGW OR MSYS) AND CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(mupen64plus-audio-aziaudio PRIVATE
        -Os
        -fno-unwind-tables
        -fno-asynchronous-unwind-tables
        -fmerge-all-constants
        -fno-ident
    )
    target_compile_options(mupen64plus-audio-aziaudio PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    )
    target_link_options(mupen64plus-audio-aziaudio PRIVATE
        -Wl,--gc-sections
        -Wl,--exclude-all-symbols
        -Wl,--build-id=none
        -s
    )
endif()

if(NOT MSVC)
    set_target_properties(mupen64plus-audio-aziaudio PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        C_VISIBILITY_PRESET hidden
    )
    if(WIN32 AND (MINGW OR MSYS))
        target_link_options(mupen64plus-audio-aziaudio PRIVATE
            -static-libgcc
            -static-libstdc++
        )
    endif()
endif()

include(GNUInstallDirs)
# Prefer the destination variables supplied by RMG; keep a standalone
# fallback so this directory remains independently buildable.
if(DEFINED AUDIO_PLUGIN_INSTALL_PATH)
    set(AZIAUDIO_PLUGIN_INSTALL_PATH "${AUDIO_PLUGIN_INSTALL_PATH}")
elseif(DEFINED PLUGIN_INSTALL_PATH)
    set(AZIAUDIO_PLUGIN_INSTALL_PATH "${PLUGIN_INSTALL_PATH}/Audio")
else()
    set(AZIAUDIO_PLUGIN_INSTALL_PATH "${CMAKE_INSTALL_LIBDIR}/mupen64plus/Audio")
endif()

install(TARGETS mupen64plus-audio-aziaudio
    LIBRARY DESTINATION ${AZIAUDIO_PLUGIN_INSTALL_PATH}
    RUNTIME DESTINATION ${AZIAUDIO_PLUGIN_INSTALL_PATH}
    ARCHIVE DESTINATION ${AZIAUDIO_PLUGIN_INSTALL_PATH}
)
