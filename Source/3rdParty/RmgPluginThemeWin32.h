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
   on the calling thread. This is important for DLL-hosted dialogs because the
   process executable does not necessarily carry a v6 manifest of its own. */
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

    pathLength = GetModuleFileNameW(module, modulePath, (DWORD)(sizeof(modulePath) / sizeof(modulePath[0])));
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

static HBRUSH RmgPluginThemeDarkButtonBrush(BOOL pressed, BOOL hot, BOOL disabled)
{
    static HBRUSH normalBrush = NULL;
    static HBRUSH hotBrush = NULL;
    static HBRUSH pressedBrush = NULL;
    static HBRUSH disabledBrush = NULL;

    if (normalBrush == NULL)
        normalBrush = CreateSolidBrush(RGB(65, 68, 73));
    if (hotBrush == NULL)
        hotBrush = CreateSolidBrush(RGB(78, 83, 90));
    if (pressedBrush == NULL)
        pressedBrush = CreateSolidBrush(RGB(43, 47, 52));
    if (disabledBrush == NULL)
        disabledBrush = CreateSolidBrush(RGB(49, 51, 54));

    if (disabled)
        return disabledBrush;
    if (pressed)
        return pressedBrush;
    return hot ? hotBrush : normalBrush;
}

static HBRUSH RmgPluginThemeDarkBorderBrush(void)
{
    static HBRUSH brush = NULL;
    if (brush == NULL)
        brush = CreateSolidBrush(RGB(105, 105, 105));
    return brush;
}

/* Push buttons are converted to owner-draw in dark mode so Windows 7 and
   classic-theme fallbacks do not leave bright button faces in plugin pages.
   Store the original style as a window property for text alignment/default
   button rendering without changing the button's command/notification flow. */
#define RMG_PLUGIN_DARK_BUTTON_PROPERTY L"RMG.Plugin.DarkButton"
#define RMG_PLUGIN_DARK_CHECK_PROPERTY L"RMG.Plugin.DarkCheck"
#define RMG_PLUGIN_DARK_GROUP_PROPERTY L"RMG.Plugin.DarkGroup"
#define RMG_PLUGIN_DARK_COMBO_PROPERTY L"RMG.Plugin.DarkCombo"
#define RMG_PLUGIN_DARK_TRACK_PROPERTY L"RMG.Plugin.DarkTrack"
#define RMG_PLUGIN_DARK_FRAME_PROPERTY L"RMG.Plugin.DarkFrame"
#define RMG_PLUGIN_DARK_PANEL_PROPERTY L"RMG.Plugin.DarkPanel"
#define RMG_PLUGIN_TAB_HOT_PROPERTY L"RMG.Plugin.TabHotItem"

static BOOL RmgPluginThemeDrawDarkButton(const DRAWITEMSTRUCT *drawItem)
{
    HWND button;
    HDC dc;
    RECT borderRect;
    RECT textRect;
    wchar_t text[256];
    HANDLE storedStyle;
    LONG_PTR originalStyle;
    HFONT buttonFont;
    HGDIOBJ oldPen;
    HGDIOBJ oldBrush;
    HPEN borderPen;
    HRGN roundedRegion;
    UINT textFlags;
    BOOL disabled;
    BOOL pressed;
    BOOL hot;
    int borderWidth;
    int savedDC;

    if (drawItem == NULL || drawItem->CtlType != ODT_BUTTON || drawItem->hwndItem == NULL)
        return FALSE;

    button = drawItem->hwndItem;
    storedStyle = GetPropW(button, RMG_PLUGIN_DARK_BUTTON_PROPERTY);
    if (storedStyle == NULL)
        return FALSE;

    dc = drawItem->hDC;
    savedDC = SaveDC(dc);
    borderRect = drawItem->rcItem;
    textRect = borderRect;
    originalStyle = (LONG_PTR)(ULONG_PTR)storedStyle;
    buttonFont = (HFONT)SendMessageW(button, WM_GETFONT, 0, 0);
    if (buttonFont != NULL)
        SelectObject(dc, buttonFont);
    disabled = (drawItem->itemState & ODS_DISABLED) != 0;
    pressed = (drawItem->itemState & ODS_SELECTED) != 0;
    hot = (drawItem->itemState & ODS_HOTLIGHT) != 0;

    /* Draw a softly rounded, subtly raised button instead of the flat
       square fallback used by classic Win32 controls.  Only the painting is
       replaced; the real button window keeps its original command semantics. */
    borderWidth = ((originalStyle & BS_TYPEMASK) == BS_DEFPUSHBUTTON) ? 2 : 1;
    roundedRegion = CreateRoundRectRgn(borderRect.left, borderRect.top,
        borderRect.right + 1, borderRect.bottom + 1, 10, 10);
    if (roundedRegion != NULL)
    {
        SelectClipRgn(dc, roundedRegion);
        FillRect(dc, &borderRect, RmgPluginThemeDarkButtonBrush(pressed, hot, disabled));
        SelectClipRgn(dc, NULL);
        DeleteObject(roundedRegion);
    }
    else
    {
        FillRect(dc, &borderRect, RmgPluginThemeDarkButtonBrush(pressed, hot, disabled));
    }

    borderPen = CreatePen(PS_SOLID, borderWidth,
        disabled ? RGB(82, 84, 87) :
        (((originalStyle & BS_TYPEMASK) == BS_DEFPUSHBUTTON) ? RGB(112, 151, 196) : RGB(105, 111, 119)));
    oldPen = SelectObject(dc, borderPen);
    oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, borderRect.left, borderRect.top, borderRect.right, borderRect.bottom, 10, 10);
    if (oldBrush != NULL)
        SelectObject(dc, oldBrush);
    if (oldPen != NULL)
        SelectObject(dc, oldPen);
    if (borderPen != NULL)
        DeleteObject(borderPen);

    /* A restrained upper highlight and lower edge give the button depth
       without imitating the old raised Windows 95 bevel. */
    if (!disabled && !pressed && borderRect.bottom - borderRect.top > 8)
    {
        HPEN highlightPen = CreatePen(PS_SOLID, 1, hot ? RGB(135, 155, 180) : RGB(91, 97, 105));
        HGDIOBJ previousPen = SelectObject(dc, highlightPen);
        MoveToEx(dc, borderRect.left + 7, borderRect.top + 2, NULL);
        LineTo(dc, borderRect.right - 7, borderRect.top + 2);
        if (previousPen != NULL)
            SelectObject(dc, previousPen);
        if (highlightPen != NULL)
            DeleteObject(highlightPen);
    }

    InflateRect(&textRect, -borderWidth, -borderWidth);

    if (GetWindowTextW(button, text, (int)(sizeof(text) / sizeof(text[0]))) > 0)
    {
        /* Keep the horizontal inset small: the N-Rage directional captions
           (for example, the Russian word "Вправо") are wider than English. */
        InflateRect(&textRect, -2, -2);
        textFlags = DT_VCENTER;
        if ((originalStyle & BS_MULTILINE) != 0)
        {
            textFlags |= DT_WORDBREAK | DT_CENTER;
        }
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
        SetTextColor(dc, disabled ? RGB(145, 145, 145) : RGB(245, 245, 245));
        DrawTextW(dc, text, -1, &textRect, textFlags);
    }

    if ((drawItem->itemState & ODS_FOCUS) != 0 &&
        (drawItem->itemState & ODS_NOFOCUSRECT) == 0)
    {
        InflateRect(&textRect, 2, 1);
        DrawFocusRect(dc, &textRect);
    }

    if (savedDC != 0)
        RestoreDC(dc, savedDC);

    return TRUE;
}

static BOOL RmgPluginThemeDrawTabControl(HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT clientRect;
    RECT pageRect;
    int itemCount;
    int selectedItem;
    int hotItem;
    int i;
    HFONT font;
    HGDIOBJ oldFont;

    if (dc == NULL)
        return FALSE;

    GetClientRect(hwnd, &clientRect);
    FillRect(dc, &clientRect, RmgPluginThemeDarkBrush());

    pageRect = clientRect;
    TabCtrl_AdjustRect(hwnd, FALSE, &pageRect);
    if (pageRect.left < pageRect.right && pageRect.top < pageRect.bottom)
    {
        HPEN pagePen = CreatePen(PS_SOLID, 1, RGB(91, 96, 103));
        HGDIOBJ oldPen = SelectObject(dc, pagePen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, pageRect.left, pageRect.top, pageRect.right, pageRect.bottom, 7, 7);
        if (oldBrush != NULL)
            SelectObject(dc, oldBrush);
        if (oldPen != NULL)
            SelectObject(dc, oldPen);
        if (pagePen != NULL)
            DeleteObject(pagePen);
    }

    itemCount = (int)SendMessageW(hwnd, TCM_GETITEMCOUNT, 0, 0);
    selectedItem = (int)SendMessageW(hwnd, TCM_GETCURSEL, 0, 0);
    {
        HANDLE hotProperty = GetPropW(hwnd, RMG_PLUGIN_TAB_HOT_PROPERTY);
        hotItem = hotProperty != NULL ? (int)(INT_PTR)hotProperty - 1 : -1;
    }
    font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
    oldFont = font != NULL ? SelectObject(dc, font) : NULL;
    SetBkMode(dc, TRANSPARENT);

    for (i = 0; i < itemCount; ++i)
    {
        RECT itemRect;
        RECT textRect;
        wchar_t caption[128] = { 0 };
        TCITEMW item;
        BOOL selected = (i == selectedItem);
        BOOL hot = (i == hotItem);
        COLORREF fillColor = selected ? RGB(70, 74, 80) :
            (hot ? RGB(61, 65, 71) : RGB(43, 46, 51));
        COLORREF borderColor = selected ? RGB(122, 151, 184) : RGB(86, 91, 98);
        HPEN itemPen;
        HBRUSH itemBrush;
        HGDIOBJ previousPen;
        HGDIOBJ previousBrush;

        if (!SendMessageW(hwnd, TCM_GETITEMRECT, (WPARAM)i, (LPARAM)&itemRect))
            continue;

        ZeroMemory(&item, sizeof(item));
        item.mask = TCIF_TEXT;
        item.pszText = caption;
        item.cchTextMax = (int)(sizeof(caption) / sizeof(caption[0]));
        SendMessageW(hwnd, TCM_GETITEMW, (WPARAM)i, (LPARAM)&item);

        itemBrush = CreateSolidBrush(fillColor);
        itemPen = CreatePen(PS_SOLID, selected ? 1 : 1, borderColor);
        previousBrush = SelectObject(dc, itemBrush);
        previousPen = SelectObject(dc, itemPen);
        RoundRect(dc, itemRect.left + 1, itemRect.top + 1,
            itemRect.right - 1, itemRect.bottom + 1, 7, 7);
        if (previousPen != NULL)
            SelectObject(dc, previousPen);
        if (previousBrush != NULL)
            SelectObject(dc, previousBrush);
        if (itemPen != NULL)
            DeleteObject(itemPen);
        if (itemBrush != NULL)
            DeleteObject(itemBrush);

        textRect = itemRect;
        InflateRect(&textRect, -5, -1);
        SetTextColor(dc, IsWindowEnabled(hwnd) ? RGB(240, 242, 245) : RGB(145, 148, 153));
        DrawTextW(dc, caption, -1, &textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (oldFont != NULL)
        SelectObject(dc, oldFont);
    EndPaint(hwnd, &paint);
    return TRUE;
}

static BOOL RmgPluginThemeDrawDarkCheckbox(HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT clientRect;
    RECT indicatorRect;
    RECT textRect;
    wchar_t caption[256] = { 0 };
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    LRESULT checkState = SendMessageW(hwnd, BM_GETCHECK, 0, 0);
    BOOL radio = ((style & BS_TYPEMASK) == BS_RADIOBUTTON ||
        (style & BS_TYPEMASK) == BS_AUTORADIOBUTTON);
    BOOL leftText = (style & BS_LEFTTEXT) != 0 || (style & BS_RIGHTBUTTON) != 0;
    BOOL enabled = IsWindowEnabled(hwnd);
    int indicatorSize = 13;
    int savedDC;
    HFONT font;

    if (dc == NULL)
        return FALSE;

    savedDC = SaveDC(dc);
    GetClientRect(hwnd, &clientRect);
    FillRect(dc, &clientRect, RmgPluginThemeDarkBrush());
    indicatorRect.left = leftText ? clientRect.right - indicatorSize - 2 : clientRect.left + 2;
    indicatorRect.top = clientRect.top + (clientRect.bottom - clientRect.top - indicatorSize) / 2;
    indicatorRect.right = indicatorRect.left + indicatorSize;
    indicatorRect.bottom = indicatorRect.top + indicatorSize;

    if (radio)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, enabled ? RGB(130, 137, 146) : RGB(78, 81, 85));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, indicatorRect.left, indicatorRect.top,
            indicatorRect.right, indicatorRect.bottom);
        if (checkState == BST_CHECKED)
        {
            HBRUSH dotBrush = CreateSolidBrush(enabled ? RGB(121, 169, 218) : RGB(99, 104, 111));
            RECT dotRect = indicatorRect;
            InflateRect(&dotRect, -4, -4);
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
        HPEN boxPen = CreatePen(PS_SOLID, 1, enabled ? RGB(128, 135, 144) : RGB(78, 81, 85));
        HGDIOBJ oldPen = SelectObject(dc, boxPen);
        HGDIOBJ oldBrush = SelectObject(dc, boxBrush);
        RoundRect(dc, indicatorRect.left, indicatorRect.top,
            indicatorRect.right, indicatorRect.bottom, 3, 3);
        if (checkState == BST_CHECKED)
        {
            HPEN tickPen = CreatePen(PS_SOLID, 2, enabled ? RGB(221, 231, 242) : RGB(130, 134, 140));
            HGDIOBJ previousPen = SelectObject(dc, tickPen);
            MoveToEx(dc, indicatorRect.left + 3, indicatorRect.top + 7, NULL);
            LineTo(dc, indicatorRect.left + 6, indicatorRect.top + 10);
            LineTo(dc, indicatorRect.left + 11, indicatorRect.top + 3);
            if (previousPen != NULL)
                SelectObject(dc, previousPen);
            if (tickPen != NULL)
                DeleteObject(tickPen);
        }
        else if (checkState == BST_INDETERMINATE)
        {
            RECT markRect = indicatorRect;
            InflateRect(&markRect, -3, -4);
            FillRect(dc, &markRect, RmgPluginThemeDarkBorderBrush());
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
    SetTextColor(dc, enabled ? RGB(239, 241, 244) : RGB(145, 148, 153));
    DrawTextW(dc, caption, -1, &textRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

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

static BOOL RmgPluginThemeDrawDarkGroupBox(HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT clientRect;
    RECT frameRect;
    RECT captionRect;
    wchar_t caption[256] = { 0 };
    HFONT font;
    SIZE captionSize = { 0, 0 };
    HPEN pen;
    HGDIOBJ oldPen;
    HGDIOBJ oldBrush;

    if (dc == NULL)
        return FALSE;

    GetClientRect(hwnd, &clientRect);
    FillRect(dc, &clientRect, RmgPluginThemeDarkBrush());
    GetWindowTextW(hwnd, caption, (int)(sizeof(caption) / sizeof(caption[0])));
    font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
    if (font != NULL)
        SelectObject(dc, font);
    if (caption[0] != L'\0')
        GetTextExtentPoint32W(dc, caption, (int)wcslen(caption), &captionSize);

    frameRect = clientRect;
    frameRect.top += 6;
    pen = CreatePen(PS_SOLID, 1, RGB(91, 96, 103));
    oldPen = SelectObject(dc, pen);
    oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, frameRect.left, frameRect.top, frameRect.right, frameRect.bottom, 7, 7);
    if (oldBrush != NULL)
        SelectObject(dc, oldBrush);
    if (oldPen != NULL)
        SelectObject(dc, oldPen);
    if (pen != NULL)
        DeleteObject(pen);

    if (caption[0] != L'\0')
    {
        captionRect.left = 9;
        captionRect.top = 0;
        captionRect.right = captionRect.left + captionSize.cx + 8;
        captionRect.bottom = captionSize.cy + 2;
        FillRect(dc, &captionRect, RmgPluginThemeDarkBrush());
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(232, 235, 239));
        TextOutW(dc, 13, 1, caption, (int)wcslen(caption));
    }

    EndPaint(hwnd, &paint);
    return TRUE;
}

static BOOL RmgPluginThemeDrawDarkPanel(HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT clientRect;
    HPEN pen;
    HGDIOBJ oldPen;
    HGDIOBJ oldBrush;

    if (dc == NULL)
        return FALSE;

    GetClientRect(hwnd, &clientRect);
    FillRect(dc, &clientRect, RmgPluginThemeDarkBrush());
    pen = CreatePen(PS_SOLID, 1, RGB(91, 96, 103));
    oldPen = SelectObject(dc, pen);
    oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, clientRect.left, clientRect.top, clientRect.right, clientRect.bottom, 7, 7);
    if (oldBrush != NULL)
        SelectObject(dc, oldBrush);
    if (oldPen != NULL)
        SelectObject(dc, oldPen);
    if (pen != NULL)
        DeleteObject(pen);
    EndPaint(hwnd, &paint);
    return TRUE;
}

static void RmgPluginThemeDrawDarkFrame(HWND hwnd)
{
    HDC dc = GetDC(hwnd);
    RECT clientRect;
    HPEN pen;
    HGDIOBJ oldPen;
    HGDIOBJ oldBrush;

    if (dc == NULL)
        return;
    GetClientRect(hwnd, &clientRect);
    InflateRect(&clientRect, -1, -1);
    pen = CreatePen(PS_SOLID, 1, RGB(91, 96, 103));
    oldPen = SelectObject(dc, pen);
    oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
    if (oldBrush != NULL)
        SelectObject(dc, oldBrush);
    if (oldPen != NULL)
        SelectObject(dc, oldPen);
    if (pen != NULL)
        DeleteObject(pen);
    ReleaseDC(hwnd, dc);
}

static BOOL RmgPluginThemeDrawDarkCombo(HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT clientRect;
    RECT textRect;
    RECT arrowRect;
    wchar_t caption[256] = { 0 };
    BOOL enabled = IsWindowEnabled(hwnd);
    HPEN pen;
    HGDIOBJ oldPen;
    HGDIOBJ oldBrush;
    POINT arrow[3];
    HFONT font;

    if (dc == NULL)
        return FALSE;

    GetClientRect(hwnd, &clientRect);
    FillRect(dc, &clientRect, RmgPluginThemeDarkInputBrush());
    arrowRect = clientRect;
    arrowRect.left = arrowRect.right - 20;

    pen = CreatePen(PS_SOLID, 1, enabled ? RGB(105, 112, 121) : RGB(73, 76, 81));
    oldPen = SelectObject(dc, pen);
    oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, clientRect.left, clientRect.top, clientRect.right, clientRect.bottom, 6, 6);
    if (oldBrush != NULL)
        SelectObject(dc, oldBrush);
    if (oldPen != NULL)
        SelectObject(dc, oldPen);
    if (pen != NULL)
        DeleteObject(pen);

    {
        HPEN separatorPen = CreatePen(PS_SOLID, 1,
            enabled ? RGB(76, 81, 88) : RGB(59, 62, 67));
        HGDIOBJ previousPen = SelectObject(dc, separatorPen);
        MoveToEx(dc, arrowRect.left, arrowRect.top + 3, NULL);
        LineTo(dc, arrowRect.left, arrowRect.bottom - 3);
        if (previousPen != NULL)
            SelectObject(dc, previousPen);
        if (separatorPen != NULL)
            DeleteObject(separatorPen);
    }
    arrow[0].x = arrowRect.left + 6;
    arrow[0].y = arrowRect.top + (arrowRect.bottom - arrowRect.top) / 2 - 2;
    arrow[1].x = arrowRect.left + 14;
    arrow[1].y = arrow[0].y;
    arrow[2].x = arrowRect.left + 10;
    arrow[2].y = arrow[0].y + 5;
    {
        HBRUSH arrowBrush = CreateSolidBrush(enabled ? RGB(222, 226, 232) : RGB(132, 136, 142));
        HGDIOBJ previousBrush = SelectObject(dc, arrowBrush);
        HGDIOBJ previousPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, arrow, 3);
        if (previousPen != NULL)
            SelectObject(dc, previousPen);
        if (previousBrush != NULL)
            SelectObject(dc, previousBrush);
        if (arrowBrush != NULL)
            DeleteObject(arrowBrush);
    }

    GetWindowTextW(hwnd, caption, (int)(sizeof(caption) / sizeof(caption[0])));
    textRect = clientRect;
    textRect.left += 7;
    textRect.right = arrowRect.left - 5;
    textRect.top += 1;
    textRect.bottom -= 1;
    font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
    if (font != NULL)
        SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? RGB(239, 241, 244) : RGB(142, 145, 150));
    DrawTextW(dc, caption, -1, &textRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    EndPaint(hwnd, &paint);
    return TRUE;
}

static BOOL RmgPluginThemeDrawDarkTrackbar(HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT clientRect;
    RECT channelRect;
    RECT thumbRect;
    BOOL vertical;
    BOOL enabled = IsWindowEnabled(hwnd);
    int ticCount;
    int i;

    if (dc == NULL)
        return FALSE;

    GetClientRect(hwnd, &clientRect);
    FillRect(dc, &clientRect, RmgPluginThemeDarkBrush());
    vertical = (GetWindowLongPtrW(hwnd, GWL_STYLE) & TBS_VERT) != 0;
    SendMessageW(hwnd, TBM_GETCHANNELRECT, 0, (LPARAM)&channelRect);

    if (vertical)
    {
        int center = (channelRect.left + channelRect.right) / 2;
        RECT groove = { center - 2, channelRect.top, center + 2, channelRect.bottom };
        FillRect(dc, &groove, RmgPluginThemeDarkInputBrush());
    }
    else
    {
        int center = (channelRect.top + channelRect.bottom) / 2;
        RECT groove = { channelRect.left, center - 2, channelRect.right, center + 2 };
        FillRect(dc, &groove, RmgPluginThemeDarkInputBrush());
    }

    {
        HPEN tickPen = CreatePen(PS_SOLID, 1,
            enabled ? RGB(135, 141, 149) : RGB(75, 79, 84));
        HGDIOBJ previousPen = SelectObject(dc, tickPen);
        ticCount = (int)SendMessageW(hwnd, TBM_GETNUMTICS, 0, 0);
        for (i = 0; i < ticCount && i < 128; ++i)
        {
            int position = (int)SendMessageW(hwnd, TBM_GETTICPOS, (WPARAM)i, 0);
            if (position < 0)
                continue;

            if (vertical)
            {
                int x = (GetWindowLongPtrW(hwnd, GWL_STYLE) & TBS_LEFT)
                    ? channelRect.left - 5 : channelRect.right + 2;
                MoveToEx(dc, x, position, NULL);
                LineTo(dc, x + 3, position);
            }
            else
            {
                int y = (GetWindowLongPtrW(hwnd, GWL_STYLE) & TBS_TOP)
                    ? channelRect.top - 5 : channelRect.bottom + 2;
                MoveToEx(dc, position, y, NULL);
                LineTo(dc, position, y + 3);
            }
        }
        if (previousPen != NULL)
            SelectObject(dc, previousPen);
        if (tickPen != NULL)
            DeleteObject(tickPen);
    }

    SendMessageW(hwnd, TBM_GETTHUMBRECT, 0, (LPARAM)&thumbRect);
    {
        HBRUSH thumbBrush = CreateSolidBrush(
            enabled ? RGB(166, 173, 182) : RGB(91, 95, 101));
        HPEN thumbPen = CreatePen(PS_SOLID, 1,
            enabled ? RGB(211, 216, 223) : RGB(117, 120, 125));
        HGDIOBJ oldBrush = SelectObject(dc, thumbBrush);
        HGDIOBJ oldPen = SelectObject(dc, thumbPen);
        RoundRect(dc, thumbRect.left, thumbRect.top,
            thumbRect.right, thumbRect.bottom, 7, 7);
        if (oldPen != NULL)
            SelectObject(dc, oldPen);
        if (oldBrush != NULL)
            SelectObject(dc, oldBrush);
        if (thumbBrush != NULL)
            DeleteObject(thumbBrush);
        if (thumbPen != NULL)
            DeleteObject(thumbPen);
    }

    EndPaint(hwnd, &paint);
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

    if (message == WM_PAINT)
    {
        if (GetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY) != NULL)
        {
            RmgPluginThemeDrawDarkCheckbox(hwnd);
            return 0;
        }
        if (GetPropW(hwnd, RMG_PLUGIN_DARK_GROUP_PROPERTY) != NULL)
        {
            RmgPluginThemeDrawDarkGroupBox(hwnd);
            return 0;
        }
        if (GetPropW(hwnd, RMG_PLUGIN_DARK_PANEL_PROPERTY) != NULL)
        {
            RmgPluginThemeDrawDarkPanel(hwnd);
            return 0;
        }
        if (GetPropW(hwnd, RMG_PLUGIN_DARK_COMBO_PROPERTY) != NULL)
        {
            RmgPluginThemeDrawDarkCombo(hwnd);
            return 0;
        }
        if (GetPropW(hwnd, RMG_PLUGIN_DARK_TRACK_PROPERTY) != NULL)
        {
            RmgPluginThemeDrawDarkTrackbar(hwnd);
            return 0;
        }
        {
            wchar_t paintClass[64] = { 0 };
            if (GetClassNameW(hwnd, paintClass, (int)(sizeof(paintClass) / sizeof(paintClass[0]))) > 0 &&
                wcscmp(paintClass, L"SysTabControl32") == 0)
            {
                RmgPluginThemeDrawTabControl(hwnd);
                return 0;
            }
        }
    }

    if (message == WM_PAINT && GetPropW(hwnd, RMG_PLUGIN_DARK_FRAME_PROPERTY) != NULL)
    {
        LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        RmgPluginThemeDrawDarkFrame(hwnd);
        return result;
    }

    if (message == WM_DRAWITEM)
    {
        if (RmgPluginThemeDrawDarkButton((const DRAWITEMSTRUCT *)lParam))
            return TRUE;
    }

    if (message == WM_MOUSEMOVE)
    {
        wchar_t controlClass[64] = { 0 };
        if (GetClassNameW(hwnd, controlClass, (int)(sizeof(controlClass) / sizeof(controlClass[0]))) > 0 &&
            wcscmp(controlClass, L"SysTabControl32") == 0)
        {
            TRACKMOUSEEVENT tracking;
            TCHITTESTINFO hit;
            int hotIndex;
            ZeroMemory(&tracking, sizeof(tracking));
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = hwnd;
            TrackMouseEvent(&tracking);
            hit.pt.x = (short)LOWORD(lParam);
            hit.pt.y = (short)HIWORD(lParam);
            hotIndex = (int)SendMessageW(hwnd, TCM_HITTEST, 0, (LPARAM)&hit);
            {
                HANDLE oldHotProperty = GetPropW(hwnd, RMG_PLUGIN_TAB_HOT_PROPERTY);
                int oldHotIndex = oldHotProperty != NULL ? (int)(INT_PTR)oldHotProperty - 1 : -1;
                if (hotIndex != oldHotIndex)
                {
                    if (hotIndex >= 0)
                        SetPropW(hwnd, RMG_PLUGIN_TAB_HOT_PROPERTY, (HANDLE)(INT_PTR)(hotIndex + 1));
                    else
                        RemovePropW(hwnd, RMG_PLUGIN_TAB_HOT_PROPERTY);
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
        }
    }
    else if (message == WM_MOUSELEAVE)
    {
        if (GetPropW(hwnd, RMG_PLUGIN_TAB_HOT_PROPERTY) != NULL)
        {
            RemovePropW(hwnd, RMG_PLUGIN_TAB_HOT_PROPERTY);
            InvalidateRect(hwnd, NULL, TRUE);
        }
    }

    if (message == WM_NOTIFY && lParam != 0)
    {
        LPNMHDR notification = (LPNMHDR)lParam;
        if (notification->code == NM_CUSTOMDRAW && notification->hwndFrom != NULL)
        {
            wchar_t controlClass[64] = { 0 };
            if (GetClassNameW(notification->hwndFrom, controlClass,
                (int)(sizeof(controlClass) / sizeof(controlClass[0]))) > 0)
            {
                LPNMCUSTOMDRAW customDraw = (LPNMCUSTOMDRAW)lParam;
                BOOL headerControl = (wcscmp(controlClass, L"SysHeader32") == 0);
                if (headerControl && customDraw->dwDrawStage == CDDS_PREPAINT)
                    return CDRF_NOTIFYITEMDRAW;
                if (headerControl && customDraw->dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    FillRect(customDraw->hdc, &customDraw->rc, RmgPluginThemeDarkBrush());
                    SetTextColor(customDraw->hdc, RGB(245, 245, 245));
                    SetBkMode(customDraw->hdc, TRANSPARENT);
                    return CDRF_NEWFONT;
                }
            }
        }
    }

    if (message == WM_ERASEBKGND)
    {
        if (GetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY) != NULL ||
            GetPropW(hwnd, RMG_PLUGIN_DARK_GROUP_PROPERTY) != NULL ||
            GetPropW(hwnd, RMG_PLUGIN_DARK_COMBO_PROPERTY) != NULL ||
            GetPropW(hwnd, RMG_PLUGIN_DARK_TRACK_PROPERTY) != NULL ||
            GetPropW(hwnd, RMG_PLUGIN_DARK_PANEL_PROPERTY) != NULL)
            return 1;

        wchar_t className[64] = { 0 };
        if (GetClassNameW(hwnd, className, (int)(sizeof(className) / sizeof(className[0]))) > 0)
        {
            BOOL dialogWindow = (wcscmp(className, L"#32770") == 0);
            BOOL etchedPanel = (wcscmp(className, L"STATIC") == 0 &&
                (GetWindowLongPtrW(hwnd, GWL_STYLE) & SS_TYPEMASK) == SS_ETCHEDFRAME);
            BOOL trackbar = (wcscmp(className, L"msctls_trackbar32") == 0);
            BOOL tabControl = (wcscmp(className, L"SysTabControl32") == 0);
            if (dialogWindow || etchedPanel || trackbar || tabControl)
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

    if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC ||
        message == WM_CTLCOLORBTN || message == WM_CTLCOLOREDIT ||
        message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORSCROLLBAR ||
        message == WM_CTLCOLORMSGBOX)
    {
        HDC dc = (HDC)wParam;
        HWND control = (HWND)lParam;
        BOOL inputControl = (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX);
        COLORREF background = inputControl ? RGB(25, 25, 25) : RGB(53, 53, 53);
        COLORREF foreground = (control != NULL && !IsWindowEnabled(control))
            ? RGB(145, 145, 145) : RGB(245, 245, 245);

        SetTextColor(dc, foreground);
        SetBkColor(dc, background);
        SetBkMode(dc, OPAQUE);
        return (LRESULT)(inputControl ? RmgPluginThemeDarkInputBrush() : RmgPluginThemeDarkBrush());
    }

    if (message == WM_NCDESTROY)
    {
        RemovePropW(hwnd, RMG_PLUGIN_DARK_BUTTON_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_GROUP_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_COMBO_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_TRACK_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_FRAME_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_PANEL_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_TAB_HOT_PROPERTY);
        RemoveWindowSubclass(hwnd, RmgPluginThemeSubclassProc, subclassId);
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    {
        LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        if (message == BM_SETCHECK || message == CB_SETCURSEL ||
            message == TBM_SETPOS || message == TCM_INSERTITEMW ||
            message == TCM_DELETEITEM || message == TCM_SETCURSEL ||
            message == WM_ENABLE || message == WM_SETTEXT)
            InvalidateRect(hwnd, NULL, TRUE);
        return result;
    }
}

static void RmgPluginThemeAttach(HWND hwnd)
{
    BOOL dark;
    wchar_t className[64] = { 0 };

    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    dark = RmgPluginThemeIsDark();
    if (dark)
    {
        SetWindowSubclass(hwnd, RmgPluginThemeSubclassProc, (UINT_PTR)0x524D47u, 0);

        if (GetClassNameW(hwnd, className, (int)(sizeof(className) / sizeof(className[0]))) > 0 &&
            wcscmp(className, L"Button") == 0)
        {
            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            LONG_PTR buttonType = style & BS_TYPEMASK;
            if (buttonType == BS_PUSHBUTTON || buttonType == BS_DEFPUSHBUTTON)
            {
                SetPropW(hwnd, RMG_PLUGIN_DARK_BUTTON_PROPERTY, (HANDLE)(ULONG_PTR)style);
                SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
                if (buttonType == BS_DEFPUSHBUTTON)
                {
                    HWND parent = GetParent(hwnd);
                    wchar_t parentClass[64] = { 0 };
                    if (parent != NULL &&
                        GetClassNameW(parent, parentClass,
                            (int)(sizeof(parentClass) / sizeof(parentClass[0]))) > 0 &&
                        wcscmp(parentClass, L"#32770") == 0)
                    {
                        SendMessageW(parent, DM_SETDEFID, (WPARAM)GetDlgCtrlID(hwnd), 0);
                    }
                }
                InvalidateRect(hwnd, NULL, TRUE);
            }
            else if (buttonType == BS_GROUPBOX)
            {
                SetPropW(hwnd, RMG_PLUGIN_DARK_GROUP_PROPERTY, (HANDLE)(ULONG_PTR)1);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            else if (buttonType == BS_CHECKBOX || buttonType == BS_AUTOCHECKBOX ||
                buttonType == BS_3STATE || buttonType == BS_AUTO3STATE ||
                buttonType == BS_RADIOBUTTON || buttonType == BS_AUTORADIOBUTTON)
            {
                /* Paint only the checkbox/radio indicator. Keep the original
                   button type so Windows continues to toggle its check state. */
                SetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY, (HANDLE)(ULONG_PTR)1);
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }

        if (wcscmp(className, L"SysListView32") == 0)
        {
            SendMessageW(hwnd, LVM_SETBKCOLOR, 0, (LPARAM)RGB(25, 25, 25));
            SendMessageW(hwnd, LVM_SETTEXTCOLOR, 0, (LPARAM)RGB(245, 245, 245));
            SendMessageW(hwnd, LVM_SETTEXTBKCOLOR, 0, (LPARAM)RGB(25, 25, 25));
        }
        else if (wcscmp(className, L"STATIC") == 0)
        {
            LONG_PTR staticType = GetWindowLongPtrW(hwnd, GWL_STYLE) & SS_TYPEMASK;
            LONG_PTR extendedStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if (staticType == SS_ETCHEDFRAME || staticType == SS_ETCHEDHORZ || staticType == SS_ETCHEDVERT)
            {
                SetPropW(hwnd, RMG_PLUGIN_DARK_PANEL_PROPERTY, (HANDLE)(ULONG_PTR)1);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            else if ((extendedStyle & (WS_EX_STATICEDGE | WS_EX_CLIENTEDGE)) != 0)
            {
                /* Remove the system's bright 3-D edge and replace it with a
                   single dark border after the static text has been painted. */
                SetPropW(hwnd, RMG_PLUGIN_DARK_FRAME_PROPERTY, (HANDLE)(ULONG_PTR)1);
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                    extendedStyle & ~(WS_EX_STATICEDGE | WS_EX_CLIENTEDGE));
                SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                    SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        else if (wcscmp(className, L"ComboBox") == 0)
        {
            LONG_PTR comboType = GetWindowLongPtrW(hwnd, GWL_STYLE) & 0x0003;
            if (comboType == CBS_DROPDOWNLIST)
            {
                SetPropW(hwnd, RMG_PLUGIN_DARK_COMBO_PROPERTY, (HANDLE)(ULONG_PTR)1);
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        else if (wcscmp(className, L"msctls_trackbar32") == 0)
        {
            SetPropW(hwnd, RMG_PLUGIN_DARK_TRACK_PROPERTY, (HANDLE)(ULONG_PTR)1);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        else if (wcscmp(className, L"SysTabControl32") == 0)
        {
            RemovePropW(hwnd, RMG_PLUGIN_TAB_HOT_PROPERTY);
            InvalidateRect(hwnd, NULL, TRUE);
        }
    }

    /* In dark mode do not fall back to the Explorer theme: on older Windows
       versions it paints native controls with light faces. Disable visual
       theming and let the custom control painters / CTLCOLOR handlers provide
       consistent colors. Keep Explorer styling for light mode. */
    SetWindowTheme(hwnd, dark ? L"" : L"Explorer", dark ? L"" : NULL);

    if (dark)
        InvalidateRect(hwnd, NULL, TRUE);
}

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif /* RMG_PLUGIN_THEME_WIN32_H */
