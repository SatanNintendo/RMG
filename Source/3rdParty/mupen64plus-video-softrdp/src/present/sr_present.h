#ifndef SR_PRESENT_H
#define SR_PRESENT_H

#include "../core/sr_defs.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
typedef void *HWND;
#endif

typedef struct sr_present {
    HWND hwnd;
    void *hdc;
    void *glrc;
    uint32_t texture;
    uint32_t program;
    uint32_t vao;
    uint32_t frame_width;
    uint32_t frame_height;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t window_width;
    uint32_t window_height;
    bool ready;
    bool has_frame;
    bool external_context;
    /* Presentation options (sr_present_set_options). */
    bool integer_scale;
    bool bilinear_filter;
    /* The sampling filter last written to the frame texture, so a changed
     * option rewrites it once rather than on every frame. */
    bool bilinear_filter_applied;
} sr_present;

bool sr_present_init(sr_present *present, HWND hwnd);
bool sr_present_init_external(sr_present *present, void *(*load_proc)(const char *name));
void sr_present_shutdown(sr_present *present);
void sr_present_clear(sr_present *present);
void sr_present_set_display_size(sr_present *present, uint32_t width, uint32_t height);
void sr_present_set_window_size(sr_present *present, uint32_t width, uint32_t height);
/*
 * Presentation options, in force from the next frame on. Integer scale draws
 * the picture at whole-number multiples of its display resolution, centred in
 * the window, so every pixel covers the same block of screen pixels instead
 * of some coming out wider than their neighbours; bilinear filtering smooths
 * the scaled picture instead of sampling nearest neighbours. Both off is the
 * default look. Set after one of the init functions: both reset the options,
 * as does shutdown.
 */
void sr_present_set_options(sr_present *present, bool integer_scale, bool bilinear_filter);
void sr_present_draw(sr_present *present);
bool sr_present_upload_rgba8(sr_present *present,
                             const sr_rgba8 *pixels,
                             uint32_t width,
                             uint32_t height,
                             uint32_t stride_pixels);

#endif
