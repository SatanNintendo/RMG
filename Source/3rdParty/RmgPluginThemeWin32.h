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

#define RMG_PLUGIN_DARK_BUTTON_PROPERTY L"RMG.Plugin.DarkButton"
#define RMG_PLUGIN_DARK_GROUP_PROPERTY  L"RMG.Plugin.DarkGroup"
#define RMG_PLUGIN_DARK_CHECK_PROPERTY  L"RMG.Plugin.DarkCheck"
#define RMG_PLUGIN_DARK_COMBO_PROPERTY  L"RMG.Plugin.DarkCombo"
#define RMG_PLUGIN_DARK_PANEL_PROPERTY  L"RMG.Plugin.DarkPanel"
#define RMG_PLUGIN_DARK_TAB_PROPERTY    L"RMG.Plugin.DarkTab"
#define RMG_PLUGIN_DARK_HEADER_PROPERTY L"RMG.Plugin.DarkHeader"
#define RMG_PLUGIN_DARK_FRAME_PROPERTY  L"RMG.Plugin.DarkFrame"
#define RMG_PLUGIN_BUTTON_HOT_PROPERTY L"RMG.Plugin.ButtonHot"
#define RMG_PLUGIN_DEFAULT_ID_SUPPORT_PROPERTY L"RMG.Plugin.DefaultIdSupport"
#define RMG_PLUGIN_DWM_PROPERTY        L"RMG.Plugin.DwmApplied"

#ifndef WM_QUERYUISTATE
#define WM_QUERYUISTATE 0x0129
#endif
#ifndef UISF_HIDEFOCUS
#define UISF_HIDEFOCUS 0x1
#endif

#define RMG_GRAY(v) RGB((v), (v), (v))
#define RMG_ACCENT_COLOR RGB(42, 130, 218)

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


/* ------------------------------------------------------------------------
   Shared "Fusion" visual language.

   Push buttons (both themes) and dark combo boxes use the same flat,
   slightly rounded surface as the Qt Fusion style used by the host and by
   the AngryLion / ParaLLEl configuration pages: a 1px outline, a 1px inner
   contrast line and a subtle vertical gradient. The dark values below were
   measured from the AngryLion RDP Plus dialog.
   ------------------------------------------------------------------------ */

typedef struct RmgPluginThemeButtonColors
{
    COLORREF outline;
    COLORREF top;
    COLORREF side;
    COLORREF bottom;
    COLORREF faceTop;
    COLORREF faceBottom;
    COLORREF text;
} RmgPluginThemeButtonColors;

typedef struct RmgPluginThemeBuffer
{
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ oldBitmap;
    int width;
    int height;
} RmgPluginThemeBuffer;

static void RmgPluginThemeFillColor(HDC dc, int left, int top, int right, int bottom, COLORREF color)
{
    RECT rect;
    if (right <= left || bottom <= top)
        return;
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    SetBkColor(dc, color);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rect, NULL, 0, NULL);
}

static COLORREF RmgPluginThemeBlend(COLORREF first, COLORREF second, int firstPercent)
{
    int r = (GetRValue(first) * firstPercent + GetRValue(second) * (100 - firstPercent)) / 100;
    int g = (GetGValue(first) * firstPercent + GetGValue(second) * (100 - firstPercent)) / 100;
    int b = (GetBValue(first) * firstPercent + GetBValue(second) * (100 - firstPercent)) / 100;
    return RGB(r, g, b);
}

static COLORREF RmgPluginThemeLerp(COLORREF from, COLORREF to, int step, int steps)
{
    int r, g, b;
    if (steps <= 0)
        return from;
    r = GetRValue(from) + ((int)GetRValue(to) - (int)GetRValue(from)) * step / steps;
    g = GetGValue(from) + ((int)GetGValue(to) - (int)GetGValue(from)) * step / steps;
    b = GetBValue(from) + ((int)GetBValue(to) - (int)GetBValue(from)) * step / steps;
    return RGB(r, g, b);
}

static void RmgPluginThemeGetButtonColors(BOOL dark, BOOL pressed, BOOL hot,
    BOOL disabled, BOOL focused, RmgPluginThemeButtonColors *c)
{
    if (dark)
    {
        if (disabled)
        {
            c->outline = RMG_GRAY(46); c->top = RMG_GRAY(64); c->side = RMG_GRAY(62);
            c->bottom = RMG_GRAY(60); c->faceTop = RMG_GRAY(60); c->faceBottom = RMG_GRAY(58);
            c->text = RMG_GRAY(128);
        }
        else if (pressed)
        {
            c->outline = RMG_GRAY(30); c->top = RMG_GRAY(50); c->side = RMG_GRAY(58);
            c->bottom = RMG_GRAY(66); c->faceTop = RMG_GRAY(49); c->faceBottom = RMG_GRAY(58);
            c->text = RMG_GRAY(245);
        }
        else if (hot)
        {
            c->outline = RMG_GRAY(38); c->top = RMG_GRAY(110); c->side = RMG_GRAY(103);
            c->bottom = RMG_GRAY(98); c->faceTop = RMG_GRAY(84); c->faceBottom = RMG_GRAY(72);
            c->text = RMG_GRAY(255);
        }
        else
        {
            c->outline = RMG_GRAY(38); c->top = RMG_GRAY(96); c->side = RMG_GRAY(91);
            c->bottom = RMG_GRAY(86); c->faceTop = RMG_GRAY(75); c->faceBottom = RMG_GRAY(63);
            c->text = RMG_GRAY(240);
        }
    }
    else
    {
        if (disabled)
        {
            c->outline = RMG_GRAY(200); c->top = RMG_GRAY(250); c->side = RMG_GRAY(247);
            c->bottom = RMG_GRAY(245); c->faceTop = RMG_GRAY(243); c->faceBottom = RMG_GRAY(238);
            c->text = RMG_GRAY(140);
        }
        else if (pressed)
        {
            c->outline = RMG_GRAY(140); c->top = RMG_GRAY(196); c->side = RMG_GRAY(208);
            c->bottom = RMG_GRAY(228); c->faceTop = RMG_GRAY(205); c->faceBottom = RMG_GRAY(222);
            c->text = RMG_GRAY(0);
        }
        else if (hot)
        {
            c->outline = RMG_GRAY(140); c->top = RMG_GRAY(255); c->side = RMG_GRAY(255);
            c->bottom = RMG_GRAY(250); c->faceTop = RMG_GRAY(252); c->faceBottom = RMG_GRAY(238);
            c->text = RMG_GRAY(0);
        }
        else
        {
            c->outline = RMG_GRAY(165); c->top = RMG_GRAY(255); c->side = RMG_GRAY(251);
            c->bottom = RMG_GRAY(247); c->faceTop = RMG_GRAY(246); c->faceBottom = RMG_GRAY(230);
            c->text = RMG_GRAY(0);
        }
    }

    if (focused && !disabled)
        c->outline = RMG_ACCENT_COLOR;
}

static void RmgPluginThemeDrawFusionSurface(HDC dc, const RECT *rc,
    const RmgPluginThemeButtonColors *c, COLORREF parentColor)
{
    int l = rc->left, t = rc->top, r = rc->right, b = rc->bottom;
    int w = r - l, h = b - t;
    int innerHeight, y;
    COLORREF corner;

    if (w < 5 || h < 5)
    {
        RmgPluginThemeFillColor(dc, l, t, r, b, c->outline);
        RmgPluginThemeFillColor(dc, l + 1, t + 1, r - 1, b - 1, c->faceTop);
        return;
    }

    corner = RmgPluginThemeBlend(c->outline, parentColor, 50);

    /* Outline without the four corner pixels, which are blended to fake a
       small anti-aliased radius like the Qt style. */
    RmgPluginThemeFillColor(dc, l + 1, t, r - 1, t + 1, c->outline);
    RmgPluginThemeFillColor(dc, l + 1, b - 1, r - 1, b, c->outline);
    RmgPluginThemeFillColor(dc, l, t + 1, l + 1, b - 1, c->outline);
    RmgPluginThemeFillColor(dc, r - 1, t + 1, r, b - 1, c->outline);
    SetPixelV(dc, l, t, corner);
    SetPixelV(dc, r - 1, t, corner);
    SetPixelV(dc, l, b - 1, corner);
    SetPixelV(dc, r - 1, b - 1, corner);

    innerHeight = h - 2;
    for (y = 0; y < innerHeight; ++y)
        RmgPluginThemeFillColor(dc, l + 1, t + 1 + y, r - 1, t + 2 + y,
            RmgPluginThemeLerp(c->faceTop, c->faceBottom, y, innerHeight - 1));

    RmgPluginThemeFillColor(dc, l + 2, t + 1, r - 2, t + 2, c->top);
    RmgPluginThemeFillColor(dc, l + 2, b - 2, r - 2, b - 1, c->bottom);
    RmgPluginThemeFillColor(dc, l + 1, t + 2, l + 2, b - 2, c->side);
    RmgPluginThemeFillColor(dc, r - 2, t + 2, r - 1, b - 2, c->side);
    SetPixelV(dc, l + 1, t + 1, RmgPluginThemeBlend(c->top, c->outline, 50));
    SetPixelV(dc, r - 2, t + 1, RmgPluginThemeBlend(c->top, c->outline, 50));
    SetPixelV(dc, l + 1, b - 2, RmgPluginThemeBlend(c->bottom, c->outline, 50));
    SetPixelV(dc, r - 2, b - 2, RmgPluginThemeBlend(c->bottom, c->outline, 50));
}

static void RmgPluginThemeDrawChevron(HDC dc, int cx, int cy, COLORREF color, COLORREF background)
{
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN softPen = CreatePen(PS_SOLID, 1, RmgPluginThemeBlend(color, background, 45));
    HGDIOBJ oldPen = SelectObject(dc, softPen);

    MoveToEx(dc, cx - 3, cy - 2, NULL);
    LineTo(dc, cx, cy + 1);
    LineTo(dc, cx + 4, cy - 3);
    SelectObject(dc, pen);
    MoveToEx(dc, cx - 3, cy - 1, NULL);
    LineTo(dc, cx, cy + 2);
    LineTo(dc, cx + 4, cy - 2);
    if (oldPen != NULL)
        SelectObject(dc, oldPen);
    DeleteObject(pen);
    DeleteObject(softPen);
}

static HDC RmgPluginThemeBufferBegin(HDC target, int width, int height, RmgPluginThemeBuffer *buffer)
{
    buffer->dc = NULL;
    buffer->bitmap = NULL;
    buffer->oldBitmap = NULL;
    buffer->width = width;
    buffer->height = height;
    if (target == NULL || width <= 0 || height <= 0)
        return NULL;
    buffer->dc = CreateCompatibleDC(target);
    if (buffer->dc == NULL)
        return NULL;
    buffer->bitmap = CreateCompatibleBitmap(target, width, height);
    if (buffer->bitmap == NULL)
    {
        DeleteDC(buffer->dc);
        buffer->dc = NULL;
        return NULL;
    }
    buffer->oldBitmap = SelectObject(buffer->dc, buffer->bitmap);
    return buffer->dc;
}

static void RmgPluginThemeBufferEnd(RmgPluginThemeBuffer *buffer, HDC target, int x, int y)
{
    if (buffer->dc == NULL)
        return;
    BitBlt(target, x, y, buffer->width, buffer->height, buffer->dc, 0, 0, SRCCOPY);
    SelectObject(buffer->dc, buffer->oldBitmap);
    DeleteObject(buffer->bitmap);
    DeleteDC(buffer->dc);
    buffer->dc = NULL;
}

static void RmgPluginThemeSelectControlFont(HWND hwnd, HDC dc)
{
    HFONT font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
    if (font == NULL)
        font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (font != NULL)
        SelectObject(dc, font);
}

static BOOL RmgPluginThemeFocusHidden(HWND hwnd)
{
    return (LOWORD(SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0)) & UISF_HIDEFOCUS) != 0;
}

static void RmgPluginThemeGetComboItemText(HWND combo, int index, wchar_t *text, int count)
{
    text[0] = L'\0';
    if (index < 0)
        return;
    if (IsWindowUnicode(combo))
    {
        LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)index, 0);
        if (length >= 0 && length < (LRESULT)count)
            SendMessageW(combo, CB_GETLBTEXT, (WPARAM)index, (LPARAM)text);
    }
    else
    {
        char ansiText[256] = { 0 };
        LRESULT length = SendMessageA(combo, CB_GETLBTEXTLEN, (WPARAM)index, 0);
        if (length >= 0 && length < (LRESULT)sizeof(ansiText))
        {
            SendMessageA(combo, CB_GETLBTEXT, (WPARAM)index, (LPARAM)ansiText);
            MultiByteToWideChar(CP_ACP, 0, ansiText, -1, text, count);
        }
    }
}

/* Draws a caption inside a rectangle. Multi-line captions are centred
   vertically as a block (DT_VCENTER only works for a single line). */
static void RmgPluginThemeDrawCaption(HDC dc, const RECT *rc, const wchar_t *text,
    BOOL multiline, UINT align, BOOL hidePrefix)
{
    RECT target = *rc;
    UINT flags = align | (hidePrefix ? DT_HIDEPREFIX : 0);

    if (multiline)
    {
        RECT calc = *rc;
        int textHeight, boxHeight;
        flags |= DT_WORDBREAK;
        DrawTextW(dc, text, -1, &calc, flags | DT_CALCRECT);
        textHeight = calc.bottom - calc.top;
        boxHeight = rc->bottom - rc->top;
        if (textHeight < boxHeight)
        {
            target.top = rc->top + (boxHeight - textHeight) / 2;
            target.bottom = target.top + textHeight;
        }
        DrawTextW(dc, text, -1, &target, flags);
    }
    else
    {
        DrawTextW(dc, text, -1, &target,
            flags | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

/* Owner-drawn push button (both themes). */
static BOOL RmgPluginThemeDrawButton(const DRAWITEMSTRUCT *drawItem)
{
    HWND button;
    RECT local;
    RECT textRect;
    wchar_t text[256] = { 0 };
    HANDLE storedStyle;
    LONG_PTR originalStyle;
    RmgPluginThemeButtonColors colors;
    RmgPluginThemeBuffer buffer;
    HDC dc;
    HDC target;
    COLORREF parentColor;
    BOOL dark, disabled, pressed, hot, focused;
    UINT align;
    int width, height, savedDC;

    if (drawItem == NULL || drawItem->CtlType != ODT_BUTTON || drawItem->hwndItem == NULL)
        return FALSE;

    button = drawItem->hwndItem;
    storedStyle = GetPropW(button, RMG_PLUGIN_DARK_BUTTON_PROPERTY);
    if (storedStyle == NULL)
        return FALSE;

    dark = RmgPluginThemeIsDark();
    originalStyle = (LONG_PTR)(ULONG_PTR)storedStyle;
    target = drawItem->hDC;
    width = drawItem->rcItem.right - drawItem->rcItem.left;
    height = drawItem->rcItem.bottom - drawItem->rcItem.top;
    if (width <= 0 || height <= 0)
        return TRUE;

    dc = RmgPluginThemeBufferBegin(target, width, height, &buffer);
    if (dc == NULL)
    {
        dc = target;
        local = drawItem->rcItem;
    }
    else
    {
        local.left = 0; local.top = 0; local.right = width; local.bottom = height;
    }

    savedDC = SaveDC(dc);
    disabled = (drawItem->itemState & ODS_DISABLED) != 0;
    pressed = (drawItem->itemState & ODS_SELECTED) != 0;
    hot = (drawItem->itemState & ODS_HOTLIGHT) != 0 ||
        GetPropW(button, RMG_PLUGIN_BUTTON_HOT_PROPERTY) != NULL;
    focused = (drawItem->itemState & ODS_FOCUS) != 0 &&
        (drawItem->itemState & ODS_NOFOCUSRECT) == 0;

    /* Background behind the rounded corners. */
    if (dark)
    {
        parentColor = RGB(53, 53, 53);
        RmgPluginThemeFillColor(dc, local.left, local.top, local.right, local.bottom, parentColor);
    }
    else
    {
        if (FAILED(DrawThemeParentBackground(button, dc, &local)))
            RmgPluginThemeFillColor(dc, local.left, local.top, local.right, local.bottom,
                GetSysColor(COLOR_BTNFACE));
        parentColor = GetPixel(dc, local.left, local.top);
        if (parentColor == CLR_INVALID)
            parentColor = GetSysColor(COLOR_BTNFACE);
    }

    RmgPluginThemeGetButtonColors(dark, pressed, hot, disabled, focused, &colors);
    RmgPluginThemeDrawFusionSurface(dc, &local, &colors, parentColor);

    if (GetWindowTextW(button, text, (int)(sizeof(text) / sizeof(text[0]))) > 0)
    {
        RmgPluginThemeSelectControlFont(button, dc);
        textRect = local;
        InflateRect(&textRect, -2, -1);
        switch (originalStyle & BS_CENTER)
        {
        case BS_LEFT:  align = DT_LEFT;   break;
        case BS_RIGHT: align = DT_RIGHT;  break;
        default:       align = DT_CENTER; break;
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, colors.text);
        RmgPluginThemeDrawCaption(dc, &textRect, text,
            (originalStyle & BS_MULTILINE) != 0, align,
            (drawItem->itemState & ODS_NOACCEL) != 0);
    }

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
    if (dc != target)
        RmgPluginThemeBufferEnd(&buffer, target, drawItem->rcItem.left, drawItem->rcItem.top);
    return TRUE;
}

/* Dark check boxes and radio buttons. No focus rectangle is drawn around the
   caption (it spanned the full control width and stayed visible after a
   click); keyboard focus is shown as an accent outline on the indicator. */
static void RmgPluginThemePaintDarkCheckbox(HWND hwnd, HDC dc, const RECT *client)
{
    RECT indicatorRect;
    RECT textRect;
    wchar_t caption[256] = { 0 };
    LONG_PTR style;
    LRESULT checkState;
    BOOL radio, leftText, enabled, pushLike, focusCue;
    int indicatorSize = 14;
    int savedDC = SaveDC(dc);

    style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    checkState = SendMessageW(hwnd, BM_GETCHECK, 0, 0);
    radio = ((style & BS_TYPEMASK) == BS_RADIOBUTTON ||
        (style & BS_TYPEMASK) == BS_AUTORADIOBUTTON);
    pushLike = (style & BS_PUSHLIKE) != 0;
    leftText = (style & BS_LEFTTEXT) != 0 || (style & BS_RIGHTBUTTON) != 0;
    enabled = IsWindowEnabled(hwnd);
    focusCue = enabled && GetFocus() == hwnd && !RmgPluginThemeFocusHidden(hwnd);

    FillRect(dc, client, RmgPluginThemeDarkBrush());
    GetWindowTextW(hwnd, caption, (int)(sizeof(caption) / sizeof(caption[0])));
    RmgPluginThemeSelectControlFont(hwnd, dc);
    SetBkMode(dc, TRANSPARENT);

    if (pushLike)
    {
        RmgPluginThemeButtonColors colors;
        BOOL hot = GetPropW(hwnd, RMG_PLUGIN_BUTTON_HOT_PROPERTY) != NULL;
        RmgPluginThemeGetButtonColors(TRUE, checkState == BST_CHECKED, hot, !enabled, focusCue, &colors);
        RmgPluginThemeDrawFusionSurface(dc, client, &colors, RGB(53, 53, 53));
        SetTextColor(dc, colors.text);
        textRect = *client;
        InflateRect(&textRect, -3, -2);
        DrawTextW(dc, caption, -1, &textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else
    {
        COLORREF boxOutline = focusCue ? RMG_ACCENT_COLOR : RMG_GRAY(enabled ? 78 : 62);
        COLORREF boxFill = RMG_GRAY(enabled ? 25 : 42);
        COLORREF markColor = RMG_GRAY(enabled ? 240 : 125);
        HPEN pen;
        HBRUSH brush;
        HGDIOBJ oldPen;
        HGDIOBJ oldBrush;

        if (indicatorSize > (client->right - client->left - 4))
            indicatorSize = client->right - client->left - 4;
        if (indicatorSize > (client->bottom - client->top - 2))
            indicatorSize = client->bottom - client->top - 2;
        if (indicatorSize < 5)
            indicatorSize = 5;

        indicatorRect.left = leftText ? client->right - indicatorSize - 2 : client->left + 2;
        indicatorRect.top = client->top + ((client->bottom - client->top - indicatorSize) / 2);
        indicatorRect.right = indicatorRect.left + indicatorSize;
        indicatorRect.bottom = indicatorRect.top + indicatorSize;

        pen = CreatePen(PS_SOLID, 1, boxOutline);
        brush = CreateSolidBrush(boxFill);
        oldPen = SelectObject(dc, pen);
        oldBrush = SelectObject(dc, brush);
        if (radio)
            Ellipse(dc, indicatorRect.left, indicatorRect.top,
                indicatorRect.right, indicatorRect.bottom);
        else
            RoundRect(dc, indicatorRect.left, indicatorRect.top,
                indicatorRect.right, indicatorRect.bottom, 4, 4);
        if (oldBrush != NULL) SelectObject(dc, oldBrush);
        if (oldPen != NULL) SelectObject(dc, oldPen);
        DeleteObject(pen);
        DeleteObject(brush);

        if (radio)
        {
            if (checkState == BST_CHECKED)
            {
                RECT dot = indicatorRect;
                int inset = indicatorSize / 4 + 1;
                HBRUSH dotBrush = CreateSolidBrush(markColor);
                HPEN dotPen = CreatePen(PS_SOLID, 1, markColor);
                InflateRect(&dot, -inset, -inset);
                oldPen = SelectObject(dc, dotPen);
                oldBrush = SelectObject(dc, dotBrush);
                Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
                if (oldBrush != NULL) SelectObject(dc, oldBrush);
                if (oldPen != NULL) SelectObject(dc, oldPen);
                DeleteObject(dotBrush);
                DeleteObject(dotPen);
            }
        }
        else if (checkState == BST_CHECKED)
        {
            HPEN tickPen = CreatePen(PS_SOLID, 2, markColor);
            oldPen = SelectObject(dc, tickPen);
            MoveToEx(dc, indicatorRect.left + (indicatorSize / 5) + 1,
                indicatorRect.top + (indicatorSize / 2), NULL);
            LineTo(dc, indicatorRect.left + (indicatorSize / 2),
                indicatorRect.top + ((indicatorSize * 3) / 4));
            LineTo(dc, indicatorRect.right - 3,
                indicatorRect.top + (indicatorSize / 4) + 1);
            if (oldPen != NULL) SelectObject(dc, oldPen);
            DeleteObject(tickPen);
        }
        else if (checkState == BST_INDETERMINATE)
        {
            RECT mark = indicatorRect;
            int inset = indicatorSize / 4 + 1;
            InflateRect(&mark, -inset, -inset);
            RmgPluginThemeFillColor(dc, mark.left, mark.top, mark.right, mark.bottom, markColor);
        }

        textRect = *client;
        if (leftText)
            textRect.right = indicatorRect.left - 5;
        else
            textRect.left = indicatorRect.right + 6;
        SetTextColor(dc, RMG_GRAY(enabled ? 240 : 128));
        DrawTextW(dc, caption, -1, &textRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
}

static BOOL RmgPluginThemeDrawDarkCheckbox(HWND hwnd)
{
    PAINTSTRUCT paint;
    RECT client;
    RmgPluginThemeBuffer buffer;
    HDC target = BeginPaint(hwnd, &paint);
    HDC dc;

    if (target == NULL)
        return FALSE;
    GetClientRect(hwnd, &client);
    dc = RmgPluginThemeBufferBegin(target, client.right - client.left,
        client.bottom - client.top, &buffer);
    if (dc == NULL)
        dc = target;
    RmgPluginThemePaintDarkCheckbox(hwnd, dc, &client);
    if (dc != target)
        RmgPluginThemeBufferEnd(&buffer, target, 0, 0);
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
    /* Only the frame and the caption are painted. The interior is the page
       background and holds sibling controls (check boxes, sliders, combos);
       filling it would wipe them out whenever the group box is repainted. */
    frameRect = drawItem->rcItem;
    frameRect.top += 7;
    GetWindowTextW(groupBox, caption, (int)(sizeof(caption) / sizeof(caption[0])));
    RmgPluginThemeSelectControlFont(groupBox, dc);
    if (caption[0] != L'\0')
        GetTextExtentPoint32W(dc, caption, (int)wcslen(caption), &captionSize);

    pen = CreatePen(PS_SOLID, 1, RMG_GRAY(88));
    oldPen = SelectObject(dc, pen);
    oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, frameRect.left, frameRect.top,
        frameRect.right, frameRect.bottom, 5, 5);
    if (oldBrush != NULL) SelectObject(dc, oldBrush);
    if (oldPen != NULL) SelectObject(dc, oldPen);
    DeleteObject(pen);

    if (caption[0] != L'\0')
    {
        captionRect.left = frameRect.left + 10;
        captionRect.top = drawItem->rcItem.top;
        captionRect.right = captionRect.left + captionSize.cx + 8;
        captionRect.bottom = captionRect.top + captionSize.cy + 3;
        FillRect(dc, &captionRect, RmgPluginThemeDarkBrush());
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RMG_GRAY(235));
        TextOutW(dc, captionRect.left + 4, captionRect.top + 1,
            caption, (int)wcslen(caption));
    }

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
    return TRUE;
}

/* Drop-down list items of dark combo boxes (the closed face is painted by
   RmgPluginThemeDrawDarkComboControl). */
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

    selected = (drawItem->itemState & ODS_SELECTED) != 0 &&
        (drawItem->itemState & ODS_COMBOBOXEDIT) == 0;
    disabled = !IsWindowEnabled(combo);

    if (selected)
        RmgPluginThemeFillColor(dc, itemRect.left, itemRect.top, itemRect.right, itemRect.bottom, RMG_ACCENT_COLOR);
    else
        FillRect(dc, &itemRect, RmgPluginThemeDarkInputBrush());
    RmgPluginThemeGetComboItemText(combo, itemIndex, text, (int)(sizeof(text) / sizeof(text[0])));
    RmgPluginThemeSelectControlFont(combo, dc);

    textRect = itemRect;
    InflateRect(&textRect, -7, -1);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? RMG_GRAY(128) : RMG_GRAY(selected ? 255 : 240));
    DrawTextW(dc, text, -1, &textRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
    return TRUE;
}

/* Closed face of a dark CBS_DROPDOWNLIST control, in the same Fusion style
   as the push buttons: button surface, left-aligned caption and a chevron. */
static void RmgPluginThemePaintDarkCombo(HWND combo, HDC dc, const RECT *client)
{
    RmgPluginThemeButtonColors colors;
    RECT textRect;
    wchar_t text[256] = { 0 };
    BOOL enabled = IsWindowEnabled(combo);
    BOOL dropped = SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) != 0;
    BOOL focusCue = enabled && GetFocus() == combo && !RmgPluginThemeFocusHidden(combo);
    BOOL hot = GetPropW(combo, RMG_PLUGIN_BUTTON_HOT_PROPERTY) != NULL;
    int selection = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    int savedDC = SaveDC(dc);

    FillRect(dc, client, RmgPluginThemeDarkBrush());
    RmgPluginThemeGetButtonColors(TRUE, dropped, hot && !dropped, !enabled, focusCue, &colors);
    RmgPluginThemeDrawFusionSurface(dc, client, &colors, RGB(53, 53, 53));

    if (selection >= 0)
        RmgPluginThemeGetComboItemText(combo, selection, text, (int)(sizeof(text) / sizeof(text[0])));
    else if (IsWindowUnicode(combo))
        GetWindowTextW(combo, text, (int)(sizeof(text) / sizeof(text[0])));

    RmgPluginThemeSelectControlFont(combo, dc);
    textRect = *client;
    textRect.left += 8;
    textRect.right -= 22;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colors.text);
    DrawTextW(dc, text, -1, &textRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RmgPluginThemeDrawChevron(dc, client->right - 12,
        client->top + (client->bottom - client->top) / 2,
        RMG_GRAY(enabled ? 190 : 110), colors.faceBottom);

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
}

static BOOL RmgPluginThemeDrawDarkComboControl(HWND combo)
{
    PAINTSTRUCT paint;
    RECT client;
    RmgPluginThemeBuffer buffer;
    HDC target;
    HDC dc;

    if (combo == NULL || GetPropW(combo, RMG_PLUGIN_DARK_COMBO_PROPERTY) == NULL)
        return FALSE;
    target = BeginPaint(combo, &paint);
    if (target == NULL)
        return FALSE;
    GetClientRect(combo, &client);
    dc = RmgPluginThemeBufferBegin(target, client.right - client.left,
        client.bottom - client.top, &buffer);
    if (dc == NULL)
        dc = target;
    RmgPluginThemePaintDarkCombo(combo, dc, &client);
    if (dc != target)
        RmgPluginThemeBufferEnd(&buffer, target, 0, 0);
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

/* Dark tab strip, painted entirely by the theme bridge. The tabs keep the
   natural per-caption widths computed by the tab control itself. (The
   previous owner-drawn fixed-width tabs used one width for every tab, and
   Windows squeezed them to fit, which truncated translated captions such as
   "Горячие клавиши".) */
static void RmgPluginThemePaintDarkTab(HWND tab, HDC dc, const RECT *client)
{
    int count = (int)SendMessageW(tab, TCM_GETITEMCOUNT, 0, 0);
    int selected = (int)SendMessageW(tab, TCM_GETCURSEL, 0, 0);
    int pageTop = -1;
    int i;
    RECT itemRect;
    int savedDC = SaveDC(dc);

    FillRect(dc, client, RmgPluginThemeDarkBrush());

    for (i = 0; i < count; ++i)
    {
        if (i != selected && SendMessageW(tab, TCM_GETITEMRECT, (WPARAM)i, (LPARAM)&itemRect) &&
            itemRect.bottom > pageTop)
            pageTop = itemRect.bottom;
    }
    if (pageTop < 0 && selected >= 0 &&
        SendMessageW(tab, TCM_GETITEMRECT, (WPARAM)selected, (LPARAM)&itemRect))
        pageTop = itemRect.bottom - 2;
    if (pageTop < 0)
        pageTop = client->top;

    /* Page border. */
    RmgPluginThemeFillColor(dc, client->left, pageTop, client->right, pageTop + 1, RMG_GRAY(88));
    RmgPluginThemeFillColor(dc, client->left, pageTop, client->left + 1, client->bottom, RMG_GRAY(88));
    RmgPluginThemeFillColor(dc, client->right - 1, pageTop, client->right, client->bottom, RMG_GRAY(88));
    RmgPluginThemeFillColor(dc, client->left, client->bottom - 1, client->right, client->bottom, RMG_GRAY(88));

    RmgPluginThemeSelectControlFont(tab, dc);
    SetBkMode(dc, TRANSPARENT);

    for (i = 0; i <= count; ++i)
    {
        int index = i;
        BOOL isSelected;
        wchar_t caption[128] = { 0 };
        TCITEMW item;
        RECT r;
        COLORREF outline, fill;

        /* Draw the selected tab last so it overlaps the page border. */
        if (i == count)
            index = selected;
        else if (i == selected)
            continue;
        if (index < 0 || index >= count)
            continue;
        if (!SendMessageW(tab, TCM_GETITEMRECT, (WPARAM)index, (LPARAM)&r))
            continue;

        isSelected = (index == selected);
        outline = isSelected ? RMG_GRAY(120) : RMG_GRAY(88);
        fill = isSelected ? RMG_GRAY(53) : RMG_GRAY(62);
        if (!isSelected)
            r.bottom = pageTop;
        else if (r.bottom > pageTop + 1)
            r.bottom = pageTop + 1;
        if (r.right - r.left < 4 || r.bottom - r.top < 4)
            continue;

        RmgPluginThemeFillColor(dc, r.left + 1, r.top + 1, r.right - 1, r.bottom, fill);
        RmgPluginThemeFillColor(dc, r.left + 1, r.top, r.right - 1, r.top + 1, outline);
        RmgPluginThemeFillColor(dc, r.left, r.top + 1, r.left + 1, r.bottom, outline);
        RmgPluginThemeFillColor(dc, r.right - 1, r.top + 1, r.right, r.bottom, outline);
        SetPixelV(dc, r.left, r.top, RmgPluginThemeBlend(outline, RGB(53, 53, 53), 40));
        SetPixelV(dc, r.right - 1, r.top, RmgPluginThemeBlend(outline, RGB(53, 53, 53), 40));
        if (isSelected && r.bottom > pageTop)
            RmgPluginThemeFillColor(dc, r.left + 1, pageTop, r.right - 1, pageTop + 1, fill);

        ZeroMemory(&item, sizeof(item));
        item.mask = TCIF_TEXT;
        item.pszText = caption;
        item.cchTextMax = (int)(sizeof(caption) / sizeof(caption[0]));
        SendMessageW(tab, TCM_GETITEMW, (WPARAM)index, (LPARAM)&item);

        InflateRect(&r, -4, -1);
        SetTextColor(dc, isSelected ? RMG_GRAY(255) : RMG_GRAY(195));
        DrawTextW(dc, caption, -1, &r,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
}

static BOOL RmgPluginThemeDrawDarkTabControl(HWND tab)
{
    PAINTSTRUCT paint;
    RECT client;
    RmgPluginThemeBuffer buffer;
    HDC target = BeginPaint(tab, &paint);
    HDC dc;

    if (target == NULL)
        return FALSE;
    GetClientRect(tab, &client);
    dc = RmgPluginThemeBufferBegin(target, client.right - client.left,
        client.bottom - client.top, &buffer);
    if (dc == NULL)
        dc = target;
    RmgPluginThemePaintDarkTab(tab, dc, &client);
    if (dc != target)
        RmgPluginThemeBufferEnd(&buffer, target, 0, 0);
    EndPaint(tab, &paint);
    return TRUE;
}

/* Dark list-view / tree header. The default header kept a white face with
   pale text (the "invisible text" in N-Rage's Modifiers list), because the
   visual style could not be switched to a dark variant on Windows 7. The
   header is therefore painted completely here. */
static void RmgPluginThemePaintDarkHeader(HWND header, HDC dc, const RECT *client)
{
    int count = (int)SendMessageW(header, HDM_GETITEMCOUNT, 0, 0);
    int i;
    int savedDC = SaveDC(dc);

    RmgPluginThemeFillColor(dc, client->left, client->top, client->right, client->bottom, RMG_GRAY(58));
    RmgPluginThemeFillColor(dc, client->left, client->bottom - 1, client->right, client->bottom, RMG_GRAY(38));
    RmgPluginThemeSelectControlFont(header, dc);
    SetBkMode(dc, TRANSPARENT);

    for (i = 0; i < count; ++i)
    {
        RECT r;
        RECT textRect;
        wchar_t text[256] = { 0 };
        HDITEMW item;
        UINT align = DT_LEFT;

        if (!SendMessageW(header, HDM_GETITEMRECT, (WPARAM)i, (LPARAM)&r))
            continue;

        RmgPluginThemeFillColor(dc, r.left, r.top, r.right, r.bottom - 1, RMG_GRAY(66));
        RmgPluginThemeFillColor(dc, r.left, r.top, r.right, r.top + 1, RMG_GRAY(82));
        RmgPluginThemeFillColor(dc, r.right - 1, r.top + 3, r.right, r.bottom - 4, RMG_GRAY(38));
        RmgPluginThemeFillColor(dc, r.right - 2, r.top + 3, r.right - 1, r.bottom - 4, RMG_GRAY(88));

        ZeroMemory(&item, sizeof(item));
        item.mask = HDI_TEXT | HDI_FORMAT;
        item.pszText = text;
        item.cchTextMax = (int)(sizeof(text) / sizeof(text[0]));
        SendMessageW(header, HDM_GETITEMW, (WPARAM)i, (LPARAM)&item);
        if ((item.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT)
            align = DT_RIGHT;
        else if ((item.fmt & HDF_JUSTIFYMASK) == HDF_CENTER)
            align = DT_CENTER;

        textRect = r;
        textRect.left += 6;
        textRect.right -= 6;
        SetTextColor(dc, RMG_GRAY(235));
        DrawTextW(dc, text, -1, &textRect,
            align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
}

static BOOL RmgPluginThemeDrawDarkHeaderControl(HWND header)
{
    PAINTSTRUCT paint;
    RECT client;
    RmgPluginThemeBuffer buffer;
    HDC target = BeginPaint(header, &paint);
    HDC dc;

    if (target == NULL)
        return FALSE;
    GetClientRect(header, &client);
    dc = RmgPluginThemeBufferBegin(target, client.right - client.left,
        client.bottom - client.top, &buffer);
    if (dc == NULL)
        dc = target;
    RmgPluginThemePaintDarkHeader(header, dc, &client);
    if (dc != target)
        RmgPluginThemeBufferEnd(&buffer, target, 0, 0);
    EndPaint(header, &paint);
    return TRUE;
}

/* Replaces the bright 3D sunken edge of edit boxes and lists with a flat
   dark frame. Only the outer 1-2 pixels are repainted, so scroll bars and
   the client area are untouched. */
static void RmgPluginThemePaintDarkFrame(HWND hwnd)
{
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    int edge = 0;
    RECT rect;
    HDC dc;
    int w, h;

    if ((exStyle & WS_EX_CLIENTEDGE) != 0)
        edge = 2;
    else if ((style & WS_BORDER) != 0 || (exStyle & WS_EX_STATICEDGE) != 0)
        edge = 1;
    if (edge == 0 || !GetWindowRect(hwnd, &rect))
        return;

    w = rect.right - rect.left;
    h = rect.bottom - rect.top;
    if (w < 6 || h < 6)
        return;

    dc = GetWindowDC(hwnd);
    if (dc == NULL)
        return;
    RmgPluginThemeFillColor(dc, 0, 0, w, 1, RMG_GRAY(88));
    RmgPluginThemeFillColor(dc, 0, h - 1, w, h, RMG_GRAY(88));
    RmgPluginThemeFillColor(dc, 0, 1, 1, h - 1, RMG_GRAY(88));
    RmgPluginThemeFillColor(dc, w - 1, 1, w, h - 1, RMG_GRAY(88));
    if (edge == 2)
    {
        RmgPluginThemeFillColor(dc, 1, 1, w - 1, 2, RMG_GRAY(25));
        RmgPluginThemeFillColor(dc, 1, h - 2, w - 1, h - 1, RMG_GRAY(25));
        RmgPluginThemeFillColor(dc, 1, 2, 2, h - 2, RMG_GRAY(25));
        RmgPluginThemeFillColor(dc, w - 2, 2, w - 1, h - 2, RMG_GRAY(25));
    }
    ReleaseDC(hwnd, dc);
}

static BOOL RmgPluginThemeIsControlClass(HWND hwnd, const wchar_t *name)
{
    wchar_t className[64] = { 0 };
    return GetClassNameW(hwnd, className,
        (int)(sizeof(className) / sizeof(className[0]))) > 0 &&
        lstrcmpiW(className, name) == 0;
}

static LRESULT CALLBACK RmgPluginThemeSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR referenceData)
{
    BOOL dark = RmgPluginThemeIsDark();
    (void)referenceData;

    if (message == WM_SHOWWINDOW && wParam != 0 && dark)
    {
        HWND rootWindow = GetAncestor(hwnd, GA_ROOT);
        if (rootWindow == hwnd &&
            (GetWindowLongPtrW(rootWindow, GWL_STYLE) & WS_CAPTION) != 0)
            RmgPluginThemeApplyWindowFrame(rootWindow, TRUE);
    }

    if (message == WM_MEASUREITEM && lParam != 0 && dark)
    {
        if (RmgPluginThemeMeasureComboItem(hwnd, (MEASUREITEMSTRUCT *)lParam))
            return TRUE;
    }

    if (message == WM_ERASEBKGND &&
        GetPropW(hwnd, RMG_PLUGIN_DARK_PANEL_PROPERTY) != NULL)
    {
        RECT clientRect;
        HBRUSH panelBrush = dark ? RmgPluginThemeDarkBrush() : GetSysColorBrush(COLOR_BTNFACE);
        if (GetClientRect(hwnd, &clientRect))
        {
            FillRect((HDC)wParam, &clientRect, panelBrush);
            return 1;
        }
    }

    if (dark)
    {
        if (message == WM_ERASEBKGND &&
            GetPropW(hwnd, RMG_PLUGIN_DARK_GROUP_PROPERTY) != NULL)
            return 1;

        if (message == WM_ERASEBKGND &&
            (GetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY) != NULL ||
             GetPropW(hwnd, RMG_PLUGIN_DARK_HEADER_PROPERTY) != NULL))
        {
            RECT clientRect;
            if (GetClientRect(hwnd, &clientRect))
            {
                FillRect((HDC)wParam, &clientRect, RmgPluginThemeDarkBrush());
                return 1;
            }
        }

        if (message == WM_PAINT)
        {
            if (GetPropW(hwnd, RMG_PLUGIN_DARK_COMBO_PROPERTY) != NULL)
            {
                RmgPluginThemeDrawDarkComboControl(hwnd);
                return 0;
            }
            if (GetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY) != NULL)
            {
                RmgPluginThemeDrawDarkCheckbox(hwnd);
                return 0;
            }
            if (GetPropW(hwnd, RMG_PLUGIN_DARK_TAB_PROPERTY) != NULL)
            {
                RmgPluginThemeDrawDarkTabControl(hwnd);
                return 0;
            }
            if (GetPropW(hwnd, RMG_PLUGIN_DARK_HEADER_PROPERTY) != NULL)
            {
                RmgPluginThemeDrawDarkHeaderControl(hwnd);
                return 0;
            }
        }

        if (message == WM_NCPAINT &&
            GetPropW(hwnd, RMG_PLUGIN_DARK_FRAME_PROPERTY) != NULL)
        {
            LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            RmgPluginThemePaintDarkFrame(hwnd);
            return result;
        }
    }

    if (message == WM_DRAWITEM && lParam != 0)
    {
        const DRAWITEMSTRUCT *drawItem = (const DRAWITEMSTRUCT *)lParam;
        if (RmgPluginThemeDrawButton(drawItem) ||
            RmgPluginThemeDrawGroupBox(drawItem) ||
            RmgPluginThemeDrawCombo(drawItem))
            return TRUE;
    }

    /* Hover tracking for owner-drawn push buttons (both themes) and dark
       check boxes / combo boxes. */
    if (GetPropW(hwnd, RMG_PLUGIN_DARK_BUTTON_PROPERTY) != NULL ||
        GetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY) != NULL ||
        GetPropW(hwnd, RMG_PLUGIN_DARK_COMBO_PROPERTY) != NULL)
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

    if (message == WM_ERASEBKGND && dark)
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

    /* Light theme: make STATIC text labels and check/radio/group captions
       transparent so they do not paint a gray box over a themed tab page.
       This must apply to real STATIC and BUTTON controls only. The previous
       implementation tested only the low style bits, and those collide with
       trackbar styles (TBS_AUTOTICKS == SS_CENTER, TBS_HORZ == SS_LEFT), so
       every horizontal trackbar received a NULL background brush and was
       painted on an uninitialised (black) buffer. */
    if (!dark && (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN))
    {
        HWND control = (HWND)lParam;
        BOOL transparentControl = FALSE;

        if (control != NULL && message == WM_CTLCOLORSTATIC &&
            RmgPluginThemeIsControlClass(control, L"Static"))
        {
            LONG_PTR staticType = GetWindowLongPtrW(control, GWL_STYLE) & SS_TYPEMASK;
            transparentControl = staticType == SS_LEFT || staticType == SS_CENTER ||
                staticType == SS_RIGHT || staticType == SS_SIMPLE ||
                staticType == SS_LEFTNOWORDWRAP;
        }
        else if (control != NULL && message == WM_CTLCOLORBTN &&
            RmgPluginThemeIsControlClass(control, L"Button"))
        {
            LONG_PTR buttonType = GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK;
            transparentControl = buttonType == BS_CHECKBOX || buttonType == BS_AUTOCHECKBOX ||
                buttonType == BS_3STATE || buttonType == BS_AUTO3STATE ||
                buttonType == BS_RADIOBUTTON || buttonType == BS_AUTORADIOBUTTON ||
                buttonType == BS_GROUPBOX;
        }

        if (transparentControl)
        {
            HDC dc = (HDC)wParam;
            COLORREF foreground = !IsWindowEnabled(control)
                ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_WINDOWTEXT);
            SetTextColor(dc, foreground);
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
    }

    if (dark &&
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
        RemovePropW(hwnd, RMG_PLUGIN_DARK_TAB_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_HEADER_PROPERTY);
        RemovePropW(hwnd, RMG_PLUGIN_DARK_FRAME_PROPERTY);
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
            message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CAPTURECHANGED)
        {
            if (GetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY) != NULL)
                InvalidateRect(hwnd, NULL, TRUE);
        }

        if (GetPropW(hwnd, RMG_PLUGIN_DARK_COMBO_PROPERTY) != NULL &&
            (message == CB_SETCURSEL || message == CB_RESETCONTENT ||
             message == CB_SHOWDROPDOWN || message == WM_ENABLE ||
             message == WM_SETFOCUS || message == WM_KILLFOCUS ||
             message == WM_SETTEXT || message == WM_LBUTTONUP ||
             message == WM_KEYUP || message == WM_MOUSEWHEEL ||
             message == WM_CAPTURECHANGED))
            InvalidateRect(hwnd, NULL, FALSE);

        /* Closing or changing a dark drop-down list: refresh the closed face. */
        if (message == WM_COMMAND && lParam != 0 && dark)
        {
            HWND source = (HWND)lParam;
            if (IsWindow(source) && GetPropW(source, RMG_PLUGIN_DARK_COMBO_PROPERTY) != NULL)
                InvalidateRect(source, NULL, FALSE);
        }
        return result;
    }
}

static void RmgPluginThemeAttach(HWND hwnd);

static void RmgPluginThemeAttachListViewHeader(HWND listView)
{
    HWND header = (HWND)SendMessageW(listView, LVM_GETHEADER, 0, 0);
    if (header != NULL && IsWindow(header))
        RmgPluginThemeAttach(header);
}

static void RmgPluginThemeAttach(HWND hwnd)
{
    BOOL dark;
    wchar_t className[64] = { 0 };
    HWND rootWindow;

    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    dark = RmgPluginThemeIsDark();

    /* Install the subclass only after the HWND has completed creation. Calls
       made from WM_INITDIALOG / WM_CREATE are safe; callers should not attach
       from HCBT_CREATEWND, which runs before WM_NCCREATE. */
    SetWindowSubclass(hwnd, RmgPluginThemeSubclassProc, (UINT_PTR)0x524D47u, 0);

    if (GetClassNameW(hwnd, className,
        (int)(sizeof(className) / sizeof(className[0]))) > 0)
    {
        if (wcscmp(className, L"Button") == 0)
        {
            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            LONG_PTR buttonType = style & BS_TYPEMASK;

            if (buttonType == BS_PUSHBUTTON || buttonType == BS_DEFPUSHBUTTON)
            {
                /* Push buttons use the unified Fusion look in BOTH themes. */
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
            else if (dark && buttonType == BS_GROUPBOX)
            {
                if (GetPropW(hwnd, RMG_PLUGIN_DARK_GROUP_PROPERTY) == NULL)
                {
                    SetPropW(hwnd, RMG_PLUGIN_DARK_GROUP_PROPERTY, (HANDLE)(ULONG_PTR)1);
                    SetWindowLongPtrW(hwnd, GWL_STYLE,
                        ((style & ~BS_TYPEMASK) | BS_OWNERDRAW) | WS_CLIPSIBLINGS);
                }
                InvalidateRect(hwnd, NULL, TRUE);
            }
            else if (dark &&
                (buttonType == BS_CHECKBOX || buttonType == BS_AUTOCHECKBOX ||
                 buttonType == BS_3STATE || buttonType == BS_AUTO3STATE ||
                 buttonType == BS_RADIOBUTTON || buttonType == BS_AUTORADIOBUTTON))
            {
                SetPropW(hwnd, RMG_PLUGIN_DARK_CHECK_PROPERTY, (HANDLE)(ULONG_PTR)1);
                SetWindowTheme(hwnd, L"", L"");
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        else if (dark && wcscmp(className, L"SysTabControl32") == 0)
        {
            SetWindowTheme(hwnd, L"", L"");
            SetPropW(hwnd, RMG_PLUGIN_DARK_TAB_PROPERTY, (HANDLE)(ULONG_PTR)1);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        else if (dark && wcscmp(className, L"SysHeader32") == 0)
        {
            SetWindowTheme(hwnd, L"", L"");
            SetPropW(hwnd, RMG_PLUGIN_DARK_HEADER_PROPERTY, (HANDLE)(ULONG_PTR)1);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        else if (dark && wcscmp(className, L"ComboBox") == 0)
        {
            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
            LONG_PTR type = style & 0x0003;
            if (type == CBS_DROPDOWNLIST &&
                (style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) == 0)
            {
                LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                SetPropW(hwnd, RMG_PLUGIN_DARK_COMBO_PROPERTY,
                    (HANDLE)(ULONG_PTR)style);
                style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
                SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~WS_BORDER);
                /* The Fusion face draws its own outline; the native sunken
                   edge would add a second, bright frame. */
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                    exStyle & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
                SetWindowTheme(hwnd, L"", L"");
                SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                    SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            else
            {
                /* Editable combos keep the native control with a dark palette. */
                HRESULT themeResult = SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
                if (FAILED(themeResult))
                    SetWindowTheme(hwnd, L"Explorer", NULL);
            }
        }
        else if (dark && wcscmp(className, L"SysListView32") == 0)
        {
            HRESULT themeResult;
            SendMessageW(hwnd, LVM_SETBKCOLOR, 0, (LPARAM)RGB(25, 25, 25));
            SendMessageW(hwnd, LVM_SETTEXTCOLOR, 0, (LPARAM)RGB(239, 242, 246));
            SendMessageW(hwnd, LVM_SETTEXTBKCOLOR, 0, (LPARAM)RGB(25, 25, 25));
            themeResult = SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
            if (FAILED(themeResult))
                SetWindowTheme(hwnd, L"Explorer", NULL);
            SetPropW(hwnd, RMG_PLUGIN_DARK_FRAME_PROPERTY, (HANDLE)(ULONG_PTR)1);
            RmgPluginThemeAttachListViewHeader(hwnd);
        }
        else if (dark && (wcscmp(className, L"Edit") == 0 ||
                          wcscmp(className, L"ListBox") == 0 ||
                          wcscmp(className, L"SysTreeView32") == 0))
        {
            HRESULT themeResult = SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
            if (FAILED(themeResult))
                SetWindowTheme(hwnd, L"Explorer", NULL);
            SetPropW(hwnd, RMG_PLUGIN_DARK_FRAME_PROPERTY, (HANDLE)(ULONG_PTR)1);
        }
        else if (dark && wcscmp(className, L"msctls_trackbar32") == 0)
        {
            /* The trackbar keeps its native channel, ticks and thumb. In the
               light theme it is left completely untouched: the black bars
               seen there came from the WM_CTLCOLORSTATIC handler, not from
               the visual style. */
            SetWindowTheme(hwnd, L"Explorer", NULL);
            InvalidateRect(hwnd, NULL, TRUE);
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
