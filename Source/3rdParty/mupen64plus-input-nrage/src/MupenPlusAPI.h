/*
        N-Rage`s Dinput8 Plugin -- Mupen64Plus 2.x API layer
    (C) 2002, 2006  Norbert Wladyka
    (C) 2026        Mupen64Plus port

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _MUPENPLUSAPI_H_
#define _MUPENPLUSAPI_H_

#include <windows.h>

// ---------------------------------------------------------------------------
// Export decorations come from the vendored m64p headers themselves
// (m64p_types.h defines EXPORT/CALL for Win32, and every prototype sits in
// an extern "C" block, which gives our matching function definitions C
// linkage too).  This mirrors how GLideN64 and other official C++ plugins
// do it.
// ---------------------------------------------------------------------------

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p/m64p_types.h"
#include "m64p/m64p_common.h"
#include "m64p/m64p_config.h"
#include "m64p/m64p_plugin.h"
#include "m64p/m64p_frontend.h"

// The m64p headers do not ship numeric API-version constants; official
// plugins define them locally (see e.g. mupen64plus-input-raphnetraw's
// version.h; the core reports 0x020302 via CoreGetAPIVersions -- see
// mupen64plus-core src/main/version.h).  We accept any 2.x Config API.
#define CONFIG_API_VERSION 0x020302

// ---------------------------------------------------------------------------
// Plugin identity
// ---------------------------------------------------------------------------

// API version we implement (input plugin spec 2.1.0 -- accepted by every
// Mupen64Plus 2.x core; the core requires >= 0x020100 and a matching
// upper 16 bits).
#define INPUT_PLUGIN_API_VERSION  0x020100

// Plugin version (semantic: 2.4.7 of the N-Rage line, Mupen64Plus port).
#define NRAGE_PLUGIN_VERSION      0x00020407

// Version of our own configuration data stored in mupen64plus.cfg.
#define NRAGE_CONFIG_VERSION      1

// ---------------------------------------------------------------------------
// Section / parameter names used in the Mupen64Plus configuration file.
// ---------------------------------------------------------------------------
#define M64CFG_SECTION_NRAGE      "NRage"                  // general + folders + browser dirs
#define M64CFG_SECTION_INPUT      "NRage-Input"            // + controller number (1..4)
#define M64CFG_SECTION_SHORTCUTS  "NRage-Shortcuts"

// ---------------------------------------------------------------------------
// Core API function pointers (resolved in PluginStartup via GetProcAddress)
// ---------------------------------------------------------------------------
extern ptr_CoreGetAPIVersions     CoreGetAPIVersions;
extern ptr_CoreDoCommand          CoreDoCommand;
extern ptr_ConfigOpenSection      ConfigOpenSection;
extern ptr_ConfigDeleteSection    ConfigDeleteSection;
extern ptr_ConfigSetParameter     ConfigSetParameter;
extern ptr_ConfigGetParameter     ConfigGetParameter;
extern ptr_ConfigSetDefaultInt    ConfigSetDefaultInt;
extern ptr_ConfigSetDefaultBool   ConfigSetDefaultBool;
extern ptr_ConfigSetDefaultString ConfigSetDefaultString;
extern ptr_ConfigGetParamInt      ConfigGetParamInt;
extern ptr_ConfigGetParamBool     ConfigGetParamBool;
extern ptr_ConfigGetParamString   ConfigGetParamString;
extern ptr_ConfigSaveSection      ConfigSaveSection;
extern ptr_ConfigGetSharedDataFilepath ConfigGetSharedDataFilepath;
extern ptr_ConfigGetUserConfigPath     ConfigGetUserConfigPath;
extern ptr_ConfigGetUserDataPath       ConfigGetUserDataPath;
extern ptr_ConfigGetUserCachePath      ConfigGetUserCachePath;

// True once PluginStartup() succeeded.
extern bool g_bM64PInitialized;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Sends a debug message to the core (DebugCallback given to PluginStartup).
void M64PDebugMessage(int level, const char *format, ...);

// Resolves a usable HWND of the emulator's window.  Mupen64Plus front-ends
// own the video window and never hand a handle to input plugins, so we have
// to find it ourselves:
//   1. the foreground window, if it belongs to our process,
//   2. otherwise the largest visible top-level window of our process
//      (excluding the console window),
//   3. otherwise NULL.
// The handle is refreshed at InitiateControllers / RomOpen / PluginConfig
// time; DirectInput cooperative levels are re-bound when it changes.
HWND ResolveEmulatorWindowHandle(void);

// Stores/resolves the last handle returned by ResolveEmulatorWindowHandle.
void    SetEmulatorWindowHandle(HWND hWnd);
HWND    GetEmulatorWindowHandle(void);

// Returns the per-user data directory reported by the core
// (ConfigGetUserDataPath), with a trailing backslash, or an empty string
// when the core is not hooked yet.  All plugin-relative paths default to
// "<data dir>\NRage".
bool GetM64PUserDataDirectory(LPTSTR pszDirectory, DWORD cchDirectory);

// Creates "<data dir>\NRage" (used for XInput configs and debug logs).
void EnsureNRageDataDirectory(void);

// Optional configuration-dialog export (mupen64plus-core PR #774).  RMG calls it
// when the user hits "Configure" in Settings -> Input; see RMG's
// m64p_custom.h.  'parent' is a frontend-owned pointer (a QWidget* in RMG)
// and must NOT be treated as an HWND.  Not declared in the stock m64p
// headers, hence the explicit C-linkage prototype here.
#ifdef __cplusplus
extern "C" {
#endif
EXPORT m64p_error CALL PluginConfig(void *parent);
#ifdef __cplusplus
}
#endif

#endif // _MUPENPLUSAPI_H_
