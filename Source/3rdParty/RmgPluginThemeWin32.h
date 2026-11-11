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

static LRESULT CALLBACK RmgPluginThemeSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR referenceData)
{
    (void)referenceData;

    if (message == WM_ERASEBKGND)
    {
        wchar_t className[64] = { 0 };
        if (GetClassNameW(hwnd, className, (int)(sizeof(className) / sizeof(className[0]))) > 0 &&
            wcscmp(className, L"#32770") == 0)
        {
            RECT clientRect;
            if (GetClientRect(hwnd, &clientRect))
            {
                FillRect((HDC)wParam, &clientRect, RmgPluginThemeDarkBrush());
                return 1;
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
        RemoveWindowSubclass(hwnd, RmgPluginThemeSubclassProc, subclassId);
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

static void RmgPluginThemeAttach(HWND hwnd)
{
    BOOL dark;
    HRESULT themeResult;

    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    dark = RmgPluginThemeIsDark();
    if (dark)
        SetWindowSubclass(hwnd, RmgPluginThemeSubclassProc, (UINT_PTR)0x524D47u, 0);

    /* Use the Explorer visual style in both modes instead of falling back to
       the old unthemed Win32 controls. On Windows 10/11 the dark variant is
       used when supported; Windows 7 and other older versions fall back to
       the regular Explorer theme while our CTLCOLOR handlers darken surfaces. */
    themeResult = SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
    if (FAILED(themeResult) && dark)
        SetWindowTheme(hwnd, L"Explorer", NULL);

    if (dark)
        InvalidateRect(hwnd, NULL, TRUE);
}

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif /* RMG_PLUGIN_THEME_WIN32_H */
