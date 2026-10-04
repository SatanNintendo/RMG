# RMG integrated third-party plugins.
#
# This file is included by Source/3rdParty/CMakeLists.txt.  Each plugin is a
# self-contained target; no source files from RMG-Core/RMG-Audio/RMG-Input are
# replaced or patched.

option(RMG_BUILD_INTEGRATED_SOFTRDP
    "Build the integrated SoftRDP Mupen64Plus video plugin"
    ON
)
option(RMG_BUILD_INTEGRATED_AZIAUDIO
    "Build the integrated AziAudio-Plus Mupen64Plus audio plugin"
    ON
)
option(RMG_BUILD_INTEGRATED_NRAGE
    "Build the integrated N-Rage Input V2 Mupen64Plus plugin on Windows"
    ON
)

if(RMG_BUILD_INTEGRATED_SOFTRDP)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/mupen64plus-video-softrdp"
        "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-video-softrdp"
    )
endif()

if(RMG_BUILD_INTEGRATED_AZIAUDIO)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/mupen64plus-audio-aziaudio"
        "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-audio-aziaudio"
    )
endif()

if(RMG_BUILD_INTEGRATED_NRAGE AND WIN32)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/mupen64plus-input-nrage"
        "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-input-nrage"
    )
endif()
