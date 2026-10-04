/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* Configuration Dialog (Windows only)                                       *
*                                                                           *
* License: GNU/GPLv2                                                        *
*                                                                           *
****************************************************************************/
#pragma once

/* This entire header is Windows-only */
#ifdef _WIN32

/* windows.h must already be included before this header.
   We use a forward declaration-style to avoid double-include issues. */
#ifndef _WINDOWS_
#  include <windows.h>
#endif

/* Call from PluginConfig(). Returns true if user pressed OK. */
bool ShowConfigDialog(HINSTANCE hInst, HWND hParent);

/* DLL instance handle — set once in DllMain */
extern HINSTANCE g_hDllInst;

#endif /* _WIN32 */
