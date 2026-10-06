/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* Configuration Dialog (Windows only)                                       *
*                                                                           *
* Two-tab dialog matching the PJ64 AziAudio look:                           *
*   [Settings]  — Output Device, Backend Sound Driver, Volume slider+Mute   *
*   [Advanced]  — Buffer FPS / Backend FPS / Buffers sliders,               *
*                 Prevent Buffer Overruns, Force Old Audio Sync,             *
*                 Emulate Audio Interface checkbox,                          *
*                 Frequency dropdown, Disallow Thread Yielding DS8/XA2       *
*                                                                           *
* Layout is built entirely with Win32 API (no MFC).                         *
* A small manifest resource enables Windows common-controls v6 styling;    *
* the dialog itself is still created dynamically without an .rc dialog.    *
*                                                                           *
* License: GNU/GPLv2                                                        *
*                                                                           *
****************************************************************************/

#ifdef _WIN32

#include "ConfigDialog.h"
#include "Configuration.h"
#include "SoundDriverFactory.h"
#include "common.h"

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <wchar.h>
#include <new>

/* ── Link commctrl for TrackBar / Tab controls ──────────────────────────── */
/* comctl32 is linked via -lcomctl32 in Makefile (MinGW) or pragma below (MSVC) */
#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#endif

/* g_hDllInst is defined in main.cpp (declared extern in ConfigDialog.h) */

/* ── Control IDs ────────────────────────────────────────────────────────── */
#define IDC_TAB             1000

/* Settings tab */
#define IDC_OUTPUT_DEVICE   1010
#define IDC_DRIVER_COMBO    1011
#define IDC_VOLUME_SLIDER   1012
#define IDC_MUTE_CHECK      1013
#define IDC_VOLUME_LABEL    1014

/* Advanced tab */
#define IDC_PREVENT_OVERRUN 1020
#define IDC_FORCE_SYNC      1021
#define IDC_BUFFERFPS_SLIDE 1022
#define IDC_BACKFPS_SLIDE   1023
#define IDC_BUFFERS_SLIDE   1024
#define IDC_BUFFERFPS_VAL   1025
#define IDC_BACKFPS_VAL     1026
#define IDC_BUFFERS_VAL     1027
#define IDC_FREQ_VAL         1028
#define IDC_FREQ_COMBO      1029
#define IDC_DISALLOW_DS8    1030
#define IDC_DISALLOW_XA2    1031

/* Buttons */
#define IDC_OK              1100
#define IDC_CANCEL          1101
#define IDC_APPLY           1102

/* ── Dialog state carried between WndProc calls ─────────────────────────── */
struct DlgState
{
    /* Settings tab controls */
    HWND hTab;
    HWND hSettingsPanel;
    HWND hAdvancedPanel;

    /* Current values (edited by user, not yet committed) */
    int  volume;          /* 0-100          */
    bool mute;
    SoundDriverType driver;

    bool preventOverrun;  /* == !getSyncAudio (inverted for UI) */
    bool forceSync;
    int  bufferFPS;       /* 30-120         */
    int  backendFPS;      /* 30-120         */
    int  buffers;         /* 1-8            */
    int  defaultFrequency;  /* 0 = auto, else 32000/44100/48000 */
    bool disallowDS8;
    bool disallowXA2;

    int  currentTab;      /* 0=Settings 1=Advanced */
};

/* ── RMG language bridge ───────────────────────────────────────────────── */
/* RMG passes the selected GUI locale through RMG_LANGUAGE while this modal
   configuration dialog is open.  When the setting is "System Default" the
   variable is empty, so we fall back to the Windows UI language. */
static bool RmgIsRussian(void)
{
    const char *language = getenv("RMG_LANGUAGE");
    if (language != NULL && language[0] != '\0')
        return _strnicmp(language, "ru", 2) == 0;

    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN;
}

static const wchar_t *AzText(const wchar_t *english, const wchar_t *russian)
{
    return RmgIsRussian() ? russian : english;
}

static std::wstring AnsiToWide(const char *text)
{
    if (text == NULL || text[0] == '\0')
        return std::wstring();

    int length = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    if (length <= 1)
        return std::wstring();

    std::wstring result((size_t)length, L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, text, -1, &result[0], length) <= 0)
        return std::wstring();

    result.resize((size_t)length - 1);
    return result;
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Windows dialog templates are expressed in dialog units (DLU), while the
   dynamically-created controls below are normal pixel-based windows. The
   original version mixed the two coordinate systems, which is why the dialog
   grew with DPI while the controls remained tiny and started overlapping.
   Use the dialog's own font/base units for every position and size. */
static int DluX(HWND hDlg, int value)
{
    RECT r = { 0, 0, value, 0 };
    MapDialogRect(hDlg, &r);
    return r.right;
}

static int DluY(HWND hDlg, int value)
{
    RECT r = { 0, 0, 0, value };
    MapDialogRect(hDlg, &r);
    return r.bottom;
}

static void SetCtrlFont(HWND hwnd, HFONT hFont)
{
    if (hwnd && hFont)
        SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
}

static HWND CreateGroupBox(HWND parent, const wchar_t *text,
                           int x, int y, int w, int hgt,
                           HFONT hFont)
{
    HWND h = CreateWindowW(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        x, y, w, hgt, parent, NULL, g_hDllInst, NULL);
    SetCtrlFont(h, hFont);
    return h;
}

static HWND CreateLabel(HWND parent, const wchar_t *text,
                        int x, int y, int w, int hgt,
                        HFONT hFont)
{
    HWND h = CreateWindowW(L"STATIC", text,
        WS_CHILD | WS_VISIBLE,
        x, y, w, hgt, parent, NULL, g_hDllInst, NULL);
    SetCtrlFont(h, hFont);
    return h;
}

/* ── Create the "Settings" panel window (child of dialog) ─────────────── */
static HWND CreateSettingsPanel(HWND hDlg, RECT *rc, DlgState *st, HFONT hFont)
{
    HWND hPanel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDFRAME,
        rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
        hDlg, NULL, g_hDllInst, NULL);
    SetCtrlFont(hPanel, hFont);

    /* Layout matches the good reference screenshot:
       - two stacked labeled combo groups on the left
       - a dedicated narrow Volume group on the right
       - warning text below the backend selector
       Everything is in dialog units, so DPI scaling is automatic. */
    const int left = DluX(hDlg, 10);
    const int top  = DluY(hDlg, 8);
    const int leftW = DluX(hDlg, 205);
    const int rightX = DluX(hDlg, 224);
    const int volumeW = DluX(hDlg, 64);

    CreateGroupBox(hPanel, AzText(L"Output Device", L"Устройство вывода"),
                   left, top, leftW, DluY(hDlg, 40), hFont);

    HWND hDev = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        left + DluX(hDlg, 8), top + DluY(hDlg, 12),
        DluX(hDlg, 185), DluY(hDlg, 90),
        hPanel, (HMENU)IDC_OUTPUT_DEVICE, g_hDllInst, NULL);
    SetCtrlFont(hDev, hFont);
    SendMessageW(hDev, CB_ADDSTRING, 0, (LPARAM)AzText(L"Default", L"По умолчанию"));
    SendMessageW(hDev, CB_SETCURSEL, 0, 0);

    CreateGroupBox(hPanel, AzText(L"Backend Sound Driver", L"Звуковой драйвер"),
                   left, top + DluY(hDlg, 43), leftW, DluY(hDlg, 43), hFont);

    HWND hDrv = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        left + DluX(hDlg, 8), top + DluY(hDlg, 55),
        DluX(hDlg, 185), DluY(hDlg, 120),
        hPanel, (HMENU)IDC_DRIVER_COMBO, g_hDllInst, NULL);
    SetCtrlFont(hDrv, hFont);

    SoundDriverType drivers[32];
    int nDrivers = SoundDriverFactory::EnumDrivers(drivers, 32);
    int selIdx = 0;
    for (int i = 0; i < nDrivers; i++)
    {
        const char *desc = SoundDriverFactory::GetDriverDescription(drivers[i]);
        const std::wstring wDesc = AnsiToWide(desc);
        int idx = (int)SendMessageW(hDrv, CB_ADDSTRING, 0, (LPARAM)wDesc.c_str());
        SendMessageW(hDrv, CB_SETITEMDATA, idx, (LPARAM)drivers[i]);
        if (drivers[i] == st->driver)
            selIdx = idx;
    }
    SendMessageW(hDrv, CB_SETCURSEL, selIdx, 0);

    /* The warning sits below the two combo groups, just like the reference,
       with enough width to keep both lines intact. */
    HWND hCaution = CreateWindowW(L"STATIC",
        AzText(L"Caution: Changing waveOut driver volume\r\n"
               L"changes application volume for all drivers.",
               L"Внимание: изменение громкости драйвера waveOut\r\n"
               L"изменяет громкость приложения для всех драйверов."),
        WS_CHILD | WS_VISIBLE,
        left + DluX(hDlg, 8), top + DluY(hDlg, 90),
        DluX(hDlg, 205), DluY(hDlg, 26),
        hPanel, NULL, g_hDllInst, NULL);
    SetCtrlFont(hCaution, hFont);

    /* Volume */
    CreateGroupBox(hPanel, AzText(L"Volume", L"Громкость"), rightX, top,
                   volumeW, DluY(hDlg, 108), hFont);

    HWND hVol = CreateWindowW(TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_VERT | TBS_AUTOTICKS | WS_TABSTOP,
        rightX + DluX(hDlg, 15), top + DluY(hDlg, 13),
        DluX(hDlg, 30), DluY(hDlg, 70),
        hPanel, (HMENU)IDC_VOLUME_SLIDER, g_hDllInst, NULL);
    SetCtrlFont(hVol, hFont);
    SendMessage(hVol, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(hVol, TBM_SETPOS, TRUE, 100 - st->volume);
    SendMessage(hVol, TBM_SETTICFREQ, 10, 0);

    HWND hMute = CreateWindowW(L"BUTTON", AzText(L"Mute", L"Без звука"),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        rightX + DluX(hDlg, 8), top + DluY(hDlg, 86),
        DluX(hDlg, 50), DluY(hDlg, 12),
        hPanel, (HMENU)IDC_MUTE_CHECK, g_hDllInst, NULL);
    SetCtrlFont(hMute, hFont);
    SendMessage(hMute, BM_SETCHECK, st->mute ? BST_CHECKED : BST_UNCHECKED, 0);

    return hPanel;
}

/* ── Create the "Advanced" panel window ─────────────────────────────────── */
/* Trackbar notifications (WM_HSCROLL/WM_VSCROLL) are delivered to the
   immediate parent of the trackbar.  The advanced sliders live inside this
   child panel, so the dialog WndProc never sees those notifications unless
   the panel forwards them.  Subclass the panel and relay slider messages to
   the owning dialog so the numeric values can update while dragging. */
static LRESULT CALLBACK AdvancedPanelProc(HWND hPanel, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_HSCROLL || msg == WM_VSCROLL)
    {
        HWND hDlg = GetParent(hPanel);
        if (hDlg)
        {
            SendMessageW(hDlg, msg, wParam, lParam);
            return 0;
        }
    }

    WNDPROC oldProc = (WNDPROC)GetWindowLongPtrW(hPanel, GWLP_USERDATA);
    if (oldProc)
        return CallWindowProcW(oldProc, hPanel, msg, wParam, lParam);

    return DefWindowProcW(hPanel, msg, wParam, lParam);
}

static HWND CreateAdvancedPanel(HWND hDlg, RECT *rc, DlgState *st, HFONT hFont)
{
    HWND hPanel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDFRAME,
        rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
        hDlg, NULL, g_hDllInst, NULL);
    SetCtrlFont(hPanel, hFont);

    WNDPROC oldPanelProc = (WNDPROC)SetWindowLongPtrW(
        hPanel, GWLP_WNDPROC, (LONG_PTR)AdvancedPanelProc);
    SetWindowLongPtrW(hPanel, GWLP_USERDATA, (LONG_PTR)oldPanelProc);

    const int lx = DluX(hDlg, 8);
    const int rx = DluX(hDlg, 220);
    const int top = DluY(hDlg, 7);

    /* Buffer options group */
    CreateGroupBox(hPanel, AzText(L"Buffer Options", L"Параметры буфера"), lx, top,
                   DluX(hDlg, 195), DluY(hDlg, 153), hFont);

    HWND hPrev = CreateWindowW(L"BUTTON", AzText(L"Prevent Buffer Overruns", L"Предотвращать переполнение буфера"),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        lx + DluX(hDlg, 10), top + DluY(hDlg, 18),
        DluX(hDlg, 175), DluY(hDlg, 12),
        hPanel, (HMENU)IDC_PREVENT_OVERRUN, g_hDllInst, NULL);
    SetCtrlFont(hPrev, hFont);
    SendMessage(hPrev, BM_SETCHECK, st->preventOverrun ? BST_CHECKED : BST_UNCHECKED, 0);

    HWND hFS = CreateWindowW(L"BUTTON", AzText(L"Force Old Audio Sync", L"Принудительная старая синхронизация звука"),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        lx + DluX(hDlg, 10), top + DluY(hDlg, 36),
        DluX(hDlg, 175), DluY(hDlg, 12),
        hPanel, (HMENU)IDC_FORCE_SYNC, g_hDllInst, NULL);
    SetCtrlFont(hFS, hFont);
    SendMessage(hFS, BM_SETCHECK, st->forceSync ? BST_CHECKED : BST_UNCHECKED, 0);

    wchar_t wbuf[32];
    CreateLabel(hPanel, AzText(L"Buffer FPS", L"FPS буфера"), lx + DluX(hDlg, 10), top + DluY(hDlg, 56),
                DluX(hDlg, 70), DluY(hDlg, 10), hFont);
    swprintf(wbuf, sizeof(wbuf) / sizeof(wbuf[0]), L"%d", st->bufferFPS);
    HWND hBFVal = CreateWindowW(L"STATIC", wbuf, WS_CHILD | WS_VISIBLE | SS_RIGHT,
        lx + DluX(hDlg, 144), top + DluY(hDlg, 56),
        DluX(hDlg, 35), DluY(hDlg, 10), hPanel,
        (HMENU)IDC_BUFFERFPS_VAL, g_hDllInst, NULL);
    SetCtrlFont(hBFVal, hFont);
    HWND hBF = CreateWindowW(TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
        lx + DluX(hDlg, 10), top + DluY(hDlg, 67),
        DluX(hDlg, 170), DluY(hDlg, 18), hPanel,
        (HMENU)IDC_BUFFERFPS_SLIDE, g_hDllInst, NULL);
    SetCtrlFont(hBF, hFont);
    SendMessage(hBF, TBM_SETRANGE, TRUE, MAKELPARAM(30, 120));
    SendMessage(hBF, TBM_SETPOS, TRUE, st->bufferFPS);
    SendMessage(hBF, TBM_SETTICFREQ, 10, 0);

    CreateLabel(hPanel, AzText(L"Backend FPS", L"FPS драйвера"), lx + DluX(hDlg, 10), top + DluY(hDlg, 88),
                DluX(hDlg, 70), DluY(hDlg, 10), hFont);
    swprintf(wbuf, sizeof(wbuf) / sizeof(wbuf[0]), L"%d", st->backendFPS);
    HWND hKFVal = CreateWindowW(L"STATIC", wbuf, WS_CHILD | WS_VISIBLE | SS_RIGHT,
        lx + DluX(hDlg, 144), top + DluY(hDlg, 88),
        DluX(hDlg, 35), DluY(hDlg, 10), hPanel,
        (HMENU)IDC_BACKFPS_VAL, g_hDllInst, NULL);
    SetCtrlFont(hKFVal, hFont);
    HWND hKF = CreateWindowW(TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
        lx + DluX(hDlg, 10), top + DluY(hDlg, 99),
        DluX(hDlg, 170), DluY(hDlg, 18), hPanel,
        (HMENU)IDC_BACKFPS_SLIDE, g_hDllInst, NULL);
    SetCtrlFont(hKF, hFont);
    SendMessage(hKF, TBM_SETRANGE, TRUE, MAKELPARAM(30, 120));
    SendMessage(hKF, TBM_SETPOS, TRUE, st->backendFPS);
    SendMessage(hKF, TBM_SETTICFREQ, 10, 0);

    CreateLabel(hPanel, AzText(L"Buffers", L"Буферы"), lx + DluX(hDlg, 10), top + DluY(hDlg, 120),
                DluX(hDlg, 70), DluY(hDlg, 10), hFont);
    swprintf(wbuf, sizeof(wbuf) / sizeof(wbuf[0]), L"%d", st->buffers);
    HWND hBUVal = CreateWindowW(L"STATIC", wbuf, WS_CHILD | WS_VISIBLE | SS_RIGHT,
        lx + DluX(hDlg, 144), top + DluY(hDlg, 120),
        DluX(hDlg, 35), DluY(hDlg, 10), hPanel,
        (HMENU)IDC_BUFFERS_VAL, g_hDllInst, NULL);
    SetCtrlFont(hBUVal, hFont);
    HWND hBU = CreateWindowW(TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
        lx + DluX(hDlg, 10), top + DluY(hDlg, 131),
        DluX(hDlg, 170), DluY(hDlg, 18), hPanel,
        (HMENU)IDC_BUFFERS_SLIDE, g_hDllInst, NULL);
    SetCtrlFont(hBU, hFont);
    SendMessage(hBU, TBM_SETRANGE, TRUE, MAKELPARAM(1, 8));
    SendMessage(hBU, TBM_SETPOS, TRUE, st->buffers);
    SendMessage(hBU, TBM_SETTICFREQ, 1, 0);

    /* Emulation options group */
    CreateGroupBox(hPanel, AzText(L"Emulation Options", L"Параметры эмуляции"), rx, top,
                   DluX(hDlg, 170), DluY(hDlg, 153), hFont);

    CreateLabel(hPanel, AzText(L"Frequency", L"Частота"), rx + DluX(hDlg, 10), top + DluY(hDlg, 18),
                DluX(hDlg, 70), DluY(hDlg, 10), hFont);
    HWND hFq = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        rx + DluX(hDlg, 10), top + DluY(hDlg, 29),
        DluX(hDlg, 140), DluY(hDlg, 100),
        hPanel, (HMENU)IDC_FREQ_COMBO, g_hDllInst, NULL);
    SetCtrlFont(hFq, hFont);
    const wchar_t *freqs[] = {AzText(L"Auto", L"Авто"), L"32000", L"44100", L"48000", NULL};
    const int freqv[] = {0, 32000, 44100, 48000, 0};
    for (int i = 0; freqs[i]; i++)
    {
        int idx = (int)SendMessageW(hFq, CB_ADDSTRING, 0, (LPARAM)freqs[i]);
        SendMessageW(hFq, CB_SETITEMDATA, idx, (LPARAM)freqv[i]);
        if (st->defaultFrequency == freqv[i])
            SendMessageW(hFq, CB_SETCURSEL, idx, 0);
    }
    if (SendMessageW(hFq, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(hFq, CB_SETCURSEL, 0, 0);

    HWND hDDS8 = CreateWindowW(L"BUTTON", AzText(L"Disallow Thread Yielding DS8", L"Запретить уступку потока DS8"),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        rx + DluX(hDlg, 10), top + DluY(hDlg, 57),
        DluX(hDlg, 150), DluY(hDlg, 12),
        hPanel, (HMENU)IDC_DISALLOW_DS8, g_hDllInst, NULL);
    SetCtrlFont(hDDS8, hFont);
    SendMessage(hDDS8, BM_SETCHECK, st->disallowDS8 ? BST_CHECKED : BST_UNCHECKED, 0);

    HWND hDXA2 = CreateWindowW(L"BUTTON", AzText(L"Disallow Thread Yielding XA2", L"Запретить уступку потока XA2"),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        rx + DluX(hDlg, 10), top + DluY(hDlg, 76),
        DluX(hDlg, 150), DluY(hDlg, 12),
        hPanel, (HMENU)IDC_DISALLOW_XA2, g_hDllInst, NULL);
    SetCtrlFont(hDXA2, hFont);
    SendMessage(hDXA2, BM_SETCHECK, st->disallowXA2 ? BST_CHECKED : BST_UNCHECKED, 0);

    return hPanel;
}

/* ── Show/hide panels based on selected tab ─────────────────────────────── */
static void SwitchTab(DlgState *st, int newTab)
{
    st->currentTab = newTab;
    ShowWindow(st->hSettingsPanel, newTab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->hAdvancedPanel, newTab == 1 ? SW_SHOW : SW_HIDE);
}

/* ── Read current control values into DlgState ──────────────────────────── */
static void ReadControls(HWND hDlg, DlgState *st)
{
    /* Settings tab */
    HWND hVol = GetDlgItem(st->hSettingsPanel, IDC_VOLUME_SLIDER);
    if (hVol)
        st->volume = 100 - (int)SendMessage(hVol, TBM_GETPOS, 0, 0);

    st->mute = (SendDlgItemMessage(st->hSettingsPanel, IDC_MUTE_CHECK,
                                   BM_GETCHECK, 0, 0) == BST_CHECKED);

    HWND hDrv = GetDlgItem(st->hSettingsPanel, IDC_DRIVER_COMBO);
    if (hDrv)
    {
        int sel = (int)SendMessage(hDrv, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR)
            st->driver = (SoundDriverType)SendMessage(hDrv, CB_GETITEMDATA, sel, 0);
    }

    /* Advanced tab */
    st->preventOverrun = (SendDlgItemMessage(st->hAdvancedPanel, IDC_PREVENT_OVERRUN,
                                              BM_GETCHECK, 0, 0) == BST_CHECKED);
    st->forceSync = (SendDlgItemMessage(st->hAdvancedPanel, IDC_FORCE_SYNC,
                                        BM_GETCHECK, 0, 0) == BST_CHECKED);
    st->disallowDS8 = (SendDlgItemMessage(st->hAdvancedPanel, IDC_DISALLOW_DS8,
                                          BM_GETCHECK, 0, 0) == BST_CHECKED);
    st->disallowXA2 = (SendDlgItemMessage(st->hAdvancedPanel, IDC_DISALLOW_XA2,
                                          BM_GETCHECK, 0, 0) == BST_CHECKED);

    HWND hBF = GetDlgItem(st->hAdvancedPanel, IDC_BUFFERFPS_SLIDE);
    if (hBF) st->bufferFPS = (int)SendMessage(hBF, TBM_GETPOS, 0, 0);

    HWND hKF = GetDlgItem(st->hAdvancedPanel, IDC_BACKFPS_SLIDE);
    if (hKF) st->backendFPS = (int)SendMessage(hKF, TBM_GETPOS, 0, 0);

    HWND hBU = GetDlgItem(st->hAdvancedPanel, IDC_BUFFERS_SLIDE);
    if (hBU) st->buffers = (int)SendMessage(hBU, TBM_GETPOS, 0, 0);

    HWND hFq = GetDlgItem(st->hAdvancedPanel, IDC_FREQ_COMBO);
    if (hFq)
    {
        int sel = (int)SendMessage(hFq, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR)
            st->defaultFrequency = (int)SendMessage(hFq, CB_GETITEMDATA, sel, 0);
    }
}

/* ── Apply DlgState to Configuration ────────────────────────────────────── */
static void ApplySettings(DlgState *st)
{
    /* v1.0.12 SESSION 8: the volume slider used to write VOLUME_DEFAULT to
       the config only - the running driver kept playing at the startup-time
       volume.  Now OK/Apply also push the value live via AziVolumeApply
       (main.cpp), so the slider is heard immediately while a game runs,
       and the next RomOpen picks it up from the persisted config. */
    /* Use the Mupen64Plus Config API through the extern pointers in main.cpp */
    extern m64p_handle l_ConfigAudio;
    extern ptr_ConfigSetParameter     ConfigSetParameter;
    extern ptr_ConfigSaveSection      ConfigSaveSection;
    extern void AziVolumeApply(int level, int muted);

    if (l_ConfigAudio == NULL || ConfigSetParameter == NULL)
        return;

    int vol = st->mute ? 0 : st->volume;
    ConfigSetParameter(l_ConfigAudio, "VOLUME_DEFAULT",  M64TYPE_INT,    &vol);

    /* M64TYPE_BOOL parameters are read back by the Mupen64Plus core as a
     * full int ( *(const int*)ParamValue ), NOT as a 1-byte bool - the
     * "BOOL" in the type name only describes the stored value, not the
     * width of the value pointed to by ParamValue. SYNC_AUDIO already used
     * an int local and saved correctly. FORCE_SYNC, DISALLOW_SLEEP_DS8 and
     * DISALLOW_SLEEP_XA2 instead pointed straight at the DlgState's
     * bool (1 byte) members: the core read 3 extra bytes of padding/
     * adjacent-member memory beyond each bool, so whatever garbage sat in
     * that padding could make the saved value not match the checkbox the
     * user actually set (BUG: unchecked boxes reappearing checked after
     * reopening the dialog). Route all three through int locals, exactly
     * like syncAudio below, so the whole 4 bytes the core reads are
     * well-defined. */
    int syncAudio = !st->preventOverrun;
    ConfigSetParameter(l_ConfigAudio, "SYNC_AUDIO",      M64TYPE_BOOL,   &syncAudio);
    int forceSync = st->forceSync;
    ConfigSetParameter(l_ConfigAudio, "FORCE_SYNC",      M64TYPE_BOOL,   &forceSync);

    ConfigSetParameter(l_ConfigAudio, "BUFFER_FPS",      M64TYPE_INT,    &st->bufferFPS);
    ConfigSetParameter(l_ConfigAudio, "BACKEND_FPS",     M64TYPE_INT,    &st->backendFPS);
    ConfigSetParameter(l_ConfigAudio, "BUFFER_LEVEL",    M64TYPE_INT,    &st->buffers);
    ConfigSetParameter(l_ConfigAudio, "DEFAULT_FREQUENCY", M64TYPE_INT,  &st->defaultFrequency);

    int disallowDS8 = st->disallowDS8;
    int disallowXA2 = st->disallowXA2;
    ConfigSetParameter(l_ConfigAudio, "DISALLOW_SLEEP_DS8", M64TYPE_BOOL, &disallowDS8);
    ConfigSetParameter(l_ConfigAudio, "DISALLOW_SLEEP_XA2", M64TYPE_BOOL, &disallowXA2);

    const char *drvStr = Configuration::DriverTypeToString(st->driver);
    ConfigSetParameter(l_ConfigAudio, "DRIVER", M64TYPE_STRING, (void*)drvStr);

    /* Reload so the running plugin picks up changes immediately */
    Configuration::LoadSettings();

    /* Push the volume to the running driver live (muted -> 0) */
    AziVolumeApply(st->volume, st->mute ? 1 : 0);

    /* Persist to disk so the selected driver and all settings survive restart */
    if (ConfigSaveSection != NULL)
        ConfigSaveSection("Audio-AziAudio");
}

/* ── Main dialog WndProc ─────────────────────────────────────────────────── */
static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DlgState *st = (DlgState*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        st = (DlgState*)lParam;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)st);

        /* Init common controls (TrackBar, TabControl) */
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_BAR_CLASSES };
        InitCommonControlsEx(&icc);

        SetWindowTextW(hDlg, AzText(L"AziAudio-Plus Audio Options", L"Параметры звука AziAudio-Plus"));

        /* ── Create Tab control ──────────────────────────────────── */
        RECT rcDlg;
        GetClientRect(hDlg, &rcDlg);

        /* Tab sits at top, buttons row at bottom.  Use DLU for margins too,
           otherwise only the dialog itself scales on high-DPI systems. */
        int btnH  = DluY(hDlg, 18);
        int tabH  = rcDlg.bottom - btnH - DluY(hDlg, 9);

        HFONT hDlgFont = (HFONT)SendMessage(hDlg, WM_GETFONT, 0, 0);

        HWND hTab = CreateWindowW(WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | TCS_TABS | WS_TABSTOP,
            DluX(hDlg, 6), DluY(hDlg, 5),
            rcDlg.right - DluX(hDlg, 12), tabH - DluY(hDlg, 7),
            hDlg, (HMENU)IDC_TAB, g_hDllInst, NULL);
        SetCtrlFont(hTab, hDlgFont);
        st->hTab = hTab;

        TCITEMW ti = {};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<wchar_t*>(AzText(L"Settings", L"Настройки"));
        SendMessageW(hTab, TCM_INSERTITEMW, 0, (LPARAM)&ti);
        ti.pszText = const_cast<wchar_t*>(AzText(L"Advanced", L"Дополнительно"));
        SendMessageW(hTab, TCM_INSERTITEMW, 1, (LPARAM)&ti);

        /* Compute inner rect of tab control (where panel goes) */
        RECT rcTab;
        GetClientRect(hTab, &rcTab);
        TabCtrl_AdjustRect(hTab, FALSE, &rcTab);
        /* Map to dialog coords */
        POINT pt = {rcTab.left, rcTab.top};
        MapWindowPoints(hTab, hDlg, &pt, 1);
        RECT rcPanel = {
            pt.x + 2,
            pt.y + 2,
            pt.x + (rcTab.right - rcTab.left) - 2,
            pt.y + (rcTab.bottom - rcTab.top) - 2
        };

        /* Create both panels (only one shown at a time) */
        st->hSettingsPanel = CreateSettingsPanel(hDlg, &rcPanel, st, hDlgFont);
        st->hAdvancedPanel = CreateAdvancedPanel(hDlg, &rcPanel, st, hDlgFont);

        SwitchTab(st, 0);

        /* ── OK / Cancel / Apply buttons ─────────────────────────── */
        int bW = DluX(hDlg, 50);
        int bH = DluY(hDlg, 14);
        int gap = DluX(hDlg, 4);
        int bY = rcDlg.bottom - bH - DluY(hDlg, 6);

        /* Plain ASCII labels avoid ANSI/Unicode mismatch in hosts that use
           Unicode window procedures. The dialog font is applied explicitly
           to keep all buttons identical to the rest of the UI. */
        HWND hOk = CreateWindowW(L"BUTTON", AzText(L"OK", L"ОК"),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            rcDlg.right - 3 * (bW + gap), bY, bW, bH,
            hDlg, (HMENU)IDC_OK, g_hDllInst, NULL);
        SetCtrlFont(hOk, hDlgFont);
        HWND hCancel = CreateWindowW(L"BUTTON", AzText(L"Cancel", L"Отмена"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            rcDlg.right - 2 * (bW + gap), bY, bW, bH,
            hDlg, (HMENU)IDC_CANCEL, g_hDllInst, NULL);
        SetCtrlFont(hCancel, hDlgFont);
        HWND hApply = CreateWindowW(L"BUTTON", AzText(L"Apply", L"Применить"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            rcDlg.right - 1 * (bW + gap), bY, bW, bH,
            hDlg, (HMENU)IDC_APPLY, g_hDllInst, NULL);
        SetCtrlFont(hApply, hDlgFont);

        return TRUE;
    }

    case WM_NOTIFY:
    {
        NMHDR *pnmh = (NMHDR*)lParam;
        if (pnmh->idFrom == IDC_TAB && pnmh->code == TCN_SELCHANGE)
        {
            int tab = TabCtrl_GetCurSel(st->hTab);
            SwitchTab(st, tab);
        }
        break;
    }

    case WM_HSCROLL:
    case WM_VSCROLL:
    {
        /* Update labels when sliders move */
        HWND hCtrl = (HWND)lParam;
        int id = GetDlgCtrlID(hCtrl);
        int pos = (int)SendMessage(hCtrl, TBM_GETPOS, 0, 0);

        if (id == IDC_VOLUME_SLIDER)
        {
            /* Volume slider is inverted */
            /* nothing needed — label not shown for volume in this layout */
        }
        else if (id == IDC_BUFFERFPS_SLIDE)
        {
            char buf[16]; snprintf(buf, sizeof(buf), "%d", pos);
            SetDlgItemTextA(st->hAdvancedPanel, IDC_BUFFERFPS_VAL, buf);
        }
        else if (id == IDC_BACKFPS_SLIDE)
        {
            char buf[16]; snprintf(buf, sizeof(buf), "%d", pos);
            SetDlgItemTextA(st->hAdvancedPanel, IDC_BACKFPS_VAL, buf);
        }
        else if (id == IDC_BUFFERS_SLIDE)
        {
            char buf[8]; snprintf(buf, sizeof(buf), "%d", pos);
            SetDlgItemTextA(st->hAdvancedPanel, IDC_BUFFERS_VAL, buf);
        }
        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDC_OK)
        {
            ReadControls(hDlg, st);
            ApplySettings(st);
            EndDialog(hDlg, 1);
        }
        else if (id == IDC_CANCEL)
        {
            EndDialog(hDlg, 0);
        }
        else if (id == IDC_APPLY)
        {
            ReadControls(hDlg, st);
            ApplySettings(st);
        }
        break;
    }

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        break;
    }
    return FALSE;
}

/* ── DialogProc trampoline using DialogBoxIndirectParam ──────────────────── */
/* The dialog controls are created dynamically; only the common-controls
   manifest is kept as a resource so the widgets receive Windows visual styles. */

/* v1.0.18 CRITICAL FIX - heap buffer overflow in the dialog template builder.

   DIAGNOSIS of "RMG sometimes silently exits while the user works in the
   plugin settings" (residual after the v1.0.14/v1.0.16/v1.0.17 fixes):
   the size computation below used to count the TITLE as a single word
   ("3 + [menu, class, title]"), so the std::vector was sized 29 words
   (58 bytes) while the writes laid out 55 words (110 bytes):

     9 words  DLGTEMPLATE header (style/exstyle/cdit/x/y/cx/cy)
     1 word   menu      (0 = none)
     1 word   class     (0 = default dialog class)
    28 words  title     ("AziAudio-Plus Audio Options" + NUL)
     1 word   point size
    15 words  typeface  ("MS Shell Dlg 2" + NUL)

   => 52 bytes written past the end of a 58-byte heap allocation, on EVERY
   config-dialog open.  The overwritten bytes are the tail of the title, the
   point size and the whole typeface string - i.e. arbitrary wchar garbage
   smashed over the NEXT heap chunk's 8-byte header and up to ~44 bytes of
   its payload.  Depending on the heap layout this either corrupts a FREE
   chunk's free-list links (crash inside RtlAllocateHeap long afterwards -
   the exact signature of the RMG.exe.dmp forensics that motivated v1.0.16)
   or silently mangles a LIVE allocation owned by anything in the frontend
   (a Qt object, a std::string, ...) which explodes at some later, seemingly
   unrelated moment - "sometimes closes without any error" / no WER dialog on the
   reporter's Windows 7 + VxKex machine.  Non-deterministic by nature: when
   the 52 bytes happen to land in tail padding, that particular open is
   harmless, which is why the crash was "sometimes", tied to working with
   the settings, and never reproduced a fixed stack.

   FIX: the buffer size and the written layout are now derived from the SAME
   two string constants, so they cannot disagree, plus a final consistency
   check refuses to run the dialog (DialogBoxIndirectParam-style -1) instead
   of corrupting memory should a future edit unbalance them again. */
static INT_PTR RunDialog(HINSTANCE hInst, HWND hParent, DlgState *st)
{
    /* Build a complete in-memory dialog template.  The old template asked for
       DS_SHELLFONT but omitted the mandatory point-size/typeface fields;
       Windows therefore had to fall back to an implementation-dependent font.
       That made the same dialog look very different between hosts/DPI levels. */
    const wchar_t *title     = AzText(L"AziAudio-Plus Audio Options", L"Параметры звука AziAudio-Plus");
    const wchar_t fontName[] = L"MS Shell Dlg 2";
    const WORD pointSize = 9;

    /* Template layout (see the Dialog Templates reference; every string is
       null-terminated WCHAR[], everything is word-aligned):
         header (9 words) + menu (1) + class (1) + title + fontsize (1) + typeface */
    const size_t headerWords   = sizeof(DLGTEMPLATE) / sizeof(WORD);
    const size_t titleWords    = wcslen(title) + 1;    /* incl. terminator */
    const size_t fontNameWords = wcslen(fontName) + 1; /* incl. terminator */
    const size_t totalWords =
        headerWords +
        1 +                     /* menu:    0 = none                    */
        1 +                     /* class:   0 = default dialog class    */
        titleWords +            /* title:   null-terminated wchar array */
        1 +                     /* font point size (DS_SETFONT only)    */
        fontNameWords;          /* typeface: null-terminated wchar array */

    /* Fixed stack buffer (size is a compile-time constant with the current
       strings: 55 words / 110 bytes).  The explicit bound check below keeps
       a future longer title/typeface from ever writing past it - it fails
       the dialog instead, exactly like the layout consistency check below. */
    static const size_t kMaxTemplateWords = 64;
    WORD templateBuf[kMaxTemplateWords];
    if (totalWords > kMaxTemplateWords)
    {
        DebugMessage(M64MSG_ERROR,
            "AziAudio-Plus: dialog template too large (%lu of %lu words) - refusing to open the settings dialog",
            (unsigned long)totalWords, (unsigned long)kMaxTemplateWords);
        return -1;
    }
    memset(templateBuf, 0, totalWords * sizeof(WORD));

    WORD *const pStart = templateBuf;
    WORD *const pEnd   = pStart + totalWords;
    WORD *p = pStart;

    DLGTEMPLATE *dlg = reinterpret_cast<DLGTEMPLATE*>(p);
    dlg->style = DS_SETFONT | DS_MODALFRAME | DS_CENTER |
                 WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlg->dwExtendedStyle = WS_EX_CONTROLPARENT;
    dlg->cdit = 0;
    dlg->x = 0;
    dlg->y = 0;
    dlg->cx = 430;
    dlg->cy = 230;

    p += headerWords;
    *p++ = 0; /* no menu */
    *p++ = 0; /* default dialog class */
    memcpy(p, title, sizeof(title));
    p += titleWords;
    *p++ = pointSize;
    memcpy(p, fontName, sizeof(fontName));
    p += fontNameWords;

    /* Layout and sizing must agree EXACTLY; if a future edit breaks this,
       fail the dialog instead of writing past the heap allocation. */
    if (p != pEnd)
    {
        /* %lu + unsigned long casts: MSVCRT's printf understands them on
           every Windows version (unlike the %tu ptrdiff extension). */
        DebugMessage(M64MSG_ERROR,
            "AziAudio-Plus: dialog template layout/sizing mismatch "
            "(wrote %lu of %lu words) - refusing to open the settings dialog",
            (unsigned long)(p - pStart), (unsigned long)totalWords);
        return -1;
    }

    return DialogBoxIndirectParamW(
        hInst,
        reinterpret_cast<const DLGTEMPLATE*>(templateBuf),
        hParent,
        DlgProc,
        (LPARAM)st);
}

/* ── Public entry point ──────────────────────────────────────────────────── */
/* ── Dialog thread (v1.0.14 crash fix) ────────────────────────────────── */
/* Analysis of the user's RMG.exe.dmp:
 *
 *   EXCEPTION_ACCESS_VIOLATION (read at 0xFFFFFFFFFFFFFFFF) inside
 *   Qt6Gui.dll while the plugin's modal DialogBoxIndirectParamW loop ran
 *   ON THE HOST'S GUI THREAD (PluginConfig -> ShowConfigDialog ->
 *   RunDialog -> user32 modal loop -> DispatchMessage -> Qt window proc
 *   -> Qt6Gui paint/TSF chain -> crash; DlgProc and RunDialog frames are
 *   present in the dump stack).
 *   A foreign (non-Qt) modal loop on a Qt application's GUI thread
 *   dispatches Qt window messages WITHOUT the Qt event dispatcher's
 *   bookkeeping; on the reporter's machine (RMG/Qt 6.8 on Windows 7 via
 *   VxKex + TSF) that ends in a wild pointer read inside Qt6Gui.
 *
 *   Fix: run the whole dialog on a dedicated thread.  The host's GUI
 *   thread is NOT hijacked into any message loop - it simply waits
 *   (blocked) until the dialog closes, so the dialog behaves modally
 *   from the frontend's point of view while no host message is ever
 *   dispatched by plugin code.
 *
 *   The dialog gets NO owner on purpose: an HWND created by the host's
 *   GUI thread must not be mixed into a dialog that runs on this
 *   thread (user32 window enable/activate state and input routing are
 *   per-thread), and an ownerless DS_CENTER dialog does not disable or
 *   steal state from the host window at all. */
struct DialogThreadCtx
{
    HINSTANCE hInst;
    DlgState  *st;
    INT_PTR    result;
};

static DWORD WINAPI DialogThreadProc(LPVOID lpParameter)
{
    DialogThreadCtx *ctx = (DialogThreadCtx*)lpParameter;

    /* Init common controls on THIS thread (TrackBar / Tab control).
       InitCommonControlsEx is process-global, but calling it from the
       thread that creates the controls is the documented pattern. */
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    ctx->result = RunDialog(ctx->hInst, NULL /* ownerless - see above */, ctx->st);
    return 0;
}

bool ShowConfigDialog(HINSTANCE hInst, HWND hParent)
{
    extern m64p_handle l_ConfigAudio;
    extern ptr_ConfigGetParamInt ConfigGetParamInt;

    /* Make sure factory drivers are registered before populating combos */
    SoundDriverFactory::Initialize();

    DlgState st = {};
    st.volume        = (int)Configuration::getVolume();
    st.mute          = (st.volume == 0);
    st.driver        = Configuration::getDriver();
    st.preventOverrun = !Configuration::getSyncAudio();
    st.forceSync     = Configuration::getForceSync();
    st.bufferFPS     = (int)Configuration::getBufferFPS();
    st.backendFPS    = (int)Configuration::getBackendFPS();
    st.buffers       = (int)Configuration::getBufferLevel();
    st.defaultFrequency = 44100;   /* read from config for initial selection */
    if (ConfigGetParamInt != NULL && l_ConfigAudio != NULL)
        st.defaultFrequency = ConfigGetParamInt(l_ConfigAudio, "DEFAULT_FREQUENCY");
    st.disallowDS8   = Configuration::getDisallowSleepDS8();
    st.disallowXA2   = Configuration::getDisallowSleepXA2();
    st.currentTab    = 0;

    HINSTANCE hi = hInst ? hInst : g_hDllInst;
    if (!hi)
        hi = GetModuleHandleA(NULL);

    DialogThreadCtx ctx;
    ctx.hInst   = hi;
    ctx.st      = new (std::nothrow) DlgState(st);
    ctx.result  = 0;

    if (ctx.st == NULL)
        return false;   /* out of memory - no settings dialog */

    HANDLE hThread = CreateThread(NULL, 0, DialogThreadProc, &ctx, 0, NULL);
    if (hThread == NULL)
    {
        /* No thread could be created (extremely unlikely) - fall back to
           the historical in-thread modal loop. */
        INT_PTR res = RunDialog(hi, hParent, ctx.st);
        delete ctx.st;
        return (res == 1);
    }

    /* Block until the dialog is done.  The frontend's GUI thread sleeps
       here WITHOUT pumping any message: plugin code never dispatches a
       host message (this is the fix), and the dialog on its own thread
       keeps repainting itself normally. */
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);

    INT_PTR res = ctx.result;
    delete ctx.st;
    return (res == 1);
}

#endif /* _WIN32 */
