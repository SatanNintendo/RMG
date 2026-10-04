#define M64P_PLUGIN_PROTOTYPES 1

#include <stdbool.h>
#include <stdint.h>

#include "api/m64p_types.h"
#include "api/m64p_config.h"
#include "../sr_config.h"

#ifndef _WIN32

m64p_error softRdpConfigGui(
    void *parent,
    m64p_handle section,
    ptr_ConfigOpenSection open_section,
    ptr_ConfigGetParamInt get_int,
    ptr_ConfigGetParamBool get_bool,
    ptr_ConfigSetParameter set_parameter,
    ptr_ConfigSaveSection save_section)
{
    (void)parent;
    (void)section;
    (void)open_section;
    (void)get_int;
    (void)get_bool;
    (void)set_parameter;
    (void)save_section;
    return M64ERR_UNSUPPORTED;
}

#else

#include <windows.h>
#include <commctrl.h>

#define SOFTRDP_CFG_CLASS L"SoftRDPConfigWindow"
#define SOFTRDP_CFG_TITLE L"SoftRDP Settings"

#define IDC_SCALE              1001
#define IDC_WORKERS            1002
#define IDC_INTEGER_SCALE      1003
#define IDC_BILINEAR           1004
#define IDC_DISABLE_DITHER     1005
#define IDC_DISABLE_DIVOT      1006
#define IDC_DISABLE_GAMMA      1007
#define IDC_DISABLE_AA         1008
#define IDC_DEFAULTS           1009
#define IDC_OK                 1010
#define IDC_CANCEL             1011
#define IDC_WINDOW_SIZE        1012
#define IDC_VSYNC              1013

#define IDC_LABEL_BASE         1100

struct softrdp_config_gui_state
{
    m64p_handle section;
    ptr_ConfigOpenSection open_section;
    ptr_ConfigGetParamInt get_int;
    ptr_ConfigGetParamBool get_bool;
    ptr_ConfigSetParameter set_parameter;
    ptr_ConfigSaveSection save_section;

    HWND hwnd;
    HWND scale;
    HWND workers;
    HWND window_size;
    HWND integer_scale;
    HWND bilinear;
    HWND disable_dither;
    HWND disable_divot;
    HWND disable_gamma;
    HWND disable_aa;
    HWND vsync;
    HWND defaults;
    HWND ok;
    HWND cancel;

    HWND owner;
    bool result;
    bool done;
};

static HFONT cfg_font(void)
{
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}
static void set_checkbox(HWND hwnd, bool checked)
{
    SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

static bool get_checkbox(HWND hwnd)
{
    return SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void add_combo_item(HWND combo, const wchar_t *text)
{
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
}

static int combo_scale_value(HWND combo)
{
    int index = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index < 0)
        return 1;
    return index + 1;
}

static int combo_workers_value(HWND combo)
{
    int index = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index <= 0)
        return 0;
    return index;
}

static void combo_window_size_value(HWND combo, int *width, int *height)
{
    static const int sizes[][2] = {
        {640, 480},
        {720, 540},
        {800, 600},
        {960, 720},
        {1024, 768},
        {1280, 960},
        {1440, 1080},
        {1600, 1200}
    };
    const int count = (int)(sizeof(sizes) / sizeof(sizes[0]));
    int index = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index < 0 || index >= count)
        index = 3;
    *width = sizes[index][0];
    *height = sizes[index][1];
}

static int find_window_size_index(int width, int height)
{
    static const int sizes[][2] = {
        {640, 480},
        {720, 540},
        {800, 600},
        {960, 720},
        {1024, 768},
        {1280, 960},
        {1440, 1080},
        {1600, 1200}
    };
    const int count = (int)(sizeof(sizes) / sizeof(sizes[0]));
    for (int i = 0; i < count; i++) {
        if (sizes[i][0] == width && sizes[i][1] == height)
            return i;
    }
    return 3;
}

static void set_defaults(struct softrdp_config_gui_state *state)
{
    SendMessageW(state->scale, CB_SETCURSEL, SR_CONFIG_DEFAULT_SCALE - 1, 0);
    SendMessageW(state->workers, CB_SETCURSEL, 0, 0);
    SendMessageW(state->window_size, CB_SETCURSEL,
                 find_window_size_index(SR_CONFIG_DEFAULT_WINDOW_WIDTH,
                                        SR_CONFIG_DEFAULT_WINDOW_HEIGHT), 0);

    set_checkbox(state->integer_scale, SR_CONFIG_DEFAULT_INTEGER_PIXEL_SCALE);
    set_checkbox(state->bilinear, SR_CONFIG_DEFAULT_BILINEAR_FILTER);
    set_checkbox(state->disable_dither, SR_CONFIG_DEFAULT_DISABLE_VI_DITHER_FILTER);
    set_checkbox(state->disable_divot, SR_CONFIG_DEFAULT_DISABLE_VI_DIVOT_FILTER);
    set_checkbox(state->disable_gamma, SR_CONFIG_DEFAULT_DISABLE_VI_GAMMA_DITHER);
    set_checkbox(state->disable_aa, SR_CONFIG_DEFAULT_DISABLE_VI_AA);
    set_checkbox(state->vsync, SR_CONFIG_DEFAULT_VSYNC);
}

static bool save_settings(struct softrdp_config_gui_state *state)
{
    if (state->set_parameter == NULL || state->save_section == NULL)
        return false;

    int scale = combo_scale_value(state->scale);
    int workers = combo_workers_value(state->workers);
    int window_width = SR_CONFIG_DEFAULT_WINDOW_WIDTH;
    int window_height = SR_CONFIG_DEFAULT_WINDOW_HEIGHT;
    combo_window_size_value(state->window_size, &window_width, &window_height);
    int integer_scale = get_checkbox(state->integer_scale) ? 1 : 0;
    int bilinear = get_checkbox(state->bilinear) ? 1 : 0;
    int disable_dither = get_checkbox(state->disable_dither) ? 1 : 0;
    int disable_divot = get_checkbox(state->disable_divot) ? 1 : 0;
    int disable_gamma = get_checkbox(state->disable_gamma) ? 1 : 0;
    int disable_aa = get_checkbox(state->disable_aa) ? 1 : 0;
    int vsync = get_checkbox(state->vsync) ? 1 : 0;

    if (state->set_parameter(state->section, SR_CONFIG_KEY_SCALE, M64TYPE_INT, &scale) != M64ERR_SUCCESS)
        return false;
    if (state->set_parameter(state->section, SR_CONFIG_KEY_WORKERS, M64TYPE_INT, &workers) != M64ERR_SUCCESS)
        return false;
    if (state->set_parameter(state->section, SR_CONFIG_KEY_VSYNC, M64TYPE_BOOL, &vsync) != M64ERR_SUCCESS)
        return false;
    if (state->set_parameter(state->section, SR_CONFIG_KEY_INTEGER_PIXEL_SCALE, M64TYPE_BOOL, &integer_scale) != M64ERR_SUCCESS)
        return false;
    if (state->set_parameter(state->section, SR_CONFIG_KEY_BILINEAR_FILTER, M64TYPE_BOOL, &bilinear) != M64ERR_SUCCESS)
        return false;
    if (state->set_parameter(state->section, SR_CONFIG_KEY_DISABLE_VI_DITHER_FILTER, M64TYPE_BOOL, &disable_dither) != M64ERR_SUCCESS)
        return false;
    if (state->set_parameter(state->section, SR_CONFIG_KEY_DISABLE_VI_DIVOT_FILTER, M64TYPE_BOOL, &disable_divot) != M64ERR_SUCCESS)
        return false;
    if (state->set_parameter(state->section, SR_CONFIG_KEY_DISABLE_VI_GAMMA_DITHER, M64TYPE_BOOL, &disable_gamma) != M64ERR_SUCCESS)
        return false;
    if (state->set_parameter(state->section, SR_CONFIG_KEY_DISABLE_VI_AA, M64TYPE_BOOL, &disable_aa) != M64ERR_SUCCESS)
        return false;

    m64p_handle general_section = NULL;
    if (state->open_section == NULL ||
        state->open_section("Video-General", &general_section) != M64ERR_SUCCESS ||
        general_section == NULL)
        return false;

    if (state->set_parameter(general_section, "ScreenWidth", M64TYPE_INT, &window_width) != M64ERR_SUCCESS)
        return false;
    if (state->set_parameter(general_section, "ScreenHeight", M64TYPE_INT, &window_height) != M64ERR_SUCCESS)
        return false;

    if (state->save_section(SR_CONFIG_M64P_SECTION) != M64ERR_SUCCESS)
        return false;
    return state->save_section("Video-General") == M64ERR_SUCCESS;
}

static void load_settings(struct softrdp_config_gui_state *state)
{
    int scale = (int)SR_CONFIG_DEFAULT_SCALE;
    int workers = (int)SR_CONFIG_DEFAULT_WORKERS;

    bool integer_scale = SR_CONFIG_DEFAULT_INTEGER_PIXEL_SCALE;
    bool bilinear = SR_CONFIG_DEFAULT_BILINEAR_FILTER;
    bool disable_dither = SR_CONFIG_DEFAULT_DISABLE_VI_DITHER_FILTER;
    bool disable_divot = SR_CONFIG_DEFAULT_DISABLE_VI_DIVOT_FILTER;
    bool disable_gamma = SR_CONFIG_DEFAULT_DISABLE_VI_GAMMA_DITHER;
    bool disable_aa = SR_CONFIG_DEFAULT_DISABLE_VI_AA;
    bool vsync = SR_CONFIG_DEFAULT_VSYNC;
    int window_width = SR_CONFIG_DEFAULT_WINDOW_WIDTH;
    int window_height = SR_CONFIG_DEFAULT_WINDOW_HEIGHT;

    if (state->get_int != NULL)
    {
        scale = (int)sr_config_clamp_scale(state->get_int(state->section, SR_CONFIG_KEY_SCALE));
        workers = (int)sr_config_clamp_workers(state->get_int(state->section, SR_CONFIG_KEY_WORKERS));
    }

    m64p_handle general_section = NULL;
    if (state->open_section != NULL &&
        state->open_section("Video-General", &general_section) == M64ERR_SUCCESS &&
        general_section != NULL && state->get_int != NULL)
    {
        window_width = state->get_int(general_section, "ScreenWidth");
        window_height = state->get_int(general_section, "ScreenHeight");
    }

    if (state->get_bool != NULL)
    {
        integer_scale = state->get_bool(state->section, SR_CONFIG_KEY_INTEGER_PIXEL_SCALE) != 0;
        bilinear = state->get_bool(state->section, SR_CONFIG_KEY_BILINEAR_FILTER) != 0;
        disable_dither = state->get_bool(state->section, SR_CONFIG_KEY_DISABLE_VI_DITHER_FILTER) != 0;
        disable_divot = state->get_bool(state->section, SR_CONFIG_KEY_DISABLE_VI_DIVOT_FILTER) != 0;
        disable_gamma = state->get_bool(state->section, SR_CONFIG_KEY_DISABLE_VI_GAMMA_DITHER) != 0;
        disable_aa = state->get_bool(state->section, SR_CONFIG_KEY_DISABLE_VI_AA) != 0;
        vsync = state->get_bool(state->section, SR_CONFIG_KEY_VSYNC) != 0;
    }

    if (window_width <= 0) window_width = SR_CONFIG_DEFAULT_WINDOW_WIDTH;
    if (window_height <= 0) window_height = SR_CONFIG_DEFAULT_WINDOW_HEIGHT;

    SendMessageW(state->scale, CB_SETCURSEL, scale - 1, 0);
    if (workers == 0)
        SendMessageW(state->workers, CB_SETCURSEL, 0, 0);
    else
        SendMessageW(state->workers, CB_SETCURSEL, workers, 0);

    SendMessageW(state->window_size, CB_SETCURSEL,
                 find_window_size_index(window_width, window_height), 0);

    set_checkbox(state->integer_scale, integer_scale);
    set_checkbox(state->bilinear, bilinear);
    set_checkbox(state->disable_dither, disable_dither);
    set_checkbox(state->disable_divot, disable_divot);
    set_checkbox(state->disable_gamma, disable_gamma);
    set_checkbox(state->disable_aa, disable_aa);
    set_checkbox(state->vsync, vsync);
}

static HWND create_control(
    const wchar_t *class_name,
    const wchar_t *text,
    DWORD style,
    DWORD ex_style,
    int x,
    int y,
    int width,
    int height,
    HWND parent,
    int id,
    HFONT font)
{
    HWND control = CreateWindowExW(
        ex_style,
        class_name,
        text,
        style,
        x,
        y,
        width,
        height,
        parent,
        (HMENU)(INT_PTR)id,
        GetModuleHandleW(NULL),
        NULL);

    if (control != NULL && font != NULL)
        SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);

    return control;
}

static void add_label(struct softrdp_config_gui_state *state, const wchar_t *text, int x, int y, int width)
{
    (void)state;
    create_control(WC_STATICW, text, WS_CHILD | WS_VISIBLE, 0,
                   x, y, width, 22, state->hwnd, IDC_LABEL_BASE + y, NULL);
}

static void create_controls(struct softrdp_config_gui_state *state)
{
    HFONT font = cfg_font();
    const int left = 18;
    const int label_width = 180;
    const int control_x = left + label_width;
    const int control_width = 235;

    add_label(state, L"Internal resolution", left, 18, label_width);
    state->scale = create_control(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                  WS_EX_CLIENTEDGE, control_x, 14, 140, 180,
                                  state->hwnd, IDC_SCALE, font);
    add_combo_item(state->scale, L"1x (native)");
    add_combo_item(state->scale, L"2x");

    add_label(state, L"Worker threads", left, 52, label_width);
    state->workers = create_control(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                    WS_EX_CLIENTEDGE, control_x, 48, 160, 240,
                                    state->hwnd, IDC_WORKERS, font);
    add_combo_item(state->workers, L"Auto");
    add_combo_item(state->workers, L"1");
    add_combo_item(state->workers, L"2");
    add_combo_item(state->workers, L"3");
    add_combo_item(state->workers, L"4");
    add_combo_item(state->workers, L"5");
    add_combo_item(state->workers, L"6");
    add_combo_item(state->workers, L"7");
    add_combo_item(state->workers, L"8");
    add_combo_item(state->workers, L"9");
    add_combo_item(state->workers, L"10");
    add_combo_item(state->workers, L"11");
    add_combo_item(state->workers, L"12");

    add_label(state, L"Windowed size", left, 90, label_width);
    state->window_size = create_control(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                        WS_EX_CLIENTEDGE, control_x, 86, 180, 190,
                                        state->hwnd, IDC_WINDOW_SIZE, font);
    add_combo_item(state->window_size, L"640 x 480");
    add_combo_item(state->window_size, L"720 x 540");
    add_combo_item(state->window_size, L"800 x 600");
    add_combo_item(state->window_size, L"960 x 720");
    add_combo_item(state->window_size, L"1024 x 768");
    add_combo_item(state->window_size, L"1280 x 960");
    add_combo_item(state->window_size, L"1440 x 1080");
    add_combo_item(state->window_size, L"1600 x 1200");

    add_label(state, L"Presentation", left, 128, label_width);
    state->integer_scale = create_control(WC_BUTTONW, L"Integer pixel scaling",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                           0, left, 150, control_width + label_width, 22,
                                           state->hwnd, IDC_INTEGER_SCALE, font);
    state->bilinear = create_control(WC_BUTTONW, L"Bilinear filtering",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                     0, left, 178, control_width + label_width, 22,
                                     state->hwnd, IDC_BILINEAR, font);
    state->vsync = create_control(WC_BUTTONW, L"Vertical synchronization (VSync)",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                  0, left, 206, control_width + label_width, 22,
                                  state->hwnd, IDC_VSYNC, font);

    add_label(state, L"VI filters", left, 242, label_width);
    state->disable_dither = create_control(WC_BUTTONW, L"Disable VI dither filter",
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                            0, left, 264, control_width + label_width, 22,
                                            state->hwnd, IDC_DISABLE_DITHER, font);
    state->disable_divot = create_control(WC_BUTTONW, L"Disable VI divot filter",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          0, left, 292, control_width + label_width, 22,
                                          state->hwnd, IDC_DISABLE_DIVOT, font);
    state->disable_gamma = create_control(WC_BUTTONW, L"Disable VI gamma dither",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                           0, left, 320, control_width + label_width, 22,
                                           state->hwnd, IDC_DISABLE_GAMMA, font);
    state->disable_aa = create_control(WC_BUTTONW, L"Disable VI anti-aliasing",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                       0, left, 348, control_width + label_width, 22,
                                       state->hwnd, IDC_DISABLE_AA, font);

    state->defaults = create_control(WC_BUTTONW, L"Defaults",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     0, 18, 396, 92, 30,
                                     state->hwnd, IDC_DEFAULTS, font);
    state->cancel = create_control(WC_BUTTONW, L"Cancel",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   0, 252, 396, 92, 30,
                                   state->hwnd, IDC_CANCEL, font);
    state->ok = create_control(WC_BUTTONW, L"OK",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                               0, 354, 396, 92, 30,
                               state->hwnd, IDC_OK, font);

    SendMessageW(state->scale, CB_SETCURSEL, 0, 0);
    SendMessageW(state->workers, CB_SETCURSEL, 0, 0);
    SendMessageW(state->window_size, CB_SETCURSEL, 3, 0);
    set_defaults(state);

    load_settings(state);
}

static void destroy_controls(struct softrdp_config_gui_state *state)
{
    (void)state;
}

static LRESULT CALLBACK config_wnd_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    struct softrdp_config_gui_state *state = (struct softrdp_config_gui_state *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (message)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lParam;
        state = (struct softrdp_config_gui_state *)create->lpCreateParams;
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        return TRUE;
    }

    case WM_CREATE:
        create_controls(state);
        SetFocus(state->scale);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_DEFAULTS:
            set_defaults(state);
            return 0;

        case IDC_OK:
            if (!save_settings(state))
            {
                MessageBoxW(hwnd,
                            L"Failed to save SoftRDP settings.",
                            SOFTRDP_CFG_TITLE,
                            MB_OK | MB_ICONERROR);
                return 0;
            }
            state->result = true;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;

        case IDC_CANCEL:
            state->result = false;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        state->result = false;
        state->done = true;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        destroy_controls(state);
        return 0;

    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

static ATOM register_config_class(void)
{
    static ATOM registered = 0;
    if (registered != 0)
        return registered;

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = config_wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = SOFTRDP_CFG_CLASS;

    registered = RegisterClassExW(&wc);
    if (registered == 0 && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
    {
        /* Another instance of the same DLL may have registered it already. */
        registered = 1;
    }
    return registered;
}

m64p_error softRdpConfigGui(
    void *parent,
    m64p_handle section,
    ptr_ConfigOpenSection open_section,
    ptr_ConfigGetParamInt get_int,
    ptr_ConfigGetParamBool get_bool,
    ptr_ConfigSetParameter set_parameter,
    ptr_ConfigSaveSection save_section)
{
    if (section == NULL || open_section == NULL || get_int == NULL || get_bool == NULL ||
        set_parameter == NULL || save_section == NULL)
        return M64ERR_NOT_INIT;

    struct softrdp_config_gui_state state;
    ZeroMemory(&state, sizeof(state));
    state.section = section;
    state.open_section = open_section;
    state.get_int = get_int;
    state.get_bool = get_bool;
    state.set_parameter = set_parameter;
    state.save_section = save_section;
    state.result = false;
    state.done = false;

    (void)parent;
    state.owner = GetActiveWindow();
    if (state.owner != NULL)
        EnableWindow(state.owner, FALSE);

    if (register_config_class() == 0)
    {
        if (state.owner != NULL)
            EnableWindow(state.owner, TRUE);
        return M64ERR_SYSTEM_FAIL;
    }

    RECT work_area;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);

    const int width = 480;
    const int height = 460;
    const int x = work_area.left + ((work_area.right - work_area.left) - width) / 2;
    const int y = work_area.top + ((work_area.bottom - work_area.top) - height) / 2;

    state.hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        SOFTRDP_CFG_CLASS,
        SOFTRDP_CFG_TITLE,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x,
        y,
        width,
        height,
        state.owner,
        NULL,
        GetModuleHandleW(NULL),
        &state);

    if (state.hwnd == NULL)
    {
        if (state.owner != NULL)
            EnableWindow(state.owner, TRUE);
        return M64ERR_SYSTEM_FAIL;
    }

    ShowWindow(state.hwnd, SW_SHOW);
    UpdateWindow(state.hwnd);
    SetForegroundWindow(state.hwnd);

    MSG msg;
    while (!state.done && GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (!IsDialogMessageW(state.hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (state.owner != NULL)
    {
        EnableWindow(state.owner, TRUE);
        SetForegroundWindow(state.owner);
    }

    return state.result ? M64ERR_SUCCESS : M64ERR_SUCCESS;
}

#endif /* _WIN32 */
