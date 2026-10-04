cmake_minimum_required(VERSION 3.15)

project(mupen64plus-video-softrdp LANGUAGES C)

# SoftRDP Mupen64Plus-only integration for RMG.
# The Project64 adapter and its 32-bit toolchain are intentionally omitted:
# RMG uses the Mupen64Plus adapter, and this keeps the integration independent
# of the archive's i7-2600K/Sandy-Bridge CPU build profile.

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(SOFTRDP_ENABLE_LOG "Enable SoftRDP logging" OFF)
option(SOFTRDP_CENSUS "Enable SoftRDP pixel census" OFF)
option(SOFTRDP_ENABLE_DUMP "Enable SoftRDP frame dumping" OFF)

if(SOFTRDP_ENABLE_DUMP AND NOT WIN32)
    message(FATAL_ERROR
        "SOFTRDP_ENABLE_DUMP=ON is Windows-only in the Mupen64Plus RMG adapter.")
endif()

set(SOFTRDP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(SOFTRDP_SCALED_SOURCES
    ${SOFTRDP_SOURCE_DIR}/core/sr.c
    ${SOFTRDP_SOURCE_DIR}/core/rdp_commands.c
    ${SOFTRDP_SOURCE_DIR}/core/rdp_memory.c
    ${SOFTRDP_SOURCE_DIR}/core/framebuffer.c
    ${SOFTRDP_SOURCE_DIR}/core/raster.c
    ${SOFTRDP_SOURCE_DIR}/core/tmem.c
    ${SOFTRDP_SOURCE_DIR}/core/primitive.c
    ${SOFTRDP_SOURCE_DIR}/core/kernel.c
    ${SOFTRDP_SOURCE_DIR}/core/blend_divider.c
    ${SOFTRDP_SOURCE_DIR}/core/vi.c
)

set(SOFTRDP_SHARED_CORE_SOURCES
    ${SOFTRDP_SOURCE_DIR}/core/sr_dispatch.c
    ${SOFTRDP_SOURCE_DIR}/core/sr_threads_vi.c
    ${SOFTRDP_SOURCE_DIR}/core/sr_async.c
)

if(WIN32)
    list(APPEND SOFTRDP_SHARED_CORE_SOURCES
        ${SOFTRDP_SOURCE_DIR}/core/sr_threads_win32.c
    )
else()
    list(APPEND SOFTRDP_SHARED_CORE_SOURCES
        ${SOFTRDP_SOURCE_DIR}/core/sr_threads_posix.c
    )
endif()

set(SOFTRDP_PRESENT_SOURCES
    ${SOFTRDP_SOURCE_DIR}/present/sr_present_gl.c
    ${SOFTRDP_SOURCE_DIR}/present/sr_field_output.c
)

set(SOFTRDP_PLUGIN_SOURCES
    ${SOFTRDP_SOURCE_DIR}/plugin/plugin_common.c
    ${SOFTRDP_SOURCE_DIR}/plugin/mupen64plus/mupen64plus_gfx.c
    ${SOFTRDP_SOURCE_DIR}/plugin/mupen64plus/mupen64plus_config_win32.c
)

if(SOFTRDP_ENABLE_DUMP)
    list(APPEND SOFTRDP_SHARED_CORE_SOURCES
        ${SOFTRDP_SOURCE_DIR}/core/sr_dump.c
    )
    list(APPEND SOFTRDP_PLUGIN_SOURCES
        ${SOFTRDP_SOURCE_DIR}/plugin/plugin_dump.c
    )
endif()

# One namespaced copy of the scaled renderer for 1x and 2x paths.  This is
# the key mechanism used by the original SoftRDP build and is preserved here
# without importing its CPU-specific -march/-mtune flags.
add_library(softrdp_core_s1 OBJECT ${SOFTRDP_SCALED_SOURCES})
add_library(softrdp_core_s2 OBJECT ${SOFTRDP_SCALED_SOURCES})
add_library(softrdp_core_shared OBJECT ${SOFTRDP_SHARED_CORE_SOURCES})

foreach(_target IN ITEMS softrdp_core_s1 softrdp_core_s2 softrdp_core_shared)
    target_include_directories(${_target} PRIVATE
        ${SOFTRDP_SOURCE_DIR}
    )
    target_compile_definitions(${_target} PRIVATE
        SOFTRDP_ENABLE_LOG=$<IF:$<BOOL:${SOFTRDP_ENABLE_LOG}>,1,0>
        SOFTRDP_CENSUS=$<IF:$<BOOL:${SOFTRDP_CENSUS}>,1,0>
        SOFTRDP_ENABLE_DUMP=$<IF:$<BOOL:${SOFTRDP_ENABLE_DUMP}>,1,0>
    )
endforeach()

target_compile_definitions(softrdp_core_s1 PRIVATE
    SOFTRDP_SCALE=1
    SR_SCALE_SUFFIX=_s1
)
target_compile_definitions(softrdp_core_s2 PRIVATE
    SOFTRDP_SCALE=2
    SR_SCALE_SUFFIX=_s2
)

# sr_namespace.h is designed as a forced include because it renames the
# renderer symbols before the scaled sources are compiled. Support GCC/Clang
# and MSVC without changing the parent RMG toolchain.
if(MSVC)
    target_compile_options(softrdp_core_s1 PRIVATE /FI${SOFTRDP_SOURCE_DIR}/core/sr_namespace.h)
    target_compile_options(softrdp_core_s2 PRIVATE /FI${SOFTRDP_SOURCE_DIR}/core/sr_namespace.h)
else()
    target_compile_options(softrdp_core_s1 PRIVATE -include ${SOFTRDP_SOURCE_DIR}/core/sr_namespace.h)
    target_compile_options(softrdp_core_s2 PRIVATE -include ${SOFTRDP_SOURCE_DIR}/core/sr_namespace.h)
endif()

if(NOT WIN32)
    set_target_properties(
        softrdp_core_s1 softrdp_core_s2 softrdp_core_shared
        PROPERTIES POSITION_INDEPENDENT_CODE ON
    )
endif()

# The scale-dependent renderer is deliberately compiled twice. On memory-
# constrained CI runners, compiling both large kernel translation units at
# once can exhaust RAM. A Ninja-only job pool serialises these SoftRDP compile
# jobs without changing RMG's global build parallelism or compiler flags.
if(CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS softrdp_compile=1)
    foreach(_target IN ITEMS softrdp_core_s1 softrdp_core_s2 softrdp_core_shared)
        set_property(TARGET ${_target} PROPERTY JOB_POOL_COMPILE softrdp_compile)
    endforeach()
endif()

add_library(mupen64plus-video-softrdp MODULE
    ${SOFTRDP_PRESENT_SOURCES}
    ${SOFTRDP_PLUGIN_SOURCES}
    $<TARGET_OBJECTS:softrdp_core_s1>
    $<TARGET_OBJECTS:softrdp_core_s2>
    $<TARGET_OBJECTS:softrdp_core_shared>
)

target_include_directories(mupen64plus-video-softrdp PRIVATE
    ${SOFTRDP_SOURCE_DIR}
    ${SOFTRDP_SOURCE_DIR}/plugin/mupen64plus/api
)

target_compile_definitions(mupen64plus-video-softrdp PRIVATE
    SOFTRDP_ENABLE_LOG=$<IF:$<BOOL:${SOFTRDP_ENABLE_LOG}>,1,0>
    SOFTRDP_CENSUS=$<IF:$<BOOL:${SOFTRDP_CENSUS}>,1,0>
    SOFTRDP_ENABLE_DUMP=$<IF:$<BOOL:${SOFTRDP_ENABLE_DUMP}>,1,0>
)

set_target_properties(mupen64plus-video-softrdp PROPERTIES
    PREFIX ""
    OUTPUT_NAME "mupen64plus-video-softrdp"
)

if(WIN32)
    target_link_libraries(mupen64plus-video-softrdp PRIVATE
        opengl32
        gdi32
        user32
    )
else()
    find_package(Threads REQUIRED)
    target_link_libraries(mupen64plus-video-softrdp PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
endif()

if(NOT MSVC)
    set_target_properties(mupen64plus-video-softrdp PROPERTIES
        C_VISIBILITY_PRESET hidden
    )
endif()

include(GNUInstallDirs)
# Prefer the destination variables supplied by RMG; keep a standalone
# fallback so this directory remains buildable on its own.
if(DEFINED GFX_PLUGIN_INSTALL_PATH)
    set(SOFTRDP_PLUGIN_INSTALL_PATH "${GFX_PLUGIN_INSTALL_PATH}")
elseif(DEFINED PLUGIN_INSTALL_PATH)
    set(SOFTRDP_PLUGIN_INSTALL_PATH "${PLUGIN_INSTALL_PATH}/GFX")
else()
    set(SOFTRDP_PLUGIN_INSTALL_PATH "${CMAKE_INSTALL_LIBDIR}/mupen64plus/GFX")
endif()

install(TARGETS mupen64plus-video-softrdp
    LIBRARY DESTINATION ${SOFTRDP_PLUGIN_INSTALL_PATH}
    RUNTIME DESTINATION ${SOFTRDP_PLUGIN_INSTALL_PATH}
    ARCHIVE DESTINATION ${SOFTRDP_PLUGIN_INSTALL_PATH}
)
