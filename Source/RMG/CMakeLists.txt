#
# Rosalie's Mupen GUI CMakeLists.txt
#
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# Explicitly tell AUTOUIC where to find .ui files.
# Without this, parallel builds can cause a race condition where
# the generated ui_SettingsDialog.h is written under a wrong name
# (e.g. ui_RomInfoDialog.h), breaking compilation.
set(CMAKE_AUTOUIC_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/UserInterface/Dialog"
    "${CMAKE_CURRENT_SOURCE_DIR}/UserInterface/Dialog/Netplay"
)

# Qt 6.2 is required because we use qt6_add_translations() (added in 6.2)
# and QLocale::Territory / QLocale::AnyTerritory (also introduced in 6.2).
#
# LinguistTools is intentionally NOT in the REQUIRED list:
#   - On Windows the official Qt installer bundles LinguistTools together
#     with the rest of Qt, so it is always available and translations are
#     compiled into the binary as before.
#   - On Linux the LinguistTools CMake config lives in a separate
#     distribution package (e.g. `qt6-linguist-tools` / `qt6-l10n-tools`
#     on Debian/Ubuntu, `qt6-tools` on Fedora). Some build environments
#     (CI runners, minimal containers) install only the runtime Qt
#     libraries and intentionally skip the tools package to save space.
#   - Making LinguistTools optional lets those minimal Linux environments
#     still build a working RMG binary; the only thing they lose is the
#     embedded .qm resources (i.e. the in-app language selector falls
#     back to English source strings). Installing the LinguistTools
#     package later and re-running CMake is enough to enable i18n.
find_package(Qt6 6.2 COMPONENTS Gui Widgets Core REQUIRED)
find_package(Qt6 6.2 COMPONENTS LinguistTools QUIET)
set(RMG_HAS_LINGUIST_TOOLS OFF)
if (Qt6LinguistTools_FOUND)
    set(RMG_HAS_LINGUIST_TOOLS ON)
else()
    message(WARNING
        "Qt6LinguistTools not found - .ts files will NOT be compiled to .qm "
        "and no translations will be embedded into the RMG binary. "
        "Install the qt6-linguist-tools (or qt6-l10n-tools) package and "
        "re-run CMake to enable translations. The build will otherwise "
        "succeed and produce a working English-only RMG binary.")
endif()

if (NOT WIN32)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif(NOT WIN32)

if (PORTABLE_INSTALL)
    add_definitions(-DPORTABLE_INSTALL)
endif(PORTABLE_INSTALL)

if (UPDATER)
    find_package(Qt6 COMPONENTS Network REQUIRED)
    add_definitions(-DUPDATER)
endif(UPDATER)

if (APPIMAGE_UPDATER)
    add_definitions(-DAPPIMAGE_UPDATER)
endif(APPIMAGE_UPDATER)

if (KCA_DRAG_DROP)
    find_package(KF6CoreAddons REQUIRED)
    add_definitions(-DKCA_DRAG_DROP)
endif(KCA_DRAG_DROP)

if (NETPLAY)
    find_package(Qt6 COMPONENTS WebSockets REQUIRED)
    add_definitions(-DNETPLAY)
endif(NETPLAY)

find_package(SDL3 REQUIRED)

set(RMG_SOURCES
    UserInterface/MainWindow.cpp
    UserInterface/MainWindow.ui
    UserInterface/Widget/RomBrowser/RomBrowserWidget.cpp
    UserInterface/Widget/RomBrowser/RomBrowserListViewWidget.cpp
    UserInterface/Widget/RomBrowser/RomBrowserGridViewWidget.cpp
    UserInterface/Widget/RomBrowser/RomBrowserLoadingWidget.cpp
    UserInterface/Widget/RomBrowser/RomBrowserEmptyWidget.cpp
    UserInterface/Widget/RomBrowser/RomBrowserEmptyWidget.ui
    UserInterface/Widget/RomBrowser/RomBrowserSearchWidget.cpp
    UserInterface/Widget/Render/DummyWidget.cpp
    UserInterface/Widget/Render/OGLWidget.cpp
    UserInterface/Widget/Render/VKWidget.cpp
    UserInterface/Widget/KeybindButton.cpp
    UserInterface/Dialog/SettingsDialog.cpp
    UserInterface/Dialog/SettingsDialog.ui
    UserInterface/Dialog/Cheats/CheatsCommon.cpp
    UserInterface/Dialog/Cheats/CheatsDialog.cpp
    UserInterface/Dialog/Cheats/CheatsDialog.ui
    UserInterface/Dialog/Cheats/AddCheatDialog.cpp
    UserInterface/Dialog/Cheats/AddCheatDialog.ui
    UserInterface/Dialog/Cheats/ChooseCheatOptionDialog.cpp
    UserInterface/Dialog/Cheats/ChooseCheatOptionDialog.ui
    UserInterface/Dialog/RomInfoDialog.cpp
    UserInterface/Dialog/RomInfoDialog.ui
    UserInterface/Dialog/AboutDialog.cpp
    UserInterface/Dialog/AboutDialog.ui
    UserInterface/Dialog/LogDialog.cpp
    UserInterface/Dialog/LogDialog.ui
    UserInterface/NoFocusDelegate.cpp
    UserInterface/EventFilter.cpp
    UserInterface/UIResources.rc
    UserInterface/UIResources.qrc
    Thread/RomSearcherThread.cpp
    Thread/EmulationThread.cpp
    Utilities/QtKeyToSdl3Key.cpp
    Utilities/QtMessageBox.cpp
    OnScreenDisplay.cpp
    Callbacks.cpp
    VidExt.cpp
    main.cpp
)

if (UPDATER)
    list(APPEND RMG_SOURCES
        UserInterface/Dialog/Update/UpdateDialog.cpp
        UserInterface/Dialog/Update/UpdateDialog.ui
        UserInterface/Dialog/Update/DownloadUpdateDialog.cpp
        UserInterface/Dialog/Update/DownloadUpdateDialog.ui
        UserInterface/Dialog/Update/InstallUpdateDialog.cpp
        UserInterface/Dialog/Update/InstallUpdateDialog.ui
    )
endif(UPDATER)

if (NETPLAY)
    list(APPEND RMG_SOURCES
        UserInterface/Dialog/Netplay/NetplayCommon.cpp
        UserInterface/Dialog/Netplay/CreateNetplaySessionDialog.cpp
        UserInterface/Dialog/Netplay/CreateNetplaySessionDialog.ui
        UserInterface/Dialog/Netplay/NetplaySessionBrowserDialog.cpp
        UserInterface/Dialog/Netplay/NetplaySessionBrowserDialog.ui
        UserInterface/Dialog/Netplay/NetplaySessionDialog.cpp
        UserInterface/Dialog/Netplay/NetplaySessionDialog.ui
        UserInterface/Dialog/Netplay/NetplaySessionPasswordDialog.cpp
        UserInterface/Dialog/Netplay/NetplaySessionPasswordDialog.ui
        UserInterface/Widget/Netplay/NetplaySessionBrowserWidget.cpp
        UserInterface/Widget/Netplay/NetplaySessionBrowserLoadingWidget.cpp
        UserInterface/Widget/Netplay/NetplaySessionBrowserEmptyWidget.cpp
        UserInterface/Widget/Netplay/NetplaySessionBrowserEmptyWidget.ui
        UserInterface/Widget/Netplay/CreateNetplaySessionWidget.cpp
        UserInterface/Widget/Netplay/CreateNetplaySessionEmptyWidget.cpp
        UserInterface/Widget/Netplay/CreateNetplaySessionEmptyWidget.ui
    )
endif(NETPLAY)

set_source_files_properties(${IMGUI_SOURCES} PROPERTIES GENERATED TRUE)
list(APPEND RMG_SOURCES ${IMGUI_SOURCES})

if (CMAKE_BUILD_TYPE MATCHES Release)
    add_executable(RMG WIN32 ${RMG_SOURCES})
else()
    add_executable(RMG ${RMG_SOURCES})
endif()

target_link_libraries(RMG 
    RMG-Core
    imgui
)

# needed for dynamically linked RMG-Core
if (PORTABLE_INSTALL)
    set_target_properties(RMG PROPERTIES
        INSTALL_RPATH "$ORIGIN"
    )
endif(PORTABLE_INSTALL)

target_include_directories(RMG PRIVATE 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${IMGUI_DIR}
)

target_link_libraries(RMG Qt6::Gui Qt6::Widgets)

if (NOT WIN32)
    target_link_libraries(RMG Qt6::DBus)
endif(NOT WIN32)

if (UPDATER)
    target_link_libraries(RMG Qt6::Network)
endif(UPDATER)

if (KCA_DRAG_DROP)
    target_link_libraries(RMG KF6::CoreAddons)
endif(KCA_DRAG_DROP)

if (NETPLAY)
    target_link_libraries(RMG Qt6::WebSockets)
endif(NETPLAY)

#
# Translations
#
# - Glob every Translations/*.ts file in this source directory.
# - qt6_add_translations() compiles each .ts -> .qm at build time,
#   wraps them into an embedded Qt resource at ":/i18n/RMG_<lang>.qm",
#   and links that resource into the RMG executable.
# - Because the resources are embedded, no extra runtime file
#   installation is required: the .qm files ship inside the binary.
# - To add a new language, drop a new "RMG_<locale>.ts" file into
#   Source/RMG/Translations/ and re-run CMake; the new language will
#   automatically be picked up and become selectable in Settings.
#
# qt6_add_translations() is available since Qt 6.2; it is the official
# replacement for the deprecated qt5_add_translation() / lrelease()
# manual workflow. The default resource prefix is ":/i18n".
#
# This block is skipped when Qt6LinguistTools is not installed (see
# the find_package() call near the top of this file). In that case
# the build still produces a working RMG binary; only the embedded
# translations are missing and the UI falls back to English source
# strings.
#
file(GLOB RMG_TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Translations/*.ts")

if (RMG_TS_FILES AND RMG_HAS_LINGUIST_TOOLS)
    # qt6_add_translations() defaults to embedding the compiled .qm files
    # under the ":/i18n" resource prefix, which matches what
    # MainWindow::loadTranslator() looks for. We deliberately do NOT
    # pass RESOURCE_PREFIX here so that the call works on every Qt 6.2+
    # (the RESOURCE_PREFIX parameter exists since 6.2 but had a few
    # implementation quirks in early point releases - the default
    # behavior is stable and is exactly what we want).
    qt6_add_translations(RMG
        TS_FILES ${RMG_TS_FILES}
    )
elseif (RMG_TS_FILES AND NOT RMG_HAS_LINGUIST_TOOLS)
    message(WARNING
        "RMG: ${RMG_TS_FILES} were found but Qt6LinguistTools is not "
        "installed - skipping .ts -> .qm compilation. See the warning "
        "above for how to install the missing package.")
else()
    message(WARNING "RMG: no translation .ts files found in ${CMAKE_CURRENT_SOURCE_DIR}/Translations - i18n will be disabled")
endif()

