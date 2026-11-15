/*
 * Shared RMG theme bridge for native Win32 plugin configuration dialogs.
 * The host sets RMG_DARK_THEME before opening plugin configuration windows.
 * Kept C-compatible because SoftRDP's configuration dialog is implemented in C.
 */
#ifndef RMG_PLUGIN_THEME_WIN32_H
#define RMG_PLUGIN_THEME_WIN32_H

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RmgPluginThemeActivation
{
    HANDLE handle;
    ULONG_PTR cookie;
    BOOL active;
} RmgPluginThemeActivation;

/* Activate the plugin DLL's embedded Common Controls v6 manifest (resource 2)
   on the calling thread. This matters for DLL-hosted dialogs whose executable
   host may not have its own v6 manifest. */
static BOOL RmgPluginThemeActivate(HMODULE module, RmgPluginThemeActivation *activation)
{
    ACTCTXW context;
    wchar_t modulePath[MAX_PATH];
    DWORD pathLength;

    if (activation == NULL)
        return FALSE;

    activation->handle = NULL;
    activation->cookie = 0;
    activation->active = FALSE;

    if (module == NULL)
        return FALSE;

    pathLength = GetModuleFileNameW(module, modulePath,
        (DWORD)(sizeof(modulePath) / sizeof(modulePath[0])));
    if (pathLength == 0 || pathLength >= (DWORD)(sizeof(modulePath) / sizeof(modulePath[0])))
        return FALSE;

    ZeroMemory(&context, sizeof(context));
    context.cbSize = sizeof(context);
    context.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
    context.lpSource = modulePath;
    context.lpResourceName = MAKEINTRESOURCEW(2);

    activation->handle = CreateActCtxW(&context);
    if (activation->handle == INVALID_HANDLE_VALUE)
    {
        activation->handle = NULL;
        return FALSE;
    }

    if (!ActivateActCtx(activation->handle, &activation->cookie))
    {
        ReleaseActCtx(activation->handle);
        activation->handle = NULL;
        return FALSE;
    }

    activation->active = TRUE;
    return TRUE;
}

static void RmgPluginThemeDeactivate(RmgPluginThemeActivation *activation)
{
    if (activation == NULL)
        return;

    if (activation->active)
        DeactivateActCtx(0, activation->cookie);
    if (activation->handle != NULL)
        ReleaseActCtx(activation->handle);

    activation->handle = NULL;
    activation->cookie = 0;
    activation->active = FALSE;
}

static BOOL RmgPluginThemeIsDark(void)
{
    char value[16] = { 0 };
    DWORD length = GetEnvironmentVariableA("RMG_DARK_THEME", value, (DWORD)sizeof(value));
    return (length > 0 && length < sizeof(value) && value[0] == '1');
}

static HBRUSH RmgPluginThemeDarkBrush(void)
{
    static HBRUSH brush = NULL;
    if (brush == NULL)
        brush = CreateSolidBrush(RGB(53, 53, 53));
    return brush;
}

static HBRUSH RmgPluginThemeDarkInputBrush(void)
{
    static HBRUSH brush = NULL;
    if (brush == NULL)
        brush = CreateSolidBrush(RGB(25, 25, 25));
    return brush;
}

static HBRUSH RmgPluginThemeSelectionBrush(void)
{
    static HBRUSH brush = NULL;
    if (brush == NULL)
        brush = CreateSolidBrush(RGB(61, 74, 90));
    return brush;
}

static HBRUSH RmgPluginThemeButtonBrush(BOOL pressed, BOOL hot, BOOL disabled)
{
    static HBRUSH normalBrush = NULL;
    static HBRUSH hotBrush = NULL;
    static HBRUSH pressedBrush = NULL;
    static HBRUSH disabledBrush = NULL;

    if (normalBrush == NULL)
        normalBrush = CreateSolidBrush(RGB(65, 68, 73));
    if (hotBrush == NULL)
        hotBrush = CreateSolidBrush(RGB(78, 84, 92));
    if (pressedBrush == NULL)
        pressedBrush = CreateSolidBrush(RGB(43, 47, 53));
    if (disabledBrush == NULL)
        disabledBrush = CreateSolidBrush(RGB(49, 51, 54));

    if (disabled)
        return disabledBrush;
    if (pressed)
        return pressedBrush;
    return hot ? hotBrush : normalBrush;
}

#define RMG_PLUGIN_DARK_BUTTON_PROPERTY L"RMG.Plugin.DarkButton"
#define RMG_PLUGIN_DARK_GROUP_PROPERTY  L"RMG.Plugin.DarkGroup"
#define RMG_PLUGIN_DARK_CHECK_PROPERTY  L"RMG.Plugin.DarkCheck"
#define RMG_PLUGIN_DARK_COMBO_PROPERTY  L"RMG.Plugin.DarkCombo"
#define RMG_PLUGIN_DARK_PANEL_PROPERTY  L"RMG.Plugin.DarkPanel"
#define RMG_PLUGIN_BUTTON_HOT_PROPERTY L"RMG.Plugin.ButtonHot"
#define RMG_PLUGIN_DEFAULT_ID_SUPPORT_PROPERTY L"RMG.Plugin.DefaultIdSupport"
#define RMG_PLUGIN_DWM_PROPERTY        L"RMG.Plugin.DwmApplied"

/* Keep DWM optional: Windows 7 and older SDK/runtime combinations must not
   acquire a hard import dependency on dwmapi. The attributes are attempted
   dynamically and ignored when the running Windows version does not support
   them. */
typedef HRESULT (WINAPI *RmgDwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);

static void RmgPluginThemeApplyWindowFrame(HWND hwnd, BOOL dark)
{
    HMODULE dwmModule;
    RmgDwmSetWindowAttributeFn setAttribute;
    BOOL darkValue = dark;
    DWORD cornerPreference = dark ? 2u : 0u; /* DWMWCP_ROUND / DWMWCP_DEFAULT */
    HRESULT result;

    if (hwnd == NULL || !IsWindowVisible(hwnd) ||
        GetPropW(hwnd, RMG_PLUGIN_DWM_PROPERTY) != NULL)
        return;

    dwmModule = LoadLibraryW(L"dwmapi.dll");
    if (dwmModule == NULL)
    {
        SetPropW(hwnd, RMG_PLUGIN_DWM_PROPERTY, (HANDLE)(ULONG_PTR)1);
        return;
    }

    setAttribute = (RmgDwmSetWindowAttributeFn)GetProcAddress(dwmModule, "DwmSetWindowAttribute");
    if (setAttribute != NULL)
    {
        /* Attribute 20 is used by current Windows 11; 19 is the earlier
           Windows 10 value. Try the newer value first and fall back. */
        result = setAttribute(hwnd, 20u, &darkValue, sizeof(darkValue));
        if (FAILED(result))
            result = setAttribute(hwnd, 19u, &darkValue, sizeof(darkValue));

        /* Windows 11 only. Older systems simply reject this attribute. */
        setAttribute(hwnd, 33u, &cornerPreference, sizeof(cornerPreference));
    }

    FreeLibrary(dwmModule);
    /* Defer the attempt until the top-level window is visible, when DWM can
       reliably apply a non-client style. */
    SetPropW(hwnd, RMG_PLUGIN_DWM_PROPERTY, (HANDLE)(ULONG_PTR)1);
}

/* Draw decorative surfaces for native plugin controls in dark mode.
   IDs, input handling, selection state and command notifications stay native;
   only the visual layer is custom where Windows does not expose a reliable
   dark visual-style variant. */
static BOOL RmgPluginThemeDrawButton(const DRAWITEMSTRUCT *drawItem)
{
    HWND button;
    HDC dc;
    RECT itemRect;
    RECT textRect;
    wchar_t text[256] = { 0 };
    HANDLE storedStyle;
    LONG_PTR originalStyle;
    HFONT font;
    HPEN borderPen;
    HGDIOBJ oldPen;
    HGDIOBJ oldBrush;
    UINT textFlags;
    BOOL disabled;
    BOOL pressed;
    BOOL hot;
    int savedDC;

    if (drawItem == NULL || drawItem->CtlType != ODT_BUTTON || drawItem->hwndItem == NULL)
        return FALSE;

    button = drawItem->hwndItem;
    storedStyle = GetPropW(button, RMG_PLUGIN_DARK_BUTTON_PROPERTY);
    if (storedStyle == NULL)
        return FALSE;

    dc = drawItem->hDC;
    savedDC = SaveDC(dc);
    itemRect = drawItem->rcItem;
    textRect = itemRect;
    originalStyle = (LONG_PTR)(ULONG_PTR)storedStyle;
    font = (HFONT)SendMessageW(button, WM_GETFONT, 0, 0);
    if (font != NULL)
        SelectObject(dc, font);

    disabled = (drawItem->itemState & ODS_DISABLED) != 0;
    pressed = (drawItem->itemState & ODS_SELECTED) != 0;
    hot = (drawItem->itemState & ODS_HOTLIGHT) != 0 ||
        GetPropW(button, RMG_PLUGIN_BUTTON_HOT_PROPERTY) != NULL;

    /* A restrained rounded surface, a fine border and an accent on the
       default button approximate the Qt/Angrylion visual language without
       replacing the Win32 input/command behaviour. */
    borderPen = CreatePen(PS_SOLID, 1,
        disabled ? RGB(78, 81, 85) :
        (((originalStyle & BS_TYPEMASK) == BS_DEFPUSHBUTTON)
            ? RGB(104, 151, 203) : RGB(98, 105, 114)));
    oldPen = SelectObject(dc, borderPen);
    oldBrush = SelectObject(dc, RmgPluginThemeButtonBrush(pressed, hot, disabled));
    RoundRect(dc, itemRect.left, itemRect.top, itemRect.right, itemRect.bottom, 8, 8);
    if (oldBrush != NULL)
        SelectObject(dc, oldBrush);
    if (oldPen != NULL)
        SelectObject(dc, oldPen);
    if (borderPen != NULL)
        DeleteObject(borderPen);

    /* Subtle upper/lower edge highlights give the flat Fusion-dark surface
       the same restrained depth as the host's modern Qt controls. */
    if (!disabled && !pressed && itemRect.right - itemRect.left > 18 &&
        itemRect.bottom - itemRect.top > 10)
    {
        HPEN edgePen = CreatePen(PS_SOLID, 1,
            hot ? RGB(126, 143, 164) : RGB(91, 97, 106));
        HGDIOBJ previousPen = SelectObject(dc, edgePen);
        MoveToEx(dc, itemRect.left + 8, itemRect.top + 2, NULL);
        LineTo(dc, itemRect.right - 8, itemRect.top + 2);
        if (previousPen != NULL)
            SelectObject(dc, previousPen);
        if (edgePen != NULL)
            DeleteObject(edgePen);

        edgePen = CreatePen(PS_SOLID, 1, RGB(38, 41, 46));
        previousPen = SelectObject(dc, edgePen);
        MoveToEx(dc, itemRect.left + 8, itemRect.bottom - 2, NULL);
        LineTo(dc, itemRect.right - 8, itemRect.bottom - 2);
        if (previousPen != NULL)
            SelectObject(dc, previousPen);
        if (edgePen != NULL)
            DeleteObject(edgePen);
    }

    if (GetWindowTextW(button, text, (int)(sizeof(text) / sizeof(text[0]))) > 0)
    {
        /* Keep horizontal inset minimal: several N-Rage mapping buttons are
           deliberately narrow, and translated captions such as "Вправо"
           must retain the same usable width as in the native light theme. */
        InflateRect(&textRect, -1, -1);
        textFlags = DT_VCENTER;
        if ((originalStyle & BS_MULTILINE) != 0)
            textFlags |= DT_WORDBREAK | DT_CENTER;
        else
        {
            textFlags |= DT_SINGLELINE | DT_END_ELLIPSIS;
            if ((originalStyle & BS_CENTER) == BS_CENTER)
                textFlags |= DT_CENTER;
            else if ((originalStyle & BS_RIGHT) == BS_RIGHT)
                textFlags |= DT_RIGHT;
            else
                textFlags |= DT_LEFT;
        }
        if ((drawItem->itemState & ODS_NOACCEL) != 0)
            textFlags |= DT_HIDEPREFIX;

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, disabled ? RGB(145, 148, 153) : RGB(241, 243, 246));
        DrawTextW(dc, text, -1, &textRect, textFlags);
    }

    if ((drawItem->itemState & ODS_FOCUS) != 0 &&
        (drawItem->itemState & ODS_NOFOCUSRECT) == 0)
    {
        InflateRect(&textRect, 1, 1);
        DrawFocusRect(dc, &textRect);
    }

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
    return TRUE;
}

static BOOL RmgPluginThemeDrawDarkCheckbox(HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC dc;
    RECT clientRect;
    RECT indicatorRect;
    RECT textRect;
    wchar_t caption[256] = { 0 };
    LONG_PTR style;
    LRESULT checkState;
    BOOL radio;
    BOOL leftText;
    BOOL enabled;
    BOOL pushLike;
    HFONT font;
    int indicatorSize = 13;
    int savedDC;

    dc = BeginPaint(hwnd, &paint);
    if (dc == NULL)
        return FALSE;

    savedDC = SaveDC(dc);
    GetClientRect(hwnd, &clientRect);
    style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    checkState = SendMessageW(hwnd, BM_GETCHECK, 0, 0);
    radio = ((style & BS_TYPEMASK) == BS_RADIOBUTTON ||
        (style & BS_TYPEMASK) == BS_AUTORADIOBUTTON);
    pushLike = (style & BS_PUSHLIKE) != 0;
    leftText = (style & BS_LEFTTEXT) != 0 || (style & BS_RIGHTBUTTON) != 0;
    enabled = IsWindowEnabled(hwnd);

    FillRect(dc, &clientRect, RmgPluginThemeDarkBrush());

    if (pushLike)
    {
        BOOL pressed = checkState == BST_CHECKED;
        BOOL hot = GetPropW(hwnd, RMG_PLUGIN_BUTTON_HOT_PROPERTY) != NULL;
        HPEN borderPen = CreatePen(PS_SOLID, 1,
            enabled ? RGB(96, 102, 112) : RGB(78, 81, 85));
        HGDIOBJ oldPen = SelectObject(dc, borderPen);
        HGDIOBJ oldBrush = SelectObject(dc,
            RmgPluginThemeButtonBrush(pressed, hot, !enabled));
        RoundRect(dc, clientRect.left + 1, clientRect.top + 1,
            clientRect.right - 1, clientRect.bottom - 1, 7, 7);
        if (oldBrush != NULL)
            SelectObject(dc, oldBrush);
        if (oldPen != NULL)
            SelectObject(dc, oldPen);
        if (borderPen != NULL)
            DeleteObject(borderPen);

        GetWindowTextW(hwnd, caption, (int)(sizeof(caption) / sizeof(caption[0])));
        font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        if (font != NULL)
            SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, enabled ? RGB(239, 242, 246) : RGB(145, 148, 153));
        textRect = clientRect;
        InflateRect(&textRect, -3, -2);
        DrawTextW(dc, caption, -1, &textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else
    {
        /* Keep the indicator within the small dimensions used by N-Rage's
           controller-layout resource controls. */
        if (indicatorSize > (clientRect.right - clientRect.left - 4))
            indicatorSize = clientRect.right - clientRect.left - 4;
        if (indicatorSize > (clientRect.bottom - clientRect.top - 2))
            indicatorSize = clientRect.bottom - clientRect.top - 2;
        if (indicatorSize < 5)
            indicatorSize = 5;

        indicatorRect.left = leftText ? clientRect.right - indicatorSize - 2 : clientRect.left + 2;
        indicatorRect.top = clientRect.top +
            ((clientRect.bottom - clientRect.top - indicatorSize) / 2);
        indicatorRect.right = indicatorRect.left + indicatorSize;
        indicatorRect.bottom = indicatorRect.top + indicatorSize;

        if (radio)
        {
            HPEN pen = CreatePen(PS_SOLID, 1,
                enabled ? RGB(143, 151, 161) : RGB(78, 81, 85));
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, indicatorRect.left, indicatorRect.top,
                indicatorRect.right, indicatorRect.bottom);
            if (checkState == BST_CHECKED)
            {
                HBRUSH dotBrush = CreateSolidBrush(
                    enabled ? RGB(121, 169, 218) : RGB(99, 104, 111));
                RECT dotRect = indicatorRect;
                int dotInset = indicatorSize / 4;
                InflateRect(&dotRect, -dotInset, -dotInset);
                FillRect(dc, &dotRect, dotBrush);
                if (dotBrush != NULL)
                    DeleteObject(dotBrush);
            }
            if (oldBrush != NULL)
                SelectObject(dc, oldBrush);
            if (oldPen != NULL)
                SelectObject(dc, oldPen);
            if (pen != NULL)
                DeleteObject(pen);
        }
        else
        {
            HBRUSH boxBrush = CreateSolidBrush(RGB(29, 31, 34));
            HPEN boxPen = CreatePen(PS_SOLID, 1,
                enabled ? RGB(143, 151, 161) : RGB(78, 81, 85));
            HGDIOBJ oldPen = SelectObject(dc, boxPen);
            HGDIOBJ oldBrush = SelectObject(dc, boxBrush);
            RoundRect(dc, indicatorRect.left, indicatorRect.top,
                indicatorRect.right, indicatorRect.bottom, 3, 3);

            if (checkState == BST_CHECKED)
            {
                HPEN tickPen = CreatePen(PS_SOLID, 2,
                    enabled ? RGB(226, 233, 242) : RGB(130, 134, 140));
                HGDIOBJ previousPen = SelectObject(dc, tickPen);
                MoveToEx(dc, indicatorRect.left + (indicatorSize / 5),
                    indicatorRect.top + (indicatorSize / 2), NULL);
                LineTo(dc, indicatorRect.left + (indicatorSize / 2),
                    indicatorRect.top + ((indicatorSize * 3) / 4));
                LineTo(dc, indicatorRect.right - 2,
                    indicatorRect.top + (indicatorSize / 4));
                if (previousPen != NULL)
                    SelectObject(dc, previousPen);
                if (tickPen != NULL)
                    DeleteObject(tickPen);
            }
            else if (checkState == BST_INDETERMINATE)
            {
                RECT markRect = indicatorRect;
                HBRUSH markBrush = CreateSolidBrush(RGB(226, 233, 242));
                int markInset = indicatorSize / 4;
                InflateRect(&markRect, -markInset, -markInset);
                FillRect(dc, &markRect, markBrush);
                if (markBrush != NULL)
                    DeleteObject(markBrush);
            }

            if (oldBrush != NULL)
                SelectObject(dc, oldBrush);
            if (oldPen != NULL)
                SelectObject(dc, oldPen);
            if (boxBrush != NULL)
                DeleteObject(boxBrush);
            if (boxPen != NULL)
                DeleteObject(boxPen);
        }

        GetWindowTextW(hwnd, caption, (int)(sizeof(caption) / sizeof(caption[0])));
        textRect = clientRect;
        if (leftText)
            textRect.right = indicatorRect.left - 5;
        else
            textRect.left = indicatorRect.right + 6;

        font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        if (font != NULL)
            SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, enabled ? RGB(239, 242, 246) : RGB(145, 148, 153));
        DrawTextW(dc, caption, -1, &textRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (GetFocus() == hwnd)
    {
        InflateRect(&textRect, -1, -1);
        DrawFocusRect(dc, &textRect);
    }

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
    EndPaint(hwnd, &paint);
    return TRUE;
}

static BOOL RmgPluginThemeDrawGroupBox(const DRAWITEMSTRUCT *drawItem)
{
    HWND groupBox;
    HDC dc;
    RECT frameRect;
    RECT captionRect;
    wchar_t caption[256] = { 0 };
    SIZE captionSize = { 0, 0 };
    HFONT font;
    HPEN pen;
    HGDIOBJ oldPen;
    HGDIOBJ oldBrush;
    int savedDC;

    if (drawItem == NULL || drawItem->CtlType != ODT_BUTTON || drawItem->hwndItem == NULL)
        return FALSE;

    groupBox = drawItem->hwndItem;
    if (GetPropW(groupBox, RMG_PLUGIN_DARK_GROUP_PROPERTY) == NULL)
        return FALSE;

    dc = drawItem->hDC;
    savedDC = SaveDC(dc);
    frameRect = drawItem->rcItem;
    frameRect.top += 7;
    GetWindowTextW(groupBox, caption, (int)(sizeof(caption) / sizeof(caption[0])));
    font = (HFONT)SendMessageW(groupBox, WM_GETFONT, 0, 0);
    if (font != NULL)
        SelectObject(dc, font);
    if (caption[0] != L'\0')
        GetTextExtentPoint32W(dc, caption, (int)wcslen(caption), &captionSize);

    pen = CreatePen(PS_SOLID, 1, RGB(91, 98, 108));
    oldPen = SelectObject(dc, pen);
    oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, frameRect.left, frameRect.top,
        frameRect.right, frameRect.bottom, 8, 8);
    if (oldBrush != NULL)
        SelectObject(dc, oldBrush);
    if (oldPen != NULL)
        SelectObject(dc, oldPen);
    if (pen != NULL)
        DeleteObject(pen);

    if (caption[0] != L'\0')
    {
        captionRect.left = frameRect.left + 10;
        captionRect.top = drawItem->rcItem.top;
        captionRect.right = captionRect.left + captionSize.cx + 8;
        captionRect.bottom = captionRect.top + captionSize.cy + 3;
        FillRect(dc, &captionRect, RmgPluginThemeDarkBrush());
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(232, 235, 239));
        TextOutW(dc, captionRect.left + 4, captionRect.top + 1,
            caption, (int)wcslen(caption));
    }

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
    return TRUE;
}

static BOOL RmgPluginThemeDrawTabItem(const DRAWITEMSTRUCT *drawItem)
{
    HWND tab;
    HDC dc;
    RECT itemRect;
    RECT textRect;
    wchar_t caption[128] = { 0 };
    TCITEMW item;
    BOOL selected;
    BOOL disabled;
    COLORREF fillColor;
    COLORREF borderColor;
    HBRUSH fillBrush;
    HPEN borderPen;
    HGDIOBJ oldBrush;
    HGDIOBJ oldPen;
    HFONT font;
    int savedDC;

    if (drawItem == NULL || drawItem->CtlType != ODT_TAB || drawItem->hwndItem == NULL)
        return FALSE;

    tab = drawItem->hwndItem;
    dc = drawItem->hDC;
    itemRect = drawItem->rcItem;
    selected = (drawItem->itemState & ODS_SELECTED) != 0;
    disabled = !IsWindowEnabled(tab);
    fillColor = selected ? RGB(65, 72, 82) : RGB(43, 46, 51);
    borderColor = selected ? RGB(112, 151, 197) : RGB(82, 88, 97);
    savedDC = SaveDC(dc);

    fillBrush = CreateSolidBrush(fillColor);
    borderPen = CreatePen(PS_SOLID, 1, borderColor);
    oldBrush = SelectObject(dc, fillBrush);
    oldPen = SelectObject(dc, borderPen);
    RoundRect(dc, itemRect.left + 1, itemRect.top + 1,
        itemRect.right - 1, itemRect.bottom, 8, 8);
    if (oldBrush != NULL)
        SelectObject(dc, oldBrush);
    if (oldPen != NULL)
        SelectObject(dc, oldPen);
    if (fillBrush != NULL)
        DeleteObject(fillBrush);
    if (borderPen != NULL)
        DeleteObject(borderPen);

    ZeroMemory(&item, sizeof(item));
    item.mask = TCIF_TEXT;
    item.pszText = caption;
    item.cchTextMax = (int)(sizeof(caption) / sizeof(caption[0]));
    SendMessageW(tab, TCM_GETITEMW, (WPARAM)drawItem->itemID, (LPARAM)&item);

    font = (HFONT)SendMessageW(tab, WM_GETFONT, 0, 0);
    if (font != NULL)
        SelectObject(dc, font);
    textRect = itemRect;
    InflateRect(&textRect, -6, -1);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? RGB(145, 148, 153) : RGB(239, 242, 246));
    DrawTextW(dc, caption, -1, &textRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
    return TRUE;
}

/* Combo boxes are owner-drawn only in dark mode. This keeps the native
   selection/keyboard behaviour while ensuring both the closed field and the
   dropped list use the same dark palette as the other plugin controls. */
static BOOL RmgPluginThemeDrawCombo(const DRAWITEMSTRUCT *drawItem)
{
    HWND combo;
    HDC dc;
    RECT itemRect;
    RECT textRect;
    wchar_t text[256] = { 0 };
    int itemIndex;
    BOOL selected;
    BOOL disabled;
    HFONT font;
    int savedDC;

    if (drawItem == NULL || drawItem->CtlType != ODT_COMBOBOX || drawItem->hwndItem == NULL)
        return FALSE;

    combo = drawItem->hwndItem;
    if (GetPropW(combo, RMG_PLUGIN_DARK_COMBO_PROPERTY) == NULL)
        return FALSE;

    dc = drawItem->hDC;
    savedDC = SaveDC(dc);
    itemRect = drawItem->rcItem;
    itemIndex = (int)drawItem->itemID;
    if (itemIndex < 0)
        itemIndex = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);

    selected = (drawItem->itemState & ODS_SELECTED) != 0;
    disabled = !IsWindowEnabled(combo);

    if (selected)
    {
        FillRect(dc, &itemRect, RmgPluginThemeSelectionBrush());
    }
    else
    {
        FillRect(dc, &itemRect, RmgPluginThemeDarkInputBrush());
    }

    if (itemIndex >= 0)
    {
        if (IsWindowUnicode(combo))
        {
            LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)itemIndex, 0);
            if (length >= 0 && length < (LRESULT)(sizeof(text) / sizeof(text[0])))
                SendMessageW(combo, CB_GETLBTEXT, (WPARAM)itemIndex, (LPARAM)text);
        }
        else
        {
            char ansiText[256] = { 0 };
            LRESULT length = SendMessageA(combo, CB_GETLBTEXTLEN, (WPARAM)itemIndex, 0);
            if (length >= 0 && length < (LRESULT)sizeof(ansiText))
            {
                SendMessageA(combo, CB_GETLBTEXT, (WPARAM)itemIndex, (LPARAM)ansiText);
                MultiByteToWideChar(CP_ACP, 0, ansiText, -1, text,
                    (int)(sizeof(text) / sizeof(text[0])));
            }
        }
    }

    font = (HFONT)SendMessageW(combo, WM_GETFONT, 0, 0);
    if (font != NULL)
        SelectObject(dc, font);

    textRect = itemRect;
    InflateRect(&textRect, -7, -1);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? RGB(145, 148, 153) : RGB(239, 242, 246));
    DrawTextW(dc, text, -1, &textRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if ((drawItem->itemState & ODS_FOCUS) != 0 &&
        (drawItem->itemState & ODS_NOFOCUSRECT) == 0)
    {
        InflateRect(&textRect, 1, 1);
        DrawFocusRect(dc, &textRect);
    }

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
    return TRUE;
}

/* Draw the closed face of a dark CBS_DROPDOWNLIST control. Owner-draw
   handles the selected/list items, but Windows still paints the outer combo
   frame and drop button on some Win7/VxKex theme combinations. Painting this
   small surface here keeps the field, arrow and list on one palette without
   changing native mouse/keyboard/drop-down behaviour. */
static BOOL RmgPluginThemeDrawDarkComboControl(HWND combo)
{
    PAINTSTRUCT paint;
    HDC dc;
    RECT clientRect;
    RECT buttonRect;
    RECT textRect;
    wchar_t text[256] = { 0 };
    int selection;
    int buttonWidth;
    BOOL enabled;
    BOOL focused;
    HFONT font;
    HGDIOBJ oldFont = NULL;
    HPEN borderPen;
    HBRUSH buttonBrush;
    COLORREF borderColor;
    POINT arrow[3];
    int savedDC;

    if (combo == NULL ||
        GetPropW(combo, RMG_PLUGIN_DARK_COMBO_PROPERTY) == NULL)
        return FALSE;

    dc = BeginPaint(combo, &paint);
    if (dc == NULL)
        return FALSE;

    savedDC = SaveDC(dc);
    GetClientRect(combo, &clientRect);
    enabled = IsWindowEnabled(combo);
    focused = GetFocus() == combo;
    buttonWidth = GetSystemMetrics(SM_CXVSCROLL);
    if (buttonWidth < 16)
        buttonWidth = 16;
    if (buttonWidth > clientRect.right - clientRect.left)
        buttonWidth = clientRect.right - clientRect.left;

    FillRect(dc, &clientRect, RmgPluginThemeDarkInputBrush());

    buttonRect = clientRect;
    buttonRect.left = buttonRect.right - buttonWidth;
    buttonBrush = RmgPluginThemeButtonBrush(FALSE, focused, !enabled);
    FillRect(dc, &buttonRect, buttonBrush);

    borderColor = focused ? RGB(121, 169, 218) : RGB(105, 110, 118);
    borderPen = CreatePen(PS_SOLID, 1, borderColor);
    {
        HGDIOBJ oldPen = SelectObject(dc, borderPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, clientRect.left, clientRect.top,
            clientRect.right - 1, clientRect.bottom - 1, 4, 4);
        if (oldBrush != NULL)
            SelectObject(dc, oldBrush);
        if (oldPen != NULL)
            SelectObject(dc, oldPen);
    }
    if (borderPen != NULL)
        DeleteObject(borderPen);

    /* Divider and a compact down-chevron in the button area. */
    {
        HPEN dividerPen = CreatePen(PS_SOLID, 1, RGB(73, 77, 84));
        HGDIOBJ oldPen = SelectObject(dc, dividerPen);
        MoveToEx(dc, buttonRect.left, buttonRect.top + 3, NULL);
        LineTo(dc, buttonRect.left, buttonRect.bottom - 3);
        if (oldPen != NULL)
            SelectObject(dc, oldPen);
        if (dividerPen != NULL)
            DeleteObject(dividerPen);
    }
    arrow[0].x = buttonRect.left + (buttonWidth / 2) - 4;
    arrow[0].y = buttonRect.top + (clientRect.bottom - clientRect.top) / 2 - 1;
    arrow[1].x = buttonRect.left + (buttonWidth / 2) + 4;
    arrow[1].y = arrow[0].y;
    arrow[2].x = buttonRect.left + (buttonWidth / 2);
    arrow[2].y = arrow[0].y + 4;
    {
        HBRUSH arrowBrush = CreateSolidBrush(enabled ? RGB(225, 230, 237) : RGB(135, 139, 145));
        HPEN arrowPen = CreatePen(PS_SOLID, 1, enabled ? RGB(225, 230, 237) : RGB(135, 139, 145));
        HGDIOBJ oldPen = SelectObject(dc, arrowPen);
        HGDIOBJ oldBrush = SelectObject(dc, arrowBrush);
        Polygon(dc, arrow, 3);
        if (oldBrush != NULL)
            SelectObject(dc, oldBrush);
        if (oldPen != NULL)
            SelectObject(dc, oldPen);
        if (arrowPen != NULL)
            DeleteObject(arrowPen);
        if (arrowBrush != NULL)
            DeleteObject(arrowBrush);
    }

    selection = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection >= 0)
    {
        if (IsWindowUnicode(combo))
        {
            LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)selection, 0);
            if (length >= 0 && length < (LRESULT)(sizeof(text) / sizeof(text[0])))
                SendMessageW(combo, CB_GETLBTEXT, (WPARAM)selection, (LPARAM)text);
        }
        else
        {
            char ansiText[256] = { 0 };
            LRESULT length = SendMessageA(combo, CB_GETLBTEXTLEN, (WPARAM)selection, 0);
            if (length >= 0 && length < (LRESULT)sizeof(ansiText))
            {
                SendMessageA(combo, CB_GETLBTEXT, (WPARAM)selection, (LPARAM)ansiText);
                MultiByteToWideChar(CP_ACP, 0, ansiText, -1, text,
                    (int)(sizeof(text) / sizeof(text[0])));
            }
        }
    }
    else if (IsWindowUnicode(combo))
    {
        GetWindowTextW(combo, text, (int)(sizeof(text) / sizeof(text[0])));
    }
    else
    {
        char ansiText[256] = { 0 };
        GetWindowTextA(combo, ansiText, (int)sizeof(ansiText));
        MultiByteToWideChar(CP_ACP, 0, ansiText, -1, text,
            (int)(sizeof(text) / sizeof(text[0])));
    }

    font = (HFONT)SendMessageW(combo, WM_GETFONT, 0, 0);
    if (font != NULL)
        oldFont = SelectObject(dc, font);
    textRect = clientRect;
    textRect.right = buttonRect.left - 1;
    InflateRect(&textRect, -7, -1);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? RGB(239, 242, 246) : RGB(145, 148, 153));
    DrawTextW(dc, text, -1, &textRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (focused)
    {
        InflateRect(&textRect, 1, 1);
        DrawFocusRect(dc, &textRect);
    }
    if (oldFont != NULL)
        SelectObject(dc, oldFont);

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
    EndPaint(combo, &paint);
    return TRUE;
}

static BOOL RmgPluginThemeMeasureComboItem(HWND parent, MEASUREITEMSTRUCT *measureItem)
{
    HWND combo;
    HDC dc;
    HFONT font;
    HGDIOBJ oldFont = NULL;
    TEXTMETRICW metrics;
    RECT clientRect;

    if (parent == NULL || measureItem == NULL || measureItem->CtlType != ODT_COMBOBOX)
        return FALSE;

    combo = GetDlgItem(parent, (int)measureItem->CtlID);
    if (combo == NULL || GetPropW(combo, RMG_PLUGIN_DARK_COMBO_PROPERTY) == NULL)
        return FALSE;

    dc = GetDC(combo);
    if (dc == NULL)
        return FALSE;

    font = (HFONT)SendMessageW(combo, WM_GETFONT, 0, 0);
    if (font != NULL)
        oldFont = SelectObject(dc, font);

    ZeroMemory(&metrics, sizeof(metrics));
    if (!GetTextMetricsW(dc, &metrics))
        metrics.tmHeight = 14;

    if (oldFont != NULL)
        SelectObject(dc, oldFont);
    ReleaseDC(combo, dc);

    measureItem->itemHeight = (UINT)(metrics.tmHeight + 7);
    if (measureItem->itemHeight < 18)
        measureItem->itemHeight = 18;
    GetClientRect(combo, &clientRect);
    measureItem->itemWidth = (UINT)(clientRect.right - clientRect.left);
    return TRUE;
}

/* Calculate a common owner-drawn tab size from the actual captions/font. */
static BOOL RmgPluginThemeCalculateTabSize(HWND tab, UINT *width, UINT *height)
{
    HDC dc;
    HFONT font;
    HGDIOBJ oldFont = NULL;
    int itemCount;
    int maxWidth = 0;
    int maxHeight = 0;
    int i;
    wchar_t caption[128];

    if (tab == NULL || width == NULL || height == NULL)
        return FALSE;

    dc = GetDC(tab);
    if (dc == NULL)
        return FALSE;

    font = (HFONT)SendMessageW(tab, WM_GETFONT, 0, 0);
    if (font != NULL)
        oldFont = SelectObject(dc, font);

    itemCount = (int)SendMessageW(tab, TCM_GETITEMCOUNT, 0, 0);
    for (i = 0; i < itemCount; ++i)
    {
        TCITEMW item;
        SIZE textSize = { 0, 0 };
        ZeroMemory(caption, sizeof(caption));
        ZeroMemory(&item, sizeof(item));
        item.mask = TCIF_TEXT;
        item.pszText = caption;
        item.cchTextMax = (int)(sizeof(caption) / sizeof(caption[0]));
        if (SendMessageW(tab, TCM_GETITEMW, (WPARAM)i, (LPARAM)&item) &&
            GetTextExtentPoint32W(dc, caption, (int)wcslen(caption), &textSize))
        {
            if (textSize.cx > maxWidth)
                maxWidth = textSize.cx;
            if (textSize.cy > maxHeight)
                maxHeight = textSize.cy;
        }
    }

    if (oldFont != NULL)
        SelectObject(dc, oldFont);
    ReleaseDC(tab, dc);

    if (maxWidth == 0)
        maxWidth = 64;
    if (maxHeight == 0)
        maxHeight = 16;

    *width = (UINT)(maxWidth + 22);
    *height = (UINT)(maxHeight + 10);
    return TRUE;
}

/* TCS_OWNERDRAWFIXED requires the tab control's parent to provide a common
   item size. It is sent only once; a later TCM_INSERTITEM can therefore
   update the size after the first caption becomes available. */
static BOOL RmgPluginThemeMeasureTabItems(HWND parent, MEASUREITEMSTRUCT *measureItem)
{
    HWND tab;
    UINT width = 0;
    UINT height = 0;

    if (parent == NULL || measureItem == NULL || measureItem->CtlType != ODT_TAB)
        return FALSE;

    tab = GetDlgItem(parent, (int)measureItem->CtlID);
    if (tab == NULL || !RmgPluginThemeCalculateTabSize(tab, &width, &height))
        return FALSE;

    measureItem->itemWidth = width;
    measureItem->itemHeight = height;
    return TRUE;
}

static LRESULT CALLBACK RmgPluginThemeSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR referenceData)
{
    (void)referenceData;

    /* Recalculate fixed tab dimensions after each caption is inserted or
       changed. WM_MEASUREITEM can arrive before the first tab exists. */
    if (RmgPluginThemeIsDark() &&
        (message == TCM_INSERTITEMA || message == TCM_INSERTITEMW ||
         message == TCM_SETITEMA || message == TCM_SETITEMW))
    {
        LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        if (result != -1)
        {
            wchar_t className[64] = { 0 };
            if (GetClassNameW(hwnd, className,
                (int)(sizeof(className) / sizeof(className[0]))) > 0 &&
                wcscmp(className, L"SysTabControl32") == 0)
            {
                UINT width = 0;
                UINT height = 0;
                if (RmgPluginThemeCalculateTabSize(hwnd, &width, &height))
                    SendMessageW(hwnd, TCM_SETITEMSIZE, 0, MAKELPARAM(width, height));
            }
        }
        return result;
    }

    if (message == WM_SHOWWINDOW && wParam != 0 && RmgPluginThemeIsDark())
    {
        HWND rootWindow = GetAncestor(hwnd, GA_ROOT);
        if (rootWindow == hwnd &&
            (GetWindowLongPtrW(rootWindow, GWL_STYLE) & WS_CAPTION) != 0)
            RmgPluginThemeApplyWindowFrame(rootWindow, TRUE);
    }

    if (message == WM_MEASUREITEM && lParam != 0 && RmgPluginThemeIsDark())
    {
        MEASUREITEMSTRUCT *measureItem = (MEASUREITEMSTRUCT *)lParam;
        if (RmgPluginThemeMeasureTabItems(hwnd, measureItem) ||
            RmgPluginThemeMeasureComboItem(hwnd, measureItem))
            return TRUE;
    }

    if (message == WM_ERASEBKGND &&
        GetPropW(hwnd, RMG_PLUGIN_DARK_PANEL_PROPERTY) != NULL)
    {
        RECT clientRect;
        HBRUSH panelBrush = RmgPluginThemeIsDark()
            ? RmgPluginThemeDarkBrush() : GetSysColorBrush(COLOR_BTNFACE);
        if (GetClientRect(hwnd, &clientRect))
        {
            FillRect((HDC)wParam, &clientRect, panelBrush);
            return 1;
        }
    }

    if (message == WM_ERASEBKGND && RmgPluginThemeIsDark() &&
        GetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY) != NULL)
    {
        RECT clientRect;
        if (GetClientRect(hwnd, &clientRect))
        {
            FillRect((HDC)wParam, &clientRect, RmgPluginThemeDarkBrush());
            return 1;
        }
    }

    if (message == WM_PAINT && RmgPluginThemeIsDark() &&
        GetPropW(hwnd, RMG_PLUGIN_DARK_COMBO_PROPERTY) != NULL)
    {
        RmgPluginThemeDrawDarkComboControl(hwnd);
        return 0;
    }

    if (message == WM_PAINT && RmgPluginThemeIsDark() &&
        GetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY) != NULL)
    {
        RmgPluginThemeDrawDarkCheckbox(hwnd);
        return 0;
    }

    if (message == WM_DRAWITEM && lParam != 0)
    {
        const DRAWITEMSTRUCT *drawItem = (const DRAWITEMSTRUCT *)lParam;
        if (RmgPluginThemeDrawButton(drawItem) ||
            RmgPluginThemeDrawGroupBox(drawItem) ||
            RmgPluginThemeDrawTabItem(drawItem) ||
            RmgPluginThemeDrawCombo(drawItem))
            return TRUE;
    }

    if (RmgPluginThemeIsDark() &&
        (GetPropW(hwnd, RMG_PLUGIN_DARK_BUTTON_PROPERTY) != NULL ||
         GetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY) != NULL))
    {
        if (message == WM_MOUSEMOVE && GetPropW(hwnd, RMG_PLUGIN_BUTTON_HOT_PROPERTY) == NULL)
        {
            TRACKMOUSEEVENT tracking;
            ZeroMemory(&tracking, sizeof(tracking));
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = hwnd;
            TrackMouseEvent(&tracking);
            SetPropW(hwnd, RMG_PLUGIN_BUTTON_HOT_PROPERTY, (HANDLE)(ULONG_PTR)1);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (message == WM_MOUSELEAVE &&
            GetPropW(hwnd, RMG_PLUGIN_BUTTON_HOT_PROPERTY) != NULL)
        {
            RemovePropW(hwnd, RMG_PLUGIN_BUTTON_HOT_PROPERTY);
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }

    if (message == WM_NOTIFY && lParam != 0 && RmgPluginThemeIsDark())
    {
        LPNMHDR notification = (LPNMHDR)lParam;
        if (notification->code == NM_CUSTOMDRAW && notification->hwndFrom != NULL)
        {
            wchar_t controlClass[64] = { 0 };
            if (GetClassNameW(notification->hwndFrom, controlClass,
                (int)(sizeof(controlClass) / sizeof(controlClass[0]))) > 0 &&
                wcscmp(controlClass, L"SysHeader32") == 0)
            {
                LPNMCUSTOMDRAW customDraw = (LPNMCUSTOMDRAW)lParam;
                if (customDraw->dwDrawStage == CDDS_PREPAINT)
                    return CDRF_NOTIFYITEMDRAW;
                if (customDraw->dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    FillRect(customDraw->hdc, &customDraw->rc, RmgPluginThemeDarkBrush());
                    SetBkMode(customDraw->hdc, TRANSPARENT);
                    SetTextColor(customDraw->hdc, RGB(239, 242, 246));
                    return CDRF_NEWFONT;
                }
            }
        }
    }

    if (message == WM_ERASEBKGND && RmgPluginThemeIsDark())
    {
        wchar_t className[64] = { 0 };
        if (GetClassNameW(hwnd, className,
            (int)(sizeof(className) / sizeof(className[0]))) > 0)
        {
            BOOL dialogWindow = (wcscmp(className, L"#32770") == 0);
            BOOL etchedPanel = (lstrcmpiW(className, L"Static") == 0 &&
                (GetWindowLongPtrW(hwnd, GWL_STYLE) & SS_TYPEMASK) == SS_ETCHEDFRAME);
            BOOL markedPanel = GetPropW(hwnd, RMG_PLUGIN_DARK_PANEL_PROPERTY) != NULL;
            BOOL tabControl = (wcscmp(className, L"SysTabControl32") == 0);
            BOOL trackbar = (wcscmp(className, L"msctls_trackbar32") == 0);
            if (dialogWindow || etchedPanel || markedPanel || tabControl || trackbar)
            {
                RECT clientRect;
                if (GetClientRect(hwnd, &clientRect))
                {
                    FillRect((HDC)wParam, &clientRect, RmgPluginThemeDarkBrush());
                    return 1;
                }
            }
        }
    }

    /* CTLCOLOR notifications are sent to the parent of the control. In the
       light theme keep the stock rendering, but make STATIC labels and
       checkbox/group-box captions transparent. This prevents the parent
       STATIC page from painting a gray rectangle behind otherwise normal
       light-theme text without restyling push buttons or input controls. */
    if (!RmgPluginThemeIsDark() &&
        (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN))
    {
        HWND control = (HWND)lParam;
        BOOL transparentButton = FALSE;
        BOOL textualStatic = FALSE;
        if (control != NULL && message == WM_CTLCOLORSTATIC)
        {
            LONG_PTR staticType = GetWindowLongPtrW(control, GWL_STYLE) & SS_TYPEMASK;
            textualStatic = staticType == SS_LEFT || staticType == SS_CENTER ||
                staticType == SS_RIGHT || staticType == SS_SIMPLE ||
                staticType == SS_LEFTNOWORDWRAP;
        }
        if (message == WM_CTLCOLORBTN && control != NULL)
        {
            LONG_PTR buttonType = GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK;
            transparentButton = buttonType == BS_CHECKBOX || buttonType == BS_AUTOCHECKBOX ||
                buttonType == BS_3STATE || buttonType == BS_AUTO3STATE ||
                buttonType == BS_RADIOBUTTON || buttonType == BS_AUTORADIOBUTTON ||
                buttonType == BS_GROUPBOX;
        }

        if (textualStatic || transparentButton)
        {
            HDC dc = (HDC)wParam;
            COLORREF foreground = (control != NULL && !IsWindowEnabled(control))
                ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_WINDOWTEXT);
            SetTextColor(dc, foreground);
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
    }

    if (RmgPluginThemeIsDark() &&
        (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC ||
         message == WM_CTLCOLORBTN || message == WM_CTLCOLOREDIT ||
         message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORSCROLLBAR ||
         message == WM_CTLCOLORMSGBOX))
    {
        HDC dc = (HDC)wParam;
        HWND control = (HWND)lParam;
        BOOL inputControl = (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX);
        COLORREF foreground = (control != NULL && !IsWindowEnabled(control))
            ? RGB(145, 148, 153) : RGB(239, 242, 246);

        SetTextColor(dc, foreground);
        if (inputControl)
        {
            SetBkColor(dc, RGB(25, 25, 25));
            SetBkMode(dc, OPAQUE);
            return (LRESULT)RmgPluginThemeDarkInputBrush();
        }

        SetBkColor(dc, RGB(53, 53, 53));
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)RmgPluginThemeDarkBrush();
    }

    if (message == WM_NCDESTROY)
    {
        RemovePropW(hwnd, RMG_PLUGIN_DARK_BUTTON_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_GROUP_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_COMBO_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_PANEL_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_BUTTON_HOT_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DEFAULT_ID_SUPPORT_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DWM_PROPERTY);
        RemoveWindowSubclass(hwnd, RmgPluginThemeSubclassProc, subclassId);
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    {
        LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        if (message == BM_SETCHECK || message == WM_SETTEXT ||
            message == WM_ENABLE || message == WM_SETFOCUS || message == WM_KILLFOCUS ||
            message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
            message == WM_KEYDOWN || message == WM_KEYUP)
        {
            if (GetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY) != NULL)
                InvalidateRect(hwnd, NULL, TRUE);
        }
        return result;
    }
}

static void RmgPluginThemeAttach(HWND hwnd)
{
    BOOL dark;
    wchar_t className[64] = { 0 };
    HRESULT themeResult;
    HWND rootWindow;

    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    dark = RmgPluginThemeIsDark();

    /* Install the subclass only after the HWND has completed creation. Calls
       made from WM_INITDIALOG / WM_CREATE are safe; callers should not attach
       from HCBT_CREATEWND, which runs before WM_NCCREATE. */
    SetWindowSubclass(hwnd, RmgPluginThemeSubclassProc, (UINT_PTR)0x524D47u, 0);

    if (GetClassNameW(hwnd, className,
        (int)(sizeof(className) / sizeof(className[0]))) > 0 && dark)
    {
        if (wcscmp(className, L"Button") == 0)
        {
            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            LONG_PTR buttonType = style & BS_TYPEMASK;
            if (buttonType == BS_PUSHBUTTON || buttonType == BS_DEFPUSHBUTTON)
            {
                HWND root = GetAncestor(hwnd, GA_ROOT);
                wchar_t rootClass[64] = { 0 };
                BOOL standardDialog = root != NULL &&
                    GetClassNameW(root, rootClass,
                        (int)(sizeof(rootClass) / sizeof(rootClass[0]))) > 0 &&
                    wcscmp(rootClass, L"#32770") == 0;
                BOOL supportsDefaultId = standardDialog ||
                    (root != NULL && GetPropW(root, RMG_PLUGIN_DEFAULT_ID_SUPPORT_PROPERTY) != NULL);

                if (buttonType == BS_PUSHBUTTON || supportsDefaultId)
                {
                    if (GetPropW(hwnd, RMG_PLUGIN_DARK_BUTTON_PROPERTY) == NULL)
                    {
                        SetPropW(hwnd, RMG_PLUGIN_DARK_BUTTON_PROPERTY,
                            (HANDLE)(ULONG_PTR)style);
                        SetWindowLongPtrW(hwnd, GWL_STYLE,
                            (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
                        if (buttonType == BS_DEFPUSHBUTTON && supportsDefaultId)
                            SendMessageW(root, DM_SETDEFID, (WPARAM)GetDlgCtrlID(hwnd), 0);
                    }
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            else if (buttonType == BS_GROUPBOX)
            {
                if (GetPropW(hwnd, RMG_PLUGIN_DARK_GROUP_PROPERTY) == NULL)
                {
                    SetPropW(hwnd, RMG_PLUGIN_DARK_GROUP_PROPERTY, (HANDLE)(ULONG_PTR)1);
                    SetWindowLongPtrW(hwnd, GWL_STYLE,
                        (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
                }
                InvalidateRect(hwnd, NULL, TRUE);
            }
            else if (buttonType == BS_CHECKBOX || buttonType == BS_AUTOCHECKBOX ||
                buttonType == BS_3STATE || buttonType == BS_AUTO3STATE ||
                buttonType == BS_RADIOBUTTON || buttonType == BS_AUTORADIOBUTTON)
            {
                SetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY, (HANDLE)(ULONG_PTR)1);
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        else if (wcscmp(className, L"SysTabControl32") == 0)
        {
            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            if ((style & TCS_OWNERDRAWFIXED) == 0)
            {
                SetWindowLongPtrW(hwnd, GWL_STYLE, style | TCS_OWNERDRAWFIXED);
                SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                    SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
            }
            InvalidateRect(hwnd, NULL, TRUE);
        }
        else if (wcscmp(className, L"ComboBox") == 0)
        {
            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            LONG_PTR type = style & 0x0003;
            if (type == CBS_DROPDOWNLIST &&
                (style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) == 0)
            {
                SetPropW(hwnd, RMG_PLUGIN_DARK_COMBO_PROPERTY,
                    (HANDLE)(ULONG_PTR)style);
                style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
                SetWindowLongPtrW(hwnd, GWL_STYLE, style);
                SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                    SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        else if (wcscmp(className, L"SysListView32") == 0)
        {
            SendMessageW(hwnd, LVM_SETBKCOLOR, 0, (LPARAM)RGB(25, 25, 25));
            SendMessageW(hwnd, LVM_SETTEXTCOLOR, 0, (LPARAM)RGB(239, 242, 246));
            SendMessageW(hwnd, LVM_SETTEXTBKCOLOR, 0, (LPARAM)RGB(25, 25, 25));
        }
    }

    /* TrackBars use the explicit Explorer class in both palettes. Some
       Windows 7/VxKex combinations otherwise render a clipped black channel
       in the light theme, while DarkMode_Explorer can hide the channel/ticks
       in dark mode. Other native controls are themed only when dark is active. */
    if (GetClassNameW(hwnd, className,
        (int)(sizeof(className) / sizeof(className[0]))) > 0 &&
        wcscmp(className, L"msctls_trackbar32") == 0)
    {
        SetWindowTheme(hwnd, L"Explorer", NULL);
        InvalidateRect(hwnd, NULL, TRUE);
    }
    else if (dark && GetClassNameW(hwnd, className,
        (int)(sizeof(className) / sizeof(className[0]))) > 0)
    {
        if (wcscmp(className, L"ComboBox") == 0 ||
                 wcscmp(className, L"Edit") == 0 ||
                 wcscmp(className, L"ListBox") == 0 ||
                 wcscmp(className, L"SysListView32") == 0 ||
                 wcscmp(className, L"SysTreeView32") == 0 ||
                 wcscmp(className, L"SysHeader32") == 0)
        {
            themeResult = SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
            if (FAILED(themeResult))
                SetWindowTheme(hwnd, L"Explorer", NULL);
        }
        else if (wcscmp(className, L"Button") == 0 ||
                 wcscmp(className, L"SysTabControl32") == 0)
        {
            SetWindowTheme(hwnd, L"", L"");
        }
    }

    rootWindow = GetAncestor(hwnd, GA_ROOT);
    if (dark && rootWindow != NULL &&
        (GetWindowLongPtrW(rootWindow, GWL_STYLE) & WS_CAPTION) != 0)
        RmgPluginThemeApplyWindowFrame(rootWindow, TRUE);

    if (dark)
        InvalidateRect(hwnd, NULL, TRUE);
}

static void RmgPluginThemeAttachPagePanel(HWND hwnd)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;
    SetPropW(hwnd, RMG_PLUGIN_DARK_PANEL_PROPERTY, (HANDLE)(ULONG_PTR)1);
    RmgPluginThemeAttach(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
}

static BOOL CALLBACK RmgPluginThemeAttachChildProc(HWND hwnd, LPARAM parameter)
{
    (void)parameter;
    RmgPluginThemeAttach(hwnd);
    return TRUE;
}

/* Attach after the dialog/resource controls exist. EnumChildWindows walks all
   descendants, including the controls inside resource-defined tab pages. */
static void RmgPluginThemeAttachTree(HWND root)
{
    if (root == NULL || !IsWindow(root))
        return;
    RmgPluginThemeAttach(root);
    EnumChildWindows(root, RmgPluginThemeAttachChildProc, 0);
}

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif /* RMG_PLUGIN_THEME_WIN32_H */
