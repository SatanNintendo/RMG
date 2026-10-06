cmake_minimum_required(VERSION 3.15)

project(NRage_Input_V2 VERSION 2.4.7 LANGUAGES C CXX)

# Isolated Windows-only N-Rage Input V2 Mupen64Plus plugin for RMG.

if(NOT WIN32)
    message(STATUS "N-Rage Input V2 is Windows-only; the target is skipped.")
    return()
endif()

enable_language(RC)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NRAGE_PLUGIN_SOURCES
    src/NRagePluginV2.cpp
    src/MupenPlusAPI.cpp
    src/M64PConfig.cpp
    src/FileAccess.cpp
    src/DirectInput.cpp
    src/Interface.cpp
    src/International.cpp
    src/PakIO.cpp
    src/GBCart.cpp
    src/XInputController.cpp
    src/Debug.cpp
    src/goombasav/goombasav.c
    src/goombasav/minilzo-2.06/minilzo.c
    src/NRagePluginV2.rc
)

add_library(NRage_Input_V2 MODULE ${NRAGE_PLUGIN_SOURCES})

set_target_properties(NRage_Input_V2 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "mupen64plus-input-nrage"
)

target_include_directories(NRage_Input_V2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/goombasav
    ${CMAKE_CURRENT_SOURCE_DIR}/src/goombasav/minilzo-2.06
)

target_compile_definitions(NRage_Input_V2 PRIVATE
    WIN32
    _WINDOWS
    _USRDLL
    NRAGE_EXPORTS
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

if(MSVC)
    set_property(TARGET NRage_Input_V2 PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_link_libraries(NRage_Input_V2 PRIVATE
        dinput8.lib
        dxguid.lib
        Xinput.lib
        comctl32.lib
        comdlg32.lib
        ole32.lib
        oleaut32.lib
        uuid.lib
        user32.lib
        gdi32.lib
        advapi32.lib
        shell32.lib
        winmm.lib
    )
elseif(MINGW)
    target_link_options(NRage_Input_V2 PRIVATE -static)
    target_link_libraries(NRage_Input_V2 PRIVATE
        dinput8
        dxguid
        xinput1_4
        comctl32
        comdlg32
        ole32
        oleaut32
        uuid
        user32
        gdi32
        advapi32
        shell32
        winmm
    )
endif()

if(MSVC)
    target_compile_options(NRage_Input_V2 PRIVATE
        /W3
        /wd4996
    )
endif()

# The original resource file refers to configs/Controller1.cpf and
# configs/Shortcuts.sc.  The plugin project root is also an include directory
# so both MSVC rc.exe and MinGW windres resolve those resources unchanged.

# Russian satellite DLL used by the plugin's language mechanism.
add_library(NRage_Language_1049 SHARED
    src/regional/satellite-dllmain.c
    "${CMAKE_CURRENT_SOURCE_DIR}/src/regional/Russian 1049.rc"
)

set_target_properties(NRage_Language_1049 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "NRage-Language-1049"
)

target_include_directories(NRage_Language_1049 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/regional
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(NRage_Language_1049 PRIVATE
    WIN32
    _WINDOWS
    UNICODE
    _UNICODE
)

include(GNUInstallDirs)
# Prefer the destination variable supplied by RMG; keep a standalone fallback.
if(DEFINED INPUT_PLUGIN_INSTALL_PATH)
    set(NRAGE_PLUGIN_INSTALL_PATH "${INPUT_PLUGIN_INSTALL_PATH}")
elseif(DEFINED PLUGIN_INSTALL_PATH)
    set(NRAGE_PLUGIN_INSTALL_PATH "${PLUGIN_INSTALL_PATH}/Input")
else()
    set(NRAGE_PLUGIN_INSTALL_PATH "${CMAKE_INSTALL_LIBDIR}/mupen64plus/Input")
endif()

install(TARGETS NRage_Input_V2
    LIBRARY DESTINATION ${NRAGE_PLUGIN_INSTALL_PATH}
    RUNTIME DESTINATION ${NRAGE_PLUGIN_INSTALL_PATH}
    ARCHIVE DESTINATION ${NRAGE_PLUGIN_INSTALL_PATH}
)

install(TARGETS NRage_Language_1049
    LIBRARY DESTINATION ${NRAGE_PLUGIN_INSTALL_PATH}
    RUNTIME DESTINATION ${NRAGE_PLUGIN_INSTALL_PATH}
    ARCHIVE DESTINATION ${NRAGE_PLUGIN_INSTALL_PATH}
)
