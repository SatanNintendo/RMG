/*
        N-Rage`s Dinput8 Plugin -- Mupen64Plus configuration storage
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

// This file replaces the old NRage.ini load/store code.  Everything the
// plugin persists now lives in the Mupen64Plus configuration file, in three
// families of sections:
//
//   [NRage]            Version, Language, ShowMessages, default folders and
//                      "last browsed" directories
//   [NRage-Input1..4]  one section per N64 controller: settings, FF device,
//                      button assignments ("offset,axisID,btnType,{guid}")
//                      and modifiers ("offset,axisID,btnType,{guid},modType,
//                      toggle,status,specific")
//   [NRage-Shortcuts]  shortcut button assignments, "PlayerN_ButtonM" and
//                      "MouseLock"
//
// The legacy .cpf/.sc profile files (and the embedded default profiles)
// keep their original text format and are loaded through the untouched
// ParseLine/ProcessKey machinery in FileAccess.cpp.

#include "commonIncludes.h"
#include "MupenPlusAPI.h"
#include "M64PConfig.h"
#include "NRagePluginV2.h"
#include "Interface.h"
#include "FileAccess.h"
#include "DirectInput.h"
#include "PakIO.h"
#include "Debug.h"

#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// Splits a comma separated list in place.  Returns the number of fields
// found (never more than iMaxFields).  Empty trailing fields are dropped.
static int SplitCSV(char *pszFields[], int iMaxFields, char *pszText)
{
    int iCount = 0;
    char *pszPos = pszText;

    while (iCount < iMaxFields)
    {
        pszFields[iCount++] = pszPos;

        char *pszComma = strchr(pszPos, ',');
        if (pszComma == NULL)
            break;
        *pszComma = '\0';
        pszPos = pszComma + 1;
    }
    return iCount;
}

// Serializes one button assignment: "offset,axisID,btnType,{guid}".
// The GUID is the guidInstance of the DirectInput device the button came
// from; it is empty when the assignment has no (live) device, which keeps
// the original "assigned but currently unavailable" behaviour.
static void SerializeButton(char *pszBuffer, size_t cbBuffer, const BUTTON &btnButton)
{
    char szGUID[GUID_STRINGLENGTH + 2];
    szGUID[0] = '\0';
    if (btnButton.parentDevice != NULL)
        GUIDtoStringA(szGUID, btnButton.parentDevice->guidInstance);

    _snprintf(pszBuffer, cbBuffer - 1, "%u,%u,%u,%s",
              (unsigned)btnButton.bOffset, (unsigned)btnButton.bAxisID,
              (unsigned)btnButton.bBtnType, szGUID);
    pszBuffer[cbBuffer - 1] = '\0';
}

// Counterpart of SerializeButton.  Resolves the parent device through
// g_devList (the system mouse is special: it is not part of the list).
static bool DeserializeButton(const char *pszValue, BUTTON &btnButton, int *piDeviceMissing)
{
    char szValue[128];
    ZeroMemory(&btnButton, sizeof(btnButton));
    if (piDeviceMissing != NULL)
        *piDeviceMissing = 0;

    if (pszValue == NULL)
        return false;
    strncpy(szValue, pszValue, sizeof(szValue) - 1);
    szValue[sizeof(szValue) - 1] = '\0';

    char *apszFields[4];
    int iFields = SplitCSV(apszFields, 4, szValue);
    if (iFields < 3)
        return false;

    btnButton.bOffset = (BYTE)atoi(apszFields[0]);
    btnButton.bAxisID = (BYTE)atoi(apszFields[1]);
    btnButton.bBtnType = (BYTE)atoi(apszFields[2]);

    if (iFields >= 4 && apszFields[3][0] != '\0')
    {
        GUID guid;
        if (StringtoGUIDA(&guid, apszFields[3]))
        {
            if (IsEqualGUID(g_sysMouse.guidInstance, guid))
            {
                btnButton.parentDevice = &g_sysMouse;
            }
            else
            {
                int iDevice = FindDeviceinList(guid);
                if (iDevice != -1)
                    btnButton.parentDevice = &g_devList[iDevice];
                else if (piDeviceMissing != NULL)
                    *piDeviceMissing = 1;   // device currently unplugged
            }
        }
    }

    return true;
}

// Serializes one modifier:
// "offset,axisID,btnType,{guid},modType,toggle,status,specific"
static void SerializeModifier(char *pszBuffer, size_t cbBuffer, const MODIFIER &modModifier)
{
    char szGUID[GUID_STRINGLENGTH + 2];
    szGUID[0] = '\0';
    if (modModifier.btnButton.parentDevice != NULL)
        GUIDtoStringA(szGUID, modModifier.btnButton.parentDevice->guidInstance);

    _snprintf(pszBuffer, cbBuffer - 1, "%u,%u,%u,%s,%u,%u,%u,%u",
              (unsigned)modModifier.btnButton.bOffset,
              (unsigned)modModifier.btnButton.bAxisID,
              (unsigned)modModifier.btnButton.bBtnType,
              szGUID,
              (unsigned)modModifier.bModType,
              (unsigned)modModifier.fToggle,
              (unsigned)modModifier.fStatus,
              (unsigned)modModifier.dwSpecific);
    pszBuffer[cbBuffer - 1] = '\0';
}

static bool DeserializeModifier(const char *pszValue, MODIFIER &modModifier)
{
    char szValue[160];
    ZeroMemory(&modModifier, sizeof(modModifier));

    if (pszValue == NULL)
        return false;
    strncpy(szValue, pszValue, sizeof(szValue) - 1);
    szValue[sizeof(szValue) - 1] = '\0';

    char *apszFields[8];
    int iFields = SplitCSV(apszFields, 8, szValue);
    if (iFields < 8)
        return false;

    modModifier.btnButton.bOffset = (BYTE)atoi(apszFields[0]);
    modModifier.btnButton.bAxisID = (BYTE)atoi(apszFields[1]);
    modModifier.btnButton.bBtnType = (BYTE)atoi(apszFields[2]);

    if (apszFields[3][0] != '\0')
    {
        GUID guid;
        if (StringtoGUIDA(&guid, apszFields[3]))
        {
            if (IsEqualGUID(g_sysMouse.guidInstance, guid))
                modModifier.btnButton.parentDevice = &g_sysMouse;
            else
            {
                int iDevice = FindDeviceinList(guid);
                if (iDevice != -1)
                    modModifier.btnButton.parentDevice = &g_devList[iDevice];
            }
        }
    }

    modModifier.bModType = (BYTE)atoi(apszFields[4]);
    modModifier.fToggle = atoi(apszFields[5]);
    modModifier.fStatus = atoi(apszFields[6]);
    modModifier.dwSpecific = (DWORD32)strtoul(apszFields[7], NULL, 10);
    return true;
}

// Reads a string parameter (returns NULL when absent), or an integer one.
static const char *GetStringParam(m64p_handle hSection, const char *pszParam)
{
    if (hSection == NULL || ConfigGetParamString == NULL)
        return NULL;
    return (*ConfigGetParamString)(hSection, pszParam);
}

static int GetIntParam(m64p_handle hSection, const char *pszParam, int iDefault)
{
    int iValue = iDefault;
    if (hSection != NULL && ConfigGetParameter != NULL)
    {
        // Raw query: distinguishes "parameter absent" (M64ERR_INPUT_NOT_FOUND,
        // wrong type, ...) from a genuine value.  The plain ConfigGetParamInt
        // helper would return 0 for absent parameters and silently eat our
        // defaults (StickRange 67, RumbleStrength 80, RawData 1, ...).
        m64p_error err = (*ConfigGetParameter)(hSection, pszParam, M64TYPE_INT, &iValue, sizeof(iValue));
        if (err != M64ERR_SUCCESS)
            iValue = iDefault;
    }
    else if (hSection != NULL && ConfigGetParamInt != NULL)
        iValue = (*ConfigGetParamInt)(hSection, pszParam);
    return iValue;
}

// Writes a string parameter; empty strings are still written so that a
// round trip keeps the parameter list stable.
static void SetStringParam(m64p_handle hSection, const char *pszParam, const char *pszValue)
{
    if (hSection != NULL && ConfigSetParameter != NULL)
        (*ConfigSetParameter)(hSection, pszParam, M64TYPE_STRING, pszValue != NULL ? pszValue : "");
}

static void SetIntParam(m64p_handle hSection, const char *pszParam, int iValue)
{
    if (hSection != NULL && ConfigSetParameter != NULL)
        (*ConfigSetParameter)(hSection, pszParam, M64TYPE_INT, &iValue);
}

// TCHAR -> ASCII into a fixed buffer (empty string on conversion failure).
static void TcharToAscii(char *pszOut, size_t cbOut, LPCTSTR pszIn)
{
    pszOut[0] = '\0';
    if (pszIn == NULL)
        return;
#ifdef _UNICODE
    int iLen = WideCharToMultiByte(CP_ACP, 0, pszIn, -1, pszOut, (int)cbOut, NULL, NULL);
    if (iLen <= 0)
        pszOut[0] = '\0';
#else
    strncpy(pszOut, pszIn, cbOut - 1);
    pszOut[cbOut - 1] = '\0';
#endif
}

static void AsciiToTchar(LPTSTR pszOut, size_t cchOut, const char *pszIn)
{
    pszOut[0] = _T('\0');
    if (pszIn == NULL)
        return;
#ifdef _UNICODE
    int iLen = MultiByteToWideChar(CP_ACP, 0, pszIn, -1, pszOut, (int)cchOut);
    if (iLen <= 0)
        pszOut[0] = _T('\0');
#else
    _tcsncpy(pszOut, pszIn, cchOut - 1);
    pszOut[cchOut - 1] = _T('\0');
#endif
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

// Applies one controller section to a CONTROLLER struct.
static void LoadControllerSection(m64p_handle hSection, LPCONTROLLER pcController)
{
    pcController->fPlugged             = GetIntParam(hSection, "Plugged", 0);
    pcController->fXInput              = GetIntParam(hSection, "XInput", 0);
    pcController->fRawData             = GetIntParam(hSection, "RawData", 1);
    pcController->PakType              = GetIntParam(hSection, "PakType", PAK_NONE);
    pcController->bDiagStretch         = GetIntParam(hSection, "RealN64Range", 0);
    pcController->bRapidFireEnabled    = (GetIntParam(hSection, "RapidFireEnabled", 0) != 0);
    pcController->bRapidFireRate       = (BYTE)GetIntParam(hSection, "RapidFireRate", 3);
    pcController->bStickRange          = (BYTE)GetIntParam(hSection, "StickRange", DEFAULT_STICKRANGE);
    pcController->bMouseMoveX          = (unsigned)GetIntParam(hSection, "MouseMoveX", DEFAULT_MOUSEMOVE);
    pcController->bMouseMoveY          = (unsigned)GetIntParam(hSection, "MouseMoveY", DEFAULT_MOUSEMOVE);
    // sanity clamp: a corrupted value >= PF_AXESETS would index past
    // aButton[] when reading the analog axes
    int iAxisSet = GetIntParam(hSection, "AxisSet", 0);
    if (iAxisSet < 0 || iAxisSet >= PF_AXESETS)
        iAxisSet = 0;
    pcController->bAxisSet             = (unsigned)iAxisSet;
    pcController->fKeyAbsoluteX        = (GetIntParam(hSection, "KeyAbsoluteX", 0) != 0);
    pcController->fKeyAbsoluteY        = (GetIntParam(hSection, "KeyAbsoluteY", 0) != 0);
    pcController->bPadDeadZone         = (BYTE)GetIntParam(hSection, "PadDeadZone", DEFAULT_DEADZONE);
    pcController->bPadThreshold        = (BYTE)GetIntParam(hSection, "PadThreshold", DEFAULT_THRESHOLD);
    pcController->wMouseSensitivityX   = (WORD)GetIntParam(hSection, "MouseSensitivityX", DEFAULT_MOUSESENSIVITY);
    pcController->wMouseSensitivityY   = (WORD)GetIntParam(hSection, "MouseSensitivityY", DEFAULT_MOUSESENSIVITY);
    pcController->bRumbleTyp           = (BYTE)GetIntParam(hSection, "RumbleType", DEFAULT_RUMBLETYP);
    pcController->bRumbleStrength      = (BYTE)GetIntParam(hSection, "RumbleStrength", DEFAULT_RUMBLESTRENGTH);
    pcController->fVisualRumble        = (GetIntParam(hSection, "VisualRumble", 0) != 0);

    // rumble device
    pcController->guidFFDevice = GUID_NULL;
    const char *pszFFGUID = GetStringParam(hSection, "FFDeviceGUID");
    if (pszFFGUID != NULL && pszFFGUID[0] != '\0')
        StringtoGUIDA(&pcController->guidFFDevice, pszFFGUID);

    // pak files
    AsciiToTchar(pcController->szMempakFile,   ARRAYSIZE(pcController->szMempakFile),   GetStringParam(hSection, "MemPakFile"));
    AsciiToTchar(pcController->szTransferRom,  ARRAYSIZE(pcController->szTransferRom),  GetStringParam(hSection, "GBRomFile"));
    AsciiToTchar(pcController->szTransferSave, ARRAYSIZE(pcController->szTransferSave), GetStringParam(hSection, "GBRomSave"));

    // buttons
    for (int j = 0; j < (int)ARRAYSIZE(pcController->aButton); j++)
    {
        char szParam[24];
        _snprintf(szParam, sizeof(szParam) - 1, "Button%d", j);
        szParam[sizeof(szParam) - 1] = '\0';

        const char *pszValue = GetStringParam(hSection, szParam);
        if (pszValue == NULL || pszValue[0] == '\0')
            continue;                       // keep default (unassigned)

        int iDeviceMissing = 0;
        DeserializeButton(pszValue, pcController->aButton[j], &iDeviceMissing);
        if (iDeviceMissing)
            DebugWriteA("Button %d references a device that is not attached\n", j);
    }

    // modifiers
    freeModifiers(pcController);
    int nModifiers = GetIntParam(hSection, "ModifierCount", 0);
    if (nModifiers < 0)
        nModifiers = 0;
    if (nModifiers > MAX_MODIFIERS)
        nModifiers = MAX_MODIFIERS;
    if (nModifiers > 0)
    {
        pcController->pModifiers = (LPMODIFIER)P_malloc(sizeof(MODIFIER) * nModifiers);
        if (pcController->pModifiers != NULL)
        {
            ZeroMemory(pcController->pModifiers, sizeof(MODIFIER) * nModifiers);
            pcController->nModifiers = 0;
            for (int j = 0; j < nModifiers; j++)
            {
                char szParam[24];
                _snprintf(szParam, sizeof(szParam) - 1, "Modifier%d", j);
                szParam[sizeof(szParam) - 1] = '\0';

                const char *pszValue = GetStringParam(hSection, szParam);
                if (pszValue == NULL || pszValue[0] == '\0')
                    continue;

                MODIFIER modWorking;
                if (DeserializeModifier(pszValue, modWorking))
                    pcController->pModifiers[pcController->nModifiers++] = modWorking;
            }
        }
    }
}

bool LoadConfigFromM64P(void)
{
    if (!g_bM64PInitialized || ConfigOpenSection == NULL)
        return false;

    m64p_handle hGeneral = NULL;
    if ((*ConfigOpenSection)(M64CFG_SECTION_NRAGE, &hGeneral) != M64ERR_SUCCESS)
        return false;

    // "Version" is our sentinel: when it is missing we are on a fresh
    // install and fall back to the embedded default profiles.
    int iVersion = 0;
    bool bFresh = true;
    if (ConfigGetParameter != NULL)
    {
        if ((*ConfigGetParameter)(hGeneral, "Version", M64TYPE_INT, &iVersion, sizeof(iVersion)) == M64ERR_SUCCESS)
            bFresh = false;
    }

    if (bFresh)
    {
        DebugWriteA("No [NRage] section in core config, using built-in defaults\n");
        for (int i = 0; i < 4; i++)
            LoadProfileFromResource(i, false);
        LoadShortcutsFromResource(false);
        return true;
    }

    // general settings
    g_strEmuInfo.Language       = (LANGID)GetIntParam(hGeneral, "Language", 0);
    g_strEmuInfo.fDisplayShortPop = (GetIntParam(hGeneral, "ShowMessages", 1) != 0);

    // folders
    AsciiToTchar(g_aszDefFolders[DIRECTORY_MEMPAK], MAX_PATH, GetStringParam(hGeneral, "MemPakDir"));
    AsciiToTchar(g_aszDefFolders[DIRECTORY_GBROMS], MAX_PATH, GetStringParam(hGeneral, "GBRomDir"));
    AsciiToTchar(g_aszDefFolders[DIRECTORY_GBSAVES], MAX_PATH, GetStringParam(hGeneral, "GBSaveDir"));

    // "last browsed" directories
    AsciiToTchar(g_aszLastBrowse[BF_MEMPAK],    MAX_PATH, GetStringParam(hGeneral, "BrowseMemPak"));
    AsciiToTchar(g_aszLastBrowse[BF_GBROM],     MAX_PATH, GetStringParam(hGeneral, "BrowseGBRom"));
    AsciiToTchar(g_aszLastBrowse[BF_GBSAVE],    MAX_PATH, GetStringParam(hGeneral, "BrowseGBSave"));
    AsciiToTchar(g_aszLastBrowse[BF_PROFILE],   MAX_PATH, GetStringParam(hGeneral, "BrowseProfile"));
    AsciiToTchar(g_aszLastBrowse[BF_NOTE],      MAX_PATH, GetStringParam(hGeneral, "BrowseNote"));
    AsciiToTchar(g_aszLastBrowse[BF_SHORTCUTS], MAX_PATH, GetStringParam(hGeneral, "BrowseShortcuts"));

    // controllers
    for (int i = 0; i < 4; i++)
    {
        SetControllerDefaults(&g_pcControllers[i]);

        char szSection[32];
        _snprintf(szSection, sizeof(szSection) - 1, M64CFG_SECTION_INPUT "%d", i + 1);
        szSection[sizeof(szSection) - 1] = '\0';

        m64p_handle hController = NULL;
        if ((*ConfigOpenSection)(szSection, &hController) == M64ERR_SUCCESS)
            LoadControllerSection(hController, &g_pcControllers[i]);
    }

    // shortcuts
    {
        m64p_handle hShortcuts = NULL;
        if ((*ConfigOpenSection)(M64CFG_SECTION_SHORTCUTS, &hShortcuts) == M64ERR_SUCCESS)
        {
            ZeroMemory(&g_scShortcuts, sizeof(SHORTCUTS));
            for (int i = 0; i < 4; i++)
            {
                for (int j = 0; j < SC_TOTAL; j++)
                {
                    char szParam[32];
                    _snprintf(szParam, sizeof(szParam) - 1, "Player%d_Button%d", i, j);
                    szParam[sizeof(szParam) - 1] = '\0';

                    const char *pszValue = GetStringParam(hShortcuts, szParam);
                    if (pszValue == NULL || pszValue[0] == '\0')
                        continue;
                    DeserializeButton(pszValue, g_scShortcuts.Player[i].aButtons[j], NULL);
                }
            }

            const char *pszValue = GetStringParam(hShortcuts, "MouseLock");
            if (pszValue != NULL && pszValue[0] != '\0')
                DeserializeButton(pszValue, g_scShortcuts.bMouseLock, NULL);
        }
    }

    DebugWriteA("Configuration loaded from Mupen64Plus core config\n");
    return true;
}

// ---------------------------------------------------------------------------
// Storing (from the config dialog's working copy)
// ---------------------------------------------------------------------------

static void StoreControllerSection(m64p_handle hSection, int i)
{
    LPCONTROLLER pcController = &g_ivConfig->Controllers[i];

    SetIntParam(hSection, "Plugged",           pcController->fPlugged ? 1 : 0);
    SetIntParam(hSection, "XInput",            pcController->fXInput ? 1 : 0);
    SetIntParam(hSection, "RawData",           pcController->fRawData ? 1 : 0);
    SetIntParam(hSection, "PakType",           (int)pcController->PakType);
    SetIntParam(hSection, "RealN64Range",      pcController->bDiagStretch);
    SetIntParam(hSection, "RapidFireEnabled",  pcController->bRapidFireEnabled ? 1 : 0);
    SetIntParam(hSection, "RapidFireRate",     (int)pcController->bRapidFireRate);
    SetIntParam(hSection, "StickRange",        (int)pcController->bStickRange);
    SetIntParam(hSection, "MouseMoveX",        (int)pcController->bMouseMoveX);
    SetIntParam(hSection, "MouseMoveY",        (int)pcController->bMouseMoveY);
    SetIntParam(hSection, "AxisSet",           (int)pcController->bAxisSet);
    SetIntParam(hSection, "KeyAbsoluteX",      pcController->fKeyAbsoluteX ? 1 : 0);
    SetIntParam(hSection, "KeyAbsoluteY",      pcController->fKeyAbsoluteY ? 1 : 0);
    SetIntParam(hSection, "PadDeadZone",       (int)pcController->bPadDeadZone);
    SetIntParam(hSection, "PadThreshold",      (int)pcController->bPadThreshold);
    SetIntParam(hSection, "MouseSensitivityX", (int)pcController->wMouseSensitivityX);
    SetIntParam(hSection, "MouseSensitivityY", (int)pcController->wMouseSensitivityY);
    SetIntParam(hSection, "RumbleType",        (int)pcController->bRumbleTyp);
    SetIntParam(hSection, "RumbleStrength",    (int)pcController->bRumbleStrength);
    SetIntParam(hSection, "VisualRumble",      pcController->fVisualRumble ? 1 : 0);

    // rumble device: the interface works with name + counter, resolve it
    // back to a GUID exactly like the old INI writer did
    char szGUID[GUID_STRINGLENGTH + 2];
    szGUID[0] = '\0';
    int iDevice = FindDeviceinList(g_ivConfig->FFDevices[i].szProductName, g_ivConfig->FFDevices[i].bProductCounter, true);
    if (iDevice != -1 && g_devList[iDevice].bEffType)
    {
        g_ivConfig->Controllers[i].guidFFDevice = g_devList[iDevice].guidInstance;
        GUIDtoStringA(szGUID, g_devList[iDevice].guidInstance);
    }
    else
        g_ivConfig->Controllers[i].guidFFDevice = GUID_NULL;
    SetStringParam(hSection, "FFDeviceGUID", szGUID);

    char szBuffer[MAX_PATH + 80];
    TcharToAscii(szBuffer, sizeof(szBuffer), pcController->szMempakFile);
    SetStringParam(hSection, "MemPakFile", szBuffer);
    TcharToAscii(szBuffer, sizeof(szBuffer), pcController->szTransferRom);
    SetStringParam(hSection, "GBRomFile", szBuffer);
    TcharToAscii(szBuffer, sizeof(szBuffer), pcController->szTransferSave);
    SetStringParam(hSection, "GBRomSave", szBuffer);

    // buttons
    for (int j = 0; j < (int)ARRAYSIZE(pcController->aButton); j++)
    {
        char szParam[24];
        _snprintf(szParam, sizeof(szParam) - 1, "Button%d", j);
        szParam[sizeof(szParam) - 1] = '\0';

        SerializeButton(szBuffer, sizeof(szBuffer), pcController->aButton[j]);
        SetStringParam(hSection, szParam, szBuffer);
    }

    // modifiers
    SetIntParam(hSection, "ModifierCount", (int)pcController->nModifiers);
    for (int j = 0; j < (int)pcController->nModifiers && j < MAX_MODIFIERS; j++)
    {
        char szParam[24];
        _snprintf(szParam, sizeof(szParam) - 1, "Modifier%d", j);
        szParam[sizeof(szParam) - 1] = '\0';

        SerializeModifier(szBuffer, sizeof(szBuffer), pcController->pModifiers[j]);
        SetStringParam(hSection, szParam, szBuffer);
    }
}

bool StoreConfigToM64P(void)
{
    if (!g_bM64PInitialized || ConfigOpenSection == NULL)
        return false;
    if (g_ivConfig == NULL)
        return false;

    // general + folders + browser dirs
    m64p_handle hGeneral = NULL;
    if ((*ConfigOpenSection)(M64CFG_SECTION_NRAGE, &hGeneral) != M64ERR_SUCCESS)
        return false;

    SetIntParam(hGeneral, "Version", NRAGE_CONFIG_VERSION);
    SetIntParam(hGeneral, "Language", (int)g_ivConfig->Language);
    SetIntParam(hGeneral, "ShowMessages", g_ivConfig->fDisplayShortPop ? 1 : 0);

    char szBuffer[MAX_PATH + 80];
    TcharToAscii(szBuffer, sizeof(szBuffer), g_aszDefFolders[DIRECTORY_MEMPAK]);
    SetStringParam(hGeneral, "MemPakDir", szBuffer);
    TcharToAscii(szBuffer, sizeof(szBuffer), g_aszDefFolders[DIRECTORY_GBROMS]);
    SetStringParam(hGeneral, "GBRomDir", szBuffer);
    TcharToAscii(szBuffer, sizeof(szBuffer), g_aszDefFolders[DIRECTORY_GBSAVES]);
    SetStringParam(hGeneral, "GBSaveDir", szBuffer);

    TcharToAscii(szBuffer, sizeof(szBuffer), g_aszLastBrowse[BF_MEMPAK]);
    SetStringParam(hGeneral, "BrowseMemPak", szBuffer);
    TcharToAscii(szBuffer, sizeof(szBuffer), g_aszLastBrowse[BF_GBROM]);
    SetStringParam(hGeneral, "BrowseGBRom", szBuffer);
    TcharToAscii(szBuffer, sizeof(szBuffer), g_aszLastBrowse[BF_GBSAVE]);
    SetStringParam(hGeneral, "BrowseGBSave", szBuffer);
    TcharToAscii(szBuffer, sizeof(szBuffer), g_aszLastBrowse[BF_PROFILE]);
    SetStringParam(hGeneral, "BrowseProfile", szBuffer);
    TcharToAscii(szBuffer, sizeof(szBuffer), g_aszLastBrowse[BF_NOTE]);
    SetStringParam(hGeneral, "BrowseNote", szBuffer);
    TcharToAscii(szBuffer, sizeof(szBuffer), g_aszLastBrowse[BF_SHORTCUTS]);
    SetStringParam(hGeneral, "BrowseShortcuts", szBuffer);

    // controllers
    for (int i = 0; i < 4; i++)
    {
        char szSection[32];
        _snprintf(szSection, sizeof(szSection) - 1, M64CFG_SECTION_INPUT "%d", i + 1);
        szSection[sizeof(szSection) - 1] = '\0';

        m64p_handle hController = NULL;
        if ((*ConfigOpenSection)(szSection, &hController) != M64ERR_SUCCESS)
            return false;
        StoreControllerSection(hController, i);
    }

    // shortcuts
    m64p_handle hShortcuts = NULL;
    if ((*ConfigOpenSection)(M64CFG_SECTION_SHORTCUTS, &hShortcuts) != M64ERR_SUCCESS)
        return false;

    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < SC_TOTAL; j++)
        {
            char szParam[32];
            _snprintf(szParam, sizeof(szParam) - 1, "Player%d_Button%d", i, j);
            szParam[sizeof(szParam) - 1] = '\0';

            SerializeButton(szBuffer, sizeof(szBuffer), g_ivConfig->Shortcuts.Player[i].aButtons[j]);
            SetStringParam(hShortcuts, szParam, szBuffer);
        }
    }
    SerializeButton(szBuffer, sizeof(szBuffer), g_ivConfig->Shortcuts.bMouseLock);
    SetStringParam(hShortcuts, "MouseLock", szBuffer);

    // persist our sections
    if (ConfigSaveSection != NULL)
    {
        (*ConfigSaveSection)(M64CFG_SECTION_NRAGE);
        for (int i = 0; i < 4; i++)
        {
            char szSection[32];
            _snprintf(szSection, sizeof(szSection) - 1, M64CFG_SECTION_INPUT "%d", i + 1);
            szSection[sizeof(szSection) - 1] = '\0';
            (*ConfigSaveSection)(szSection);
        }
        (*ConfigSaveSection)(M64CFG_SECTION_SHORTCUTS);
    }

    DebugWriteA("Configuration stored to Mupen64Plus core config\n");
    return true;
}

// ---------------------------------------------------------------------------

LANGID GetLanguageFromM64P(void)
{
    if (!g_bM64PInitialized || ConfigOpenSection == NULL || ConfigGetParamInt == NULL)
        return 0;

    m64p_handle hGeneral = NULL;
    if ((*ConfigOpenSection)(M64CFG_SECTION_NRAGE, &hGeneral) != M64ERR_SUCCESS)
        return 0;

    // Reading defaults through ConfigSetDefaultInt also *writes* the default
    // into a potentially fresh section, so query the raw parameter instead.
    // (ConfigGetParameter with M64TYPE_INT always writes an int-sized value.)
    int iLanguage = 0;
    if (ConfigGetParameter != NULL)
    {
        if ((*ConfigGetParameter)(hGeneral, "Language", M64TYPE_INT, &iLanguage, sizeof(iLanguage)) != M64ERR_SUCCESS)
            return 0;
    }
    return (LANGID)iLanguage;
}
