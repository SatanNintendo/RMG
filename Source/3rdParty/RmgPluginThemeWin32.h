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

static HBRUSH RmgPluginThemeDarkButtonBrush(BOOL pressed, BOOL disabled)
{
    static HBRUSH normalBrush = NULL;
    static HBRUSH pressedBrush = NULL;
    static HBRUSH disabledBrush = NULL;

    if (normalBrush == NULL)
        normalBrush = CreateSolidBrush(RGB(64, 64, 64));
    if (pressedBrush == NULL)
        pressedBrush = CreateSolidBrush(RGB(43, 43, 43));
    if (disabledBrush == NULL)
        disabledBrush = CreateSolidBrush(RGB(47, 47, 47));

    if (disabled)
        return disabledBrush;
    return pressed ? pressedBrush : normalBrush;
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
    UINT textFlags;
    BOOL disabled;
    BOOL pressed;
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

    FillRect(dc, &borderRect, RmgPluginThemeDarkBorderBrush());

    borderWidth = ((originalStyle & BS_TYPEMASK) == BS_DEFPUSHBUTTON) ? 2 : 1;
    InflateRect(&textRect, -borderWidth, -borderWidth);
    FillRect(dc, &textRect, RmgPluginThemeDarkButtonBrush(pressed, disabled));

    if (GetWindowTextW(button, text, (int)(sizeof(text) / sizeof(text[0]))) > 0)
    {
        InflateRect(&textRect, -5, -2);
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

static LRESULT CALLBACK RmgPluginThemeSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR referenceData)
{
    (void)referenceData;

    if (message == WM_DRAWITEM)
    {
        if (RmgPluginThemeDrawDarkButton((const DRAWITEMSTRUCT *)lParam))
            return TRUE;
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
                BOOL tabControl = (wcscmp(controlClass, L"SysTabControl32") == 0);
                if ((headerControl || tabControl) &&
                    customDraw->dwDrawStage == CDDS_PREPAINT)
                    return CDRF_NOTIFYITEMDRAW;
                if ((headerControl || tabControl) &&
                    customDraw->dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    HBRUSH itemBrush = RmgPluginThemeDarkBrush();
                    if (tabControl && (customDraw->uItemState & CDIS_SELECTED) != 0)
                        itemBrush = RmgPluginThemeDarkButtonBrush(FALSE, FALSE);
                    FillRect(customDraw->hdc, &customDraw->rc, itemBrush);
                    SetTextColor(customDraw->hdc, RGB(245, 245, 245));
                    SetBkMode(customDraw->hdc, TRANSPARENT);
                    return CDRF_NEWFONT;
                }
            }
        }
    }

    if (message == WM_ERASEBKGND)
    {
        wchar_t className[64] = { 0 };
        if (GetClassNameW(hwnd, className, (int)(sizeof(className) / sizeof(className[0]))) > 0)
        {
            BOOL dialogWindow = (wcscmp(className, L"#32770") == 0);
            BOOL etchedPanel = (wcscmp(className, L"STATIC") == 0 &&
                (GetWindowLongPtrW(hwnd, GWL_STYLE) & SS_TYPEMASK) == SS_ETCHEDFRAME);
            BOOL trackbar = (wcscmp(className, L"msctls_trackbar32") == 0);
            if (dialogWindow || etchedPanel || trackbar)
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
        RemoveWindowSubclass(hwnd, RmgPluginThemeSubclassProc, subclassId);
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
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
        }

        if (wcscmp(className, L"SysListView32") == 0)
        {
            SendMessageW(hwnd, LVM_SETBKCOLOR, 0, (LPARAM)RGB(25, 25, 25));
            SendMessageW(hwnd, LVM_SETTEXTCOLOR, 0, (LPARAM)RGB(245, 245, 245));
            SendMessageW(hwnd, LVM_SETTEXTBKCOLOR, 0, (LPARAM)RGB(25, 25, 25));
        }
        else if (wcscmp(className, L"SysTabControl32") == 0)
        {
            SendMessageW(hwnd, TCM_SETBKCOLOR, 0, (LPARAM)RGB(53, 53, 53));
        }
    }

    /* In dark mode do not fall back to the Explorer theme: on older Windows
       versions it paints native controls with light faces. Disable visual
       theming instead and let the CTLCOLOR handlers / dark owner-drawn buttons
       provide consistent colors. Keep Explorer styling for light mode. */
    SetWindowTheme(hwnd, dark ? L"" : L"Explorer", dark ? L"" : NULL);

    if (dark)
        InvalidateRect(hwnd, NULL, TRUE);
}

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif /* RMG_PLUGIN_THEME_WIN32_H */
