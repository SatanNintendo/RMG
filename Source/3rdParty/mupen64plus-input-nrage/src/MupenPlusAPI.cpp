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

#include "commonIncludes.h"
#include "MupenPlusAPI.h"
#include "M64PConfig.h"
#include "NRagePluginV2.h"
#include "Interface.h"
#include "FileAccess.h"
#include "PakIO.h"
#include "DirectInput.h"
#include "International.h"
#include "Debug.h"

#include <stdio.h>

// ---------------------------------------------------------------------------
// Core API function pointers
// ---------------------------------------------------------------------------
ptr_CoreGetAPIVersions     CoreGetAPIVersions     = NULL;
ptr_CoreDoCommand          CoreDoCommand          = NULL;
ptr_ConfigOpenSection      ConfigOpenSection      = NULL;
ptr_ConfigDeleteSection    ConfigDeleteSection    = NULL;
ptr_ConfigSetParameter     ConfigSetParameter     = NULL;
ptr_ConfigGetParameter     ConfigGetParameter     = NULL;
ptr_ConfigSetDefaultInt    ConfigSetDefaultInt    = NULL;
ptr_ConfigSetDefaultBool   ConfigSetDefaultBool   = NULL;
ptr_ConfigSetDefaultString ConfigSetDefaultString = NULL;
ptr_ConfigGetParamInt      ConfigGetParamInt      = NULL;
ptr_ConfigGetParamBool     ConfigGetParamBool     = NULL;
ptr_ConfigGetParamString   ConfigGetParamString   = NULL;
ptr_ConfigSaveSection      ConfigSaveSection      = NULL;
ptr_ConfigGetSharedDataFilepath ConfigGetSharedDataFilepath = NULL;
ptr_ConfigGetUserConfigPath     ConfigGetUserConfigPath     = NULL;
ptr_ConfigGetUserDataPath       ConfigGetUserDataPath       = NULL;
ptr_ConfigGetUserCachePath      ConfigGetUserCachePath      = NULL;

bool g_bM64PInitialized = false;

// ---------------------------------------------------------------------------
// Debug callback (handed to us by the front-end in PluginStartup)
// ---------------------------------------------------------------------------
static void (*g_pDebugCallback)(void *, int, const char *) = NULL;
static void  *g_pDebugContext = NULL;

void M64PDebugMessage(int level, const char *format, ...)
{
    if (g_pDebugCallback == NULL)
        return;

    char szBuffer[1024];
    va_list val;
    va_start(val, format);
    _vsnprintf(szBuffer, sizeof(szBuffer) - 1, format, val);
    va_end(val);
    szBuffer[sizeof(szBuffer) - 1] = '\0';

    (*g_pDebugCallback)(g_pDebugContext, level, szBuffer);
}

// ---------------------------------------------------------------------------
// Emulator window resolution
// ---------------------------------------------------------------------------
static HWND g_hEmulatorWindow = NULL;

struct FINDWNDCTX
{
    HWND    hWndBest;
    LONG    lBestArea;
};

static BOOL CALLBACK FindEmulatorWndEnumProc(HWND hWnd, LPARAM lParam)
{
    FINDWNDCTX *pCtx = (FINDWNDCTX *)lParam;

    DWORD dwPID = 0;
    GetWindowThreadProcessId(hWnd, &dwPID);
    if (dwPID != GetCurrentProcessId())
        return TRUE;                        // foreign window, skip

    if (!IsWindowVisible(hWnd))
        return TRUE;                        // hidden, skip
    if (hWnd == GetConsoleWindow())
        return TRUE;                        // console window, skip
    if (GetWindow(hWnd, GW_OWNER) != NULL)
        return TRUE;                        // owned dialog (about, etc.), skip
    if (GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return TRUE;                        // tool windows, skip

    RECT rc;
    if (!GetWindowRect(hWnd, &rc))
        return TRUE;

    LONG lArea = (rc.right - rc.left) * (rc.bottom - rc.top);
    if (lArea > pCtx->lBestArea)
    {
        pCtx->lBestArea = lArea;
        pCtx->hWndBest = hWnd;
    }
    return TRUE;
}

HWND ResolveEmulatorWindowHandle(void)
{
    HWND hWnd = GetForegroundWindow();
    if (hWnd != NULL)
    {
        DWORD dwPID = 0;
        GetWindowThreadProcessId(hWnd, &dwPID);
        if (dwPID == GetCurrentProcessId() && hWnd != GetConsoleWindow())
        {
            g_hEmulatorWindow = hWnd;
            return hWnd;
        }
    }

    // The front-end's main/video window is (usually) the largest visible
    // top-level window of the emulator process.
    FINDWNDCTX ctx;
    ctx.hWndBest = NULL;
    ctx.lBestArea = 0;
    EnumWindows(FindEmulatorWndEnumProc, (LPARAM)&ctx);

    g_hEmulatorWindow = ctx.hWndBest;
    return ctx.hWndBest;
}

void SetEmulatorWindowHandle(HWND hWnd)
{
    g_hEmulatorWindow = hWnd;
}

HWND GetEmulatorWindowHandle(void)
{
    return g_hEmulatorWindow;
}

// ---------------------------------------------------------------------------
// Data directory helpers
// ---------------------------------------------------------------------------
bool GetM64PUserDataDirectory(LPTSTR pszDirectory, DWORD cchDirectory)
{
    if (pszDirectory == NULL || cchDirectory == 0)
        return false;

    pszDirectory[0] = _T('\0');

    if (!g_bM64PInitialized || ConfigGetUserDataPath == NULL)
        return false;

    const char *pszDataDir = ConfigGetUserDataPath();
    if (pszDataDir == NULL || *pszDataDir == '\0')
        return false;

#ifdef _UNICODE
    int iLen = MultiByteToWideChar(CP_UTF8, 0, pszDataDir, -1, pszDirectory, (int)cchDirectory);
    if (iLen <= 0)
        return false;
    // iLen includes the terminating NUL
    iLen -= 1;
#else
    int iLen = (int)strlen(pszDataDir);
    if ((DWORD)iLen + 1 > cchDirectory)
        return false;
    lstrcpynA(pszDirectory, pszDataDir, (int)cchDirectory);
#endif

    if (iLen > 0 && pszDirectory[iLen - 1] != _T('\\'))
    {
        if ((DWORD)iLen + 2 > cchDirectory)
            return false;
        pszDirectory[iLen] = _T('\\');
        pszDirectory[iLen + 1] = _T('\0');
    }
    return true;
}

// Recursively creates "<dir>\NRage" under the core's user data path.
void EnsureNRageDataDirectory(void)
{
    TCHAR szDataDir[MAX_PATH + 1];
    if (!GetM64PUserDataDirectory(szDataDir, ARRAYSIZE(szDataDir)))
        return;

    TCHAR szPath[MAX_PATH + 1];
    _tcsncpy(szPath, szDataDir, ARRAYSIZE(szPath) - 1);
    szPath[ARRAYSIZE(szPath) - 1] = _T('\0');
    _tcsncat(szPath, _T("NRage"), ARRAYSIZE(szPath) - _tcslen(szPath) - 1);
    CreateDirectory(szPath, NULL); // succeeds silently if it already exists
}

// ---------------------------------------------------------------------------
// Mupen64Plus plugin API
// ---------------------------------------------------------------------------

/******************************************************************
  Function: PluginGetVersion
  Purpose:  This function tells the emulator the type of plugin,
            its name, and the plugin interface version it implements.
  output:   M64ERR_SUCCESS on success
*******************************************************************/
EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion,
                                        int *APIVersion, const char **PluginNamePtr,
                                        int *Capabilities)
{
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_INPUT;

    if (PluginVersion != NULL)
        *PluginVersion = NRAGE_PLUGIN_VERSION;

    if (APIVersion != NULL)
        *APIVersion = INPUT_PLUGIN_API_VERSION;

    if (PluginNamePtr != NULL)
        *PluginNamePtr = STRING_PLUGINNAME;

    if (Capabilities != NULL)
        *Capabilities = 0;

    return M64ERR_SUCCESS;
}

/******************************************************************
  Function: PluginStartup
  Purpose:  Called when the plugin is loaded by the front-end.  We use
            it to hook the Core configuration/command API and to read
            our settings from the core's config file.
  input:    CoreLibHandle: handle of the mupen64plus core library
            Context: front-end context for the debug callback
            DebugCallback: callback for debug messages
  output:   M64ERR_SUCCESS / error code
*******************************************************************/
EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    if (g_bM64PInitialized)
        return M64ERR_ALREADY_INIT;

    g_pDebugCallback = DebugCallback;
    g_pDebugContext = Context;

    if (CoreLibHandle == NULL)
    {
        M64PDebugMessage(M64MSG_ERROR, "PluginStartup: no core library handle");
        return M64ERR_INPUT_ASSERT;
    }

    // Hook CoreGetAPIVersions and check the Config API for compatibility.
    CoreGetAPIVersions = (ptr_CoreGetAPIVersions)GetProcAddress((HMODULE)CoreLibHandle, "CoreGetAPIVersions");
    if (CoreGetAPIVersions == NULL)
    {
        M64PDebugMessage(M64MSG_ERROR, "Core emulator broken; no CoreGetAPIVersions() function found.");
        return M64ERR_INCOMPATIBLE;
    }

    int ConfigAPIVersion = 0, DebugAPIVersion = 0, VidextAPIVersion = 0;
    (*CoreGetAPIVersions)(&ConfigAPIVersion, &DebugAPIVersion, &VidextAPIVersion, NULL);
    if ((ConfigAPIVersion & 0xffff0000) != (CONFIG_API_VERSION & 0xffff0000) || ConfigAPIVersion < 0x020000)
    {
        M64PDebugMessage(M64MSG_ERROR, "Emulator core Config API (v%i.%i.%i) incompatible with plugin",
                         (ConfigAPIVersion >> 16) & 0xffff, (ConfigAPIVersion >> 8) & 0xff, ConfigAPIVersion & 0xff);
        return M64ERR_INCOMPATIBLE;
    }

    // Hook everything we need from the core.
    CoreDoCommand = (ptr_CoreDoCommand)GetProcAddress((HMODULE)CoreLibHandle, "CoreDoCommand");
    ConfigOpenSection = (ptr_ConfigOpenSection)GetProcAddress((HMODULE)CoreLibHandle, "ConfigOpenSection");
    ConfigDeleteSection = (ptr_ConfigDeleteSection)GetProcAddress((HMODULE)CoreLibHandle, "ConfigDeleteSection");
    ConfigSetParameter = (ptr_ConfigSetParameter)GetProcAddress((HMODULE)CoreLibHandle, "ConfigSetParameter");
    ConfigGetParameter = (ptr_ConfigGetParameter)GetProcAddress((HMODULE)CoreLibHandle, "ConfigGetParameter");
    ConfigSetDefaultInt = (ptr_ConfigSetDefaultInt)GetProcAddress((HMODULE)CoreLibHandle, "ConfigSetDefaultInt");
    ConfigSetDefaultBool = (ptr_ConfigSetDefaultBool)GetProcAddress((HMODULE)CoreLibHandle, "ConfigSetDefaultBool");
    ConfigSetDefaultString = (ptr_ConfigSetDefaultString)GetProcAddress((HMODULE)CoreLibHandle, "ConfigSetDefaultString");
    ConfigGetParamInt = (ptr_ConfigGetParamInt)GetProcAddress((HMODULE)CoreLibHandle, "ConfigGetParamInt");
    ConfigGetParamBool = (ptr_ConfigGetParamBool)GetProcAddress((HMODULE)CoreLibHandle, "ConfigGetParamBool");
    ConfigGetParamString = (ptr_ConfigGetParamString)GetProcAddress((HMODULE)CoreLibHandle, "ConfigGetParamString");
    ConfigSaveSection = (ptr_ConfigSaveSection)GetProcAddress((HMODULE)CoreLibHandle, "ConfigSaveSection");
    ConfigGetSharedDataFilepath = (ptr_ConfigGetSharedDataFilepath)GetProcAddress((HMODULE)CoreLibHandle, "ConfigGetSharedDataFilepath");
    ConfigGetUserConfigPath = (ptr_ConfigGetUserConfigPath)GetProcAddress((HMODULE)CoreLibHandle, "ConfigGetUserConfigPath");
    ConfigGetUserDataPath = (ptr_ConfigGetUserDataPath)GetProcAddress((HMODULE)CoreLibHandle, "ConfigGetUserDataPath");
    ConfigGetUserCachePath = (ptr_ConfigGetUserCachePath)GetProcAddress((HMODULE)CoreLibHandle, "ConfigGetUserCachePath");

    if (!ConfigOpenSection || !ConfigSetParameter || !ConfigGetParameter ||
        !ConfigSetDefaultInt || !ConfigSetDefaultBool || !ConfigSetDefaultString ||
        !ConfigGetParamInt   || !ConfigGetParamBool   || !ConfigGetParamString ||
        !ConfigSaveSection)
    {
        M64PDebugMessage(M64MSG_ERROR, "Couldn't connect to Core configuration functions");
        return M64ERR_INCOMPATIBLE;
    }
    // Optional hooks (CoreDoCommand / path getters) are allowed to be absent
    // in exotic front-ends; we simply degrade the matching functionality.

    g_bM64PInitialized = true;

    // Prepare <UserData>\NRage for plugin-relative files.
    EnsureNRageDataDirectory();

    // Language selection: in the Zilmar build the language DLL was loaded in
    // DllMain (reading an INI); with Mupen64Plus the language lives in the
    // core config, so it can only be resolved here.
#ifdef _UNICODE
    {
        LANGID lang = GetLanguageFromM64P();
        if (lang == 0)
        {
            g_strEmuInfo.Language = DetectLanguage();
            M64PDebugMessage(M64MSG_INFO, "Autoselect language: %d", g_strEmuInfo.Language);
        }
        else
            g_strEmuInfo.Language = lang;

        HMODULE hResource = LoadLanguageDLL(g_strEmuInfo.Language);
        if (hResource != NULL)
            g_hResourceDLL = hResource;
        else
        {
            g_strEmuInfo.Language = 0;
            g_hResourceDLL = g_strEmuInfo.hinst;
            M64PDebugMessage(M64MSG_INFO, "couldn't load language DLL, falling back to defaults");
        }
    }
#endif

    M64PDebugMessage(M64MSG_INFO, STRING_PLUGINNAME " " VERSIONNUMBER " (Mupen64Plus port) started");

    return M64ERR_SUCCESS;
}

/******************************************************************
  Function: PluginShutdown
  Purpose:  Called when the front-end unloads the plugin (or switches
            to a different one).  Releases everything we own.
  output:   M64ERR_SUCCESS / error code
*******************************************************************/
EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!g_bM64PInitialized)
        return M64ERR_NOT_INIT;

    // Same cleanup the Zilmar spec performed in CloseDLL().
    if (g_bRunning)
        RomClosed();

    for (int i = 0; i < 4; i++)
    {
        freePakData(&g_pcControllers[i]);
        freeModifiers(&g_pcControllers[i]);
        SetControllerDefaults(&g_pcControllers[i]);
    }

    FreeDirectInput();

    // Release the language DLL if one was loaded.
    if (g_hResourceDLL != NULL && g_hResourceDLL != g_strEmuInfo.hinst)
    {
        FreeLibrary(g_hResourceDLL);
        g_hResourceDLL = g_strEmuInfo.hinst;
    }
    g_strEmuInfo.Language = 0;

    g_strEmuInfo.fInitialisedPlugin = false;
    g_bRunning = false;

    // Drop the core hooks.
    CoreGetAPIVersions = NULL;
    CoreDoCommand = NULL;
    ConfigOpenSection = NULL;
    ConfigDeleteSection = NULL;
    ConfigSetParameter = NULL;
    ConfigGetParameter = NULL;
    ConfigSetDefaultInt = NULL;
    ConfigSetDefaultBool = NULL;
    ConfigSetDefaultString = NULL;
    ConfigGetParamInt = NULL;
    ConfigGetParamBool = NULL;
    ConfigGetParamString = NULL;
    ConfigSaveSection = NULL;
    ConfigGetSharedDataFilepath = NULL;
    ConfigGetUserConfigPath = NULL;
    ConfigGetUserDataPath = NULL;
    ConfigGetUserCachePath = NULL;

    g_pDebugCallback = NULL;
    g_pDebugContext = NULL;
    g_hEmulatorWindow = NULL;

    g_bM64PInitialized = false;
    return M64ERR_SUCCESS;
}

/******************************************************************
  Function: PluginConfig
  Purpose:  Optional export (mupen64plus-core PR #774).  RMG calls it
            from Settings -> Input -> Configure; 'parent' is a
            frontend-owned pointer (a QWidget* in RMG) and must not be
            treated as an HWND.  For our Win32 dialog we use the active
            window of the calling thread instead.
  output:   M64ERR_SUCCESS / error code
*******************************************************************/
EXPORT m64p_error CALL PluginConfig(void *parent)
{
    UNREFERENCED_PARAMETER(parent); // QWidget* in RMG -- not an HWND

    if (!g_bM64PInitialized)
        return M64ERR_NOT_INIT;

    // On the Qt GUI thread GetActiveWindow() returns the front-end's main
    // window, which is a perfectly good dialog parent.
    HWND hParent = GetActiveWindow();
    if (hParent == NULL)
        hParent = GetForegroundWindow();

    SetEmulatorWindowHandle(hParent);
    g_strEmuInfo.hMainWindow = hParent;

    // Behave exactly like the old DllConfig().
    OpenConfigurationDialog(hParent);

    return M64ERR_SUCCESS;
}
