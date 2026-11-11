#ifdef N64VIDEO_C

// anamorphic NTSC resolution
#define H_RES_NTSC 640
#define V_RES_NTSC 480

// anamorphic PAL resolution
#define H_RES_PAL 768
#define V_RES_PAL 576

// typical VI_V_SYNC values for NTSC and PAL
#define V_SYNC_NTSC 525
#define V_SYNC_PAL 625

// maximum possible size of the prescale area
#define PRESCALE_WIDTH H_RES_NTSC
#define PRESCALE_HEIGHT V_SYNC_PAL

enum vi_type
{
    VI_TYPE_BLANK,      // no data, no sync
    VI_TYPE_RESERVED,   // unused, should never be set
    VI_TYPE_RGBA5551,   // 16 bit color (internally 18 bit RGBA5553)
    VI_TYPE_RGBA8888    // 32 bit color
};

enum vi_aa
{
    VI_AA_RESAMP_EXTRA_ALWAYS,  // resample and AA (always fetch extra lines)
    VI_AA_RESAMP_EXTRA,         // resample and AA (fetch extra lines if needed)
    VI_AA_RESAMP_ONLY,          // only resample (treat as all fully covered)
    VI_AA_REPLICATE             // replicate pixels, no interpolation
};

struct vi_reg_ctrl
{
    uint8_t type;
    bool gamma_dither_enable;
    bool gamma_enable;
    bool divot_enable;
    bool vbus_clock_enable;
    bool serrate;
    bool test_mode;
    uint8_t aa_mode;
    bool kill_we;
    uint8_t pixel_advance;
    bool dither_filter_enable;
};

typedef void(*vi_fetch_filter_func)(struct n64video_pixel*, uint32_t, uint32_t, struct vi_reg_ctrl, uint32_t, uint32_t);

#include "vi/gamma.c"
#include "vi/lerp.c"
#include "vi/divot.c"
#include "vi/video.c"
#include "vi/restore.c"
#include "vi/fetch.c"
#include "vi_deinterlace.h"

// states
static int32_t prevvicurrent;
static int32_t emucontrolsvicurrent;
static bool prevserrate;
static bool lowerfield;
static int32_t oldvstart;
static bool prevwasblank;
static int32_t vactivelines;
static bool ispal;
static int32_t minhpass;
static int32_t maxhpass;
static uint32_t x_add;
static uint32_t x_start;
static uint32_t y_add;
static uint32_t y_start;
static int32_t v_sync;
static int32_t vi_width_low;
static uint32_t frame_buffer;
static uint32_t tvfadeoutstate[PRESCALE_HEIGHT];
static uint32_t zb_address;
static int32_t vinnglitch;

// Prescale retains both temporal fields. Deinterlacing output is always built
// in a separate buffer so the next VI update can still access the opposite field.
static struct n64video_pixel prescale[PRESCALE_WIDTH * PRESCALE_HEIGHT];
static struct n64video_pixel bob_buffer[PRESCALE_WIDTH * PRESCALE_HEIGHT];

// Deinterlacer state. One history per field parity keeps only the real
// scanlines of that parity (indexed by absolute prescale coordinates), together
// with the per-pixel temporal change measured when that field was received.
// The buffers are static to keep the VI hot path free of allocations and are
// reset for every ROM/session and on every discontinuity.
struct vi_deint_history_info
{
    bool valid;
    uint32_t origin_x;
    uint32_t origin_y;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    int32_t vi_h_start;
    int32_t vi_v_start;
    int32_t vi_hres;
    int32_t vi_vres;
    int32_t vi_min_hpass;
    int32_t vi_max_hpass;
    bool hide_overscan;
    bool pal;
};

static struct n64video_pixel vi_deint_history[2][PRESCALE_WIDTH * PRESCALE_HEIGHT];
static uint8_t vi_deint_delta[2][PRESCALE_WIDTH * PRESCALE_HEIGHT];  /* raw per-pixel change */
static uint8_t vi_deint_zone[2][PRESCALE_WIDTH * PRESCALE_HEIGHT];   /* dilated motion zone */
static struct vi_deint_history_info vi_deint_info[2];
static enum vi_deinterlace_mode vi_deint_active_mode = VI_DEINTERLACE_OFF;
static int32_t vi_deint_last_parity = -1;

static uint32_t prescale_ptr;
static int32_t linecount;

// parsed VI registers
static uint32_t** vi_reg_ptr;
static struct vi_reg_ctrl ctrl;
static int32_t hres, vres;
static int32_t hres_raw, vres_raw;
static int32_t v_start;
static int32_t h_start;
static int32_t v_current_line;

static void vi_deint_reset_history(void)
{
    memset(vi_deint_info, 0, sizeof(vi_deint_info));
    vi_deint_last_parity = -1;
}

/* Effective deinterlacing mode. The legacy boolean selects Bob. */
static enum vi_deinterlace_mode vi_deint_effective_mode(void)
{
    enum vi_deinterlace_mode mode = config.vi.deinterlace_mode;

    if (mode == VI_DEINTERLACE_OFF && config.vi.bob_deinterlace) {
        mode = VI_DEINTERLACE_BOB;
    }
    if ((int)mode < (int)VI_DEINTERLACE_OFF || (int)mode >= (int)VI_DEINTERLACE_NUM) {
        mode = VI_DEINTERLACE_OFF;
    }
    return mode;
}

static void vi_init(void)
{
    vi_gamma_init();
    vi_restore_init();

    memset(prescale, 0, sizeof(prescale));
    memset(vi_deint_delta, 0, sizeof(vi_deint_delta));
    memset(vi_deint_zone, 0, sizeof(vi_deint_zone));
    vi_deint_active_mode = VI_DEINTERLACE_OFF;
    vi_deint_reset_history();

    prevvicurrent = 0;
    emucontrolsvicurrent = -1;
    lowerfield = false;
    prevserrate = false;
    oldvstart = 1337;
    prevwasblank = false;
    zb_address = 0;
}

static void vi_process_full_parallel(uint32_t worker_id)
{
    int32_t y;
    struct n64video_pixel *viaa_array = state[worker_id].viaa_array;
    struct n64video_pixel *divot_array = state[worker_id].divot_array;

    int32_t cache_marker = 0, cache_next_marker = 0, divot_cache_marker = 0, divot_cache_next_marker = 0;
    int32_t cache_marker_init = (x_start >> 10) - 1;

    struct n64video_pixel *viaa_cache = &viaa_array[0];
    struct n64video_pixel *viaa_cache_next = &viaa_array[0xa10];
    struct n64video_pixel *divot_cache = &divot_array[0];
    struct n64video_pixel *divot_cache_next = &divot_array[0xa10];

    struct n64video_pixel color, nextcolor, scancolor, scannextcolor;

    vi_fetch_filter_func vi_fetch_filter_ptr = ctrl.type & 1 ? vi_fetch_filter32 : vi_fetch_filter16;

    uint32_t pixels = 0, nextpixels = 0, fetchbugstate = 0;

    int32_t xfrac = 0, yfrac = 0;
    int32_t line_x = 0, next_line_x = 0, prev_line_x = 0, far_line_x = 0;
    int32_t prev_scan_x = 0, scan_x = 0, next_scan_x = 0, far_scan_x = 0;
    int32_t prev_x = 0, cur_x = 0, next_x = 0, far_x = 0;

    bool cache_init = false;

    pixels = 0;

    int32_t y_begin = 0;
    int32_t y_end = vres;
    int32_t y_inc = 1;

    if (config.parallel) {
        y_begin = worker_id;
        y_inc = parallel_num_workers();
    }

    for (y = y_begin; y < y_end; y += y_inc) {
        int32_t x;
        uint32_t x_offs = x_start;
        uint32_t curry = y_start + y * y_add;
        uint32_t nexty = y_start + (y + 1) * y_add;
        uint32_t prevy = curry >> 10;

        cache_marker = cache_next_marker = cache_marker_init;
        if (ctrl.divot_enable) {
            divot_cache_marker = divot_cache_next_marker = cache_marker_init;
        }

        struct n64video_pixel* pixel_row = &prescale[prescale_ptr + linecount * y];

        yfrac = (curry >> 5) & 0x1f;
        pixels = vi_width_low * prevy;
        nextpixels = vi_width_low + pixels;

        if (prevy == (nexty >> 10)) {
            fetchbugstate = 2;
        } else {
            fetchbugstate >>= 1;
        }

        for (x = 0; x < hres; x++, x_offs += x_add) {
            line_x = x_offs >> 10;
            prev_line_x = line_x - 1;
            next_line_x = line_x + 1;
            far_line_x = line_x + 2;

            cur_x = pixels + line_x;
            prev_x = pixels + prev_line_x;
            next_x = pixels + next_line_x;
            far_x = pixels + far_line_x;

            scan_x = nextpixels + line_x;
            prev_scan_x = nextpixels + prev_line_x;
            next_scan_x = nextpixels + next_line_x;
            far_scan_x = nextpixels + far_line_x;

            line_x++;
            prev_line_x++;
            next_line_x++;
            far_line_x++;

            xfrac = (x_offs >> 5) & 0x1f;

            if (prev_line_x > cache_marker) {
                vi_fetch_filter_ptr(&viaa_cache[prev_line_x], frame_buffer, prev_x, ctrl, vi_width_low, 0);
                vi_fetch_filter_ptr(&viaa_cache[line_x], frame_buffer, cur_x, ctrl, vi_width_low, 0);
                vi_fetch_filter_ptr(&viaa_cache[next_line_x], frame_buffer, next_x, ctrl, vi_width_low, 0);
                cache_marker = next_line_x;
            } else if (line_x > cache_marker) {
                vi_fetch_filter_ptr(&viaa_cache[line_x], frame_buffer, cur_x, ctrl, vi_width_low, 0);
                vi_fetch_filter_ptr(&viaa_cache[next_line_x], frame_buffer, next_x, ctrl, vi_width_low, 0);
                cache_marker = next_line_x;
            } else if (next_line_x > cache_marker) {
                vi_fetch_filter_ptr(&viaa_cache[next_line_x], frame_buffer, next_x, ctrl, vi_width_low, 0);
                cache_marker = next_line_x;
            }

            if (prev_line_x > cache_next_marker) {
                vi_fetch_filter_ptr(&viaa_cache_next[prev_line_x], frame_buffer, prev_scan_x, ctrl, vi_width_low, fetchbugstate);
                vi_fetch_filter_ptr(&viaa_cache_next[line_x], frame_buffer, scan_x, ctrl, vi_width_low, fetchbugstate);
                vi_fetch_filter_ptr(&viaa_cache_next[next_line_x], frame_buffer, next_scan_x, ctrl, vi_width_low, fetchbugstate);
                cache_next_marker = next_line_x;
            } else if (line_x > cache_next_marker) {
                vi_fetch_filter_ptr(&viaa_cache_next[line_x], frame_buffer, scan_x, ctrl, vi_width_low, fetchbugstate);
                vi_fetch_filter_ptr(&viaa_cache_next[next_line_x], frame_buffer, next_scan_x, ctrl, vi_width_low, fetchbugstate);
                cache_next_marker = next_line_x;
            } else if (next_line_x > cache_next_marker) {
                vi_fetch_filter_ptr(&viaa_cache_next[next_line_x], frame_buffer, next_scan_x, ctrl, vi_width_low, fetchbugstate);
                cache_next_marker = next_line_x;
            }

            if (ctrl.divot_enable) {
                if (far_line_x > cache_marker) {
                    vi_fetch_filter_ptr(&viaa_cache[far_line_x], frame_buffer, far_x, ctrl, vi_width_low, 0);
                    cache_marker = far_line_x;
                }

                if (far_line_x > cache_next_marker) {
                    vi_fetch_filter_ptr(&viaa_cache_next[far_line_x], frame_buffer, far_scan_x, ctrl, vi_width_low, fetchbugstate);
                    cache_next_marker = far_line_x;
                }

                if (line_x > divot_cache_marker) {
                    divot_filter(&divot_cache[line_x], viaa_cache[line_x], viaa_cache[prev_line_x], viaa_cache[next_line_x]);
                    divot_filter(&divot_cache[next_line_x], viaa_cache[next_line_x], viaa_cache[line_x], viaa_cache[far_line_x]);
                    divot_cache_marker = next_line_x;
                } else if (next_line_x > divot_cache_marker) {
                    divot_filter(&divot_cache[next_line_x], viaa_cache[next_line_x], viaa_cache[line_x], viaa_cache[far_line_x]);
                    divot_cache_marker = next_line_x;
                }

                if (line_x > divot_cache_next_marker) {
                    divot_filter(&divot_cache_next[line_x], viaa_cache_next[line_x], viaa_cache_next[prev_line_x], viaa_cache_next[next_line_x]);
                    divot_filter(&divot_cache_next[next_line_x], viaa_cache_next[next_line_x], viaa_cache_next[line_x], viaa_cache_next[far_line_x]);
                    divot_cache_next_marker = next_line_x;
                } else if (next_line_x > divot_cache_next_marker) {
                    divot_filter(&divot_cache_next[next_line_x], viaa_cache_next[next_line_x], viaa_cache_next[line_x], viaa_cache_next[far_line_x]);
                    divot_cache_next_marker = next_line_x;
                }

                color = divot_cache[line_x];
            } else {
                color = viaa_cache[line_x];
            }

            bool lerping = ctrl.aa_mode != VI_AA_REPLICATE && (xfrac || yfrac);

            if (lerping) {
                if (ctrl.divot_enable) {
                    nextcolor = divot_cache[next_line_x];
                    scancolor = divot_cache_next[line_x];
                    scannextcolor = divot_cache_next[next_line_x];
                } else {
                    nextcolor = viaa_cache[next_line_x];
                    scancolor = viaa_cache_next[line_x];
                    scannextcolor = viaa_cache_next[next_line_x];
                }

                vi_vl_lerp(&color, scancolor, yfrac);
                vi_vl_lerp(&nextcolor, scannextcolor, yfrac);
                vi_vl_lerp(&color, nextcolor, xfrac);
            } else if (vinnglitch) {
                if (prev_line_x & vinnglitch) {
                    color.r = color.g = color.b = 0;
                } else {
                    cur_x = pixels + (prev_line_x & (vinnglitch - 1));
                    vi_fetch_filter_ptr(&color, frame_buffer, cur_x, ctrl, vres, 0);

                    if (ctrl.divot_enable) {
                        struct n64video_pixel prevcol, nextcol;
                        prev_x = pixels + ((prev_line_x - 1) & (vinnglitch - 1));
                        next_x = pixels + (line_x & (vinnglitch - 1));
                        vi_fetch_filter_ptr(&prevcol, frame_buffer, prev_x, ctrl, vres, 0);
                        vi_fetch_filter_ptr(&nextcol, frame_buffer, next_x, ctrl, vres, 0);
                        divot_filter(&color, color, prevcol, nextcol);
                    }
                }
            }

            struct n64video_pixel* pixel = &pixel_row[x];

            if (x >= minhpass && x < maxhpass) {
                *pixel = color;
                gamma_filters(pixel, ctrl.gamma_enable, ctrl.gamma_dither_enable, &state[worker_id].vi_rseed);
            } else {
                pixel->r = pixel->g = pixel->b = 0;
            }
        }

        if (!cache_init && y_add == 0x400) {
            cache_marker = cache_next_marker;
            cache_next_marker = cache_marker_init;

            struct n64video_pixel* tempccvgptr = viaa_cache;
            viaa_cache = viaa_cache_next;
            viaa_cache_next = tempccvgptr;
            if (ctrl.divot_enable) {
                divot_cache_marker = divot_cache_next_marker;
                divot_cache_next_marker = cache_marker_init;
                tempccvgptr = divot_cache;
                divot_cache = divot_cache_next;
                divot_cache_next = tempccvgptr;
            }

            cache_init = true;
        }
    }
}

/*
 * Resolve the view currently exposed by VI. fb->pixels points into prescale;
 * deriving the origin from the pointer keeps full-frame and overscan-cropped
 * output on the same coordinate system.
 */
static bool vi_get_prescale_view(const struct n64video_frame_buffer* fb,
                                 uint32_t* origin_x, uint32_t* origin_y)
{
    size_t offset;

    if (!fb || !fb->pixels || fb->pitch != PRESCALE_WIDTH ||
        !fb->width || !fb->height || fb->width > PRESCALE_WIDTH ||
        fb->height > PRESCALE_HEIGHT) {
        return false;
    }

    offset = (size_t)(fb->pixels - prescale);
    if (offset >= (size_t)PRESCALE_WIDTH * PRESCALE_HEIGHT) {
        return false;
    }

    *origin_y = (uint32_t)(offset / PRESCALE_WIDTH);
    *origin_x = (uint32_t)(offset % PRESCALE_WIDTH);

    if (*origin_x > PRESCALE_WIDTH || fb->width > PRESCALE_WIDTH - *origin_x ||
        *origin_y > PRESCALE_HEIGHT || fb->height > PRESCALE_HEIGHT - *origin_y) {
        return false;
    }

    return true;
}

/* Same-parity comparison needs an identical view of the previous field. */
static bool vi_deint_geometry_matches(const struct vi_deint_history_info* info,
                                      uint32_t origin_x, uint32_t origin_y,
                                      uint32_t width, uint32_t height, uint32_t pitch)
{
    return info->valid && info->origin_x == origin_x && info->origin_y == origin_y &&
           info->width == width && info->height == height && info->pitch == pitch &&
           info->vi_h_start == h_start && info->vi_v_start == v_start &&
           info->vi_hres == hres && info->vi_vres == vres &&
           info->vi_min_hpass == minhpass && info->vi_max_hpass == maxhpass &&
           info->hide_overscan == config.vi.hide_overscan && info->pal == ispal;
}

/* The opposite field only has to share the horizontal layout; each of its
   rows is additionally checked against the vertical range it covers. */
static bool vi_deint_source_geometry_matches(const struct vi_deint_history_info* info,
                                             uint32_t origin_x, uint32_t width,
                                             uint32_t pitch)
{
    return info->valid && info->origin_x == origin_x && info->width == width &&
           info->pitch == pitch &&
           info->vi_h_start == h_start && info->vi_hres == hres &&
           info->vi_min_hpass == minhpass && info->vi_max_hpass == maxhpass &&
           info->hide_overscan == config.vi.hide_overscan && info->pal == ispal;
}

static void vi_deint_store_field(const struct n64video_frame_buffer* fb,
                                 uint32_t origin_x, uint32_t origin_y,
                                 uint32_t parity)
{
    const uint32_t width = fb->width;
    const uint32_t height = fb->height;
    struct vi_deint_history_info* info = &vi_deint_info[parity];

    /* Save only this field's real samples, indexed by absolute prescale
       coordinates, so later comparisons are independent of the output crop. */
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t absolute_y = origin_y + y;
        if ((absolute_y & 1u) == parity) {
            memcpy(&vi_deint_history[parity][(size_t)absolute_y * PRESCALE_WIDTH + origin_x],
                   &prescale[(size_t)absolute_y * PRESCALE_WIDTH + origin_x],
                   (size_t)width * sizeof(struct n64video_pixel));
        }
    }

    info->valid = true;
    info->origin_x = origin_x;
    info->origin_y = origin_y;
    info->width = width;
    info->height = height;
    info->pitch = fb->pitch;
    info->vi_h_start = h_start;
    info->vi_v_start = v_start;
    info->vi_hres = hres;
    info->vi_vres = vres;
    info->vi_min_hpass = minhpass;
    info->vi_max_hpass = maxhpass;
    info->hide_overscan = config.vi.hide_overscan;
    info->pal = ispal;
    vi_deint_last_parity = (int32_t)parity;
}

static void vi_deinterlace_bob(struct n64video_frame_buffer* fb)
{
    uint32_t origin_x, origin_y;
    uint32_t width = fb->width;
    uint32_t height = fb->height;
    uint32_t parity = lowerfield ? 1u : 0u;

    if (!ctrl.serrate || !vi_get_prescale_view(fb, &origin_x, &origin_y) || height < 2u) {
        return;
    }

    for (uint32_t y = 0; y < height; ++y) {
        uint32_t absolute_y = origin_y + y;
        struct n64video_pixel* destination = &bob_buffer[(size_t)y * width];
        const struct n64video_pixel* source =
            &prescale[(size_t)absolute_y * PRESCALE_WIDTH + origin_x];

        if ((absolute_y & 1u) == parity) {
            /* Keep every real sample at its original scanline parity. */
            memcpy(destination, source, (size_t)width * sizeof(*destination));
        } else {
            /* Reconstruct a missing line only from the current field. */
            uint32_t upper_y = y > 0u ? y - 1u : y + 1u;
            uint32_t lower_y = y + 1u < height ? y + 1u : upper_y;
            const struct n64video_pixel* upper =
                &prescale[(size_t)(origin_y + upper_y) * PRESCALE_WIDTH + origin_x];
            const struct n64video_pixel* lower =
                &prescale[(size_t)(origin_y + lower_y) * PRESCALE_WIDTH + origin_x];

            vi_deint_bob_row(destination, upper, lower, width);
        }
    }

    fb->pixels = bob_buffer;
    fb->pitch = width;
}

static void vi_deinterlace_adaptive(struct n64video_frame_buffer* fb)
{
    uint32_t origin_x, origin_y;
    const uint32_t width = fb->width;
    const uint32_t height = fb->height;
    const uint32_t parity = lowerfield ? 1u : 0u;
    const uint32_t opposite = parity ^ 1u;
    bool sequence_ok;
    bool can_compare;
    bool can_use_opposite;

    if (!ctrl.serrate || !vi_get_prescale_view(fb, &origin_x, &origin_y) || height < 2u) {
        vi_deint_reset_history();
        return;
    }

    /* A repeated field parity means that at least one temporal field was
       skipped or the VI mode changed. Never weave across that discontinuity. */
    sequence_ok = vi_deint_last_parity < 0 || vi_deint_last_parity == (int32_t)opposite;
    if (!sequence_ok) {
        vi_deint_reset_history();
    }

    can_compare = sequence_ok &&
                  vi_deint_geometry_matches(&vi_deint_info[parity], origin_x, origin_y,
                                            width, height, fb->pitch);
    can_use_opposite = sequence_ok &&
                       vi_deint_source_geometry_matches(&vi_deint_info[opposite],
                                                        origin_x, width, fb->pitch);

    /* Temporal change of every real scanline of this field. When no valid
       same-parity history exists the change is "unknown" (maximum), which
       makes the neighbouring reconstructed lines fall back to Bob. */
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t absolute_y = origin_y + y;
        size_t offset = (size_t)absolute_y * PRESCALE_WIDTH + origin_x;

        if ((absolute_y & 1u) != parity) {
            continue;
        }
        vi_deint_motion_row(&vi_deint_delta[parity][offset], &prescale[offset],
                            can_compare ? &vi_deint_history[parity][offset] : NULL, width);
    }

    /* Dilate into the motion zone used for decisions. */
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t absolute_y = origin_y + y;
        size_t offset = (size_t)absolute_y * PRESCALE_WIDTH + origin_x;
        const uint8_t* above = NULL;
        const uint8_t* below = NULL;

        if ((absolute_y & 1u) != parity) {
            continue;
        }
        if (y >= 2u) {
            above = &vi_deint_delta[parity][offset - 2u * PRESCALE_WIDTH];
        }
        if (y + 2u < height) {
            below = &vi_deint_delta[parity][offset + 2u * PRESCALE_WIDTH];
        }
        vi_deint_zone_row(&vi_deint_zone[parity][offset], &vi_deint_delta[parity][offset],
                          above, below, width);
    }

    for (uint32_t y = 0; y < height; ++y) {
        uint32_t absolute_y = origin_y + y;
        struct n64video_pixel* destination = &bob_buffer[(size_t)y * width];
        const struct n64video_pixel* source =
            &prescale[(size_t)absolute_y * PRESCALE_WIDTH + origin_x];

        if ((absolute_y & 1u) == parity) {
            /* The current field's real scanline is always retained verbatim. */
            memcpy(destination, source, (size_t)width * sizeof(*destination));
        } else {
            uint32_t upper_y = y > 0u ? y - 1u : y + 1u;
            uint32_t lower_y = y + 1u < height ? y + 1u : upper_y;
            size_t upper_off = (size_t)(origin_y + upper_y) * PRESCALE_WIDTH + origin_x;
            size_t lower_off = (size_t)(origin_y + lower_y) * PRESCALE_WIDTH + origin_x;
            size_t line_off = (size_t)absolute_y * PRESCALE_WIDTH + origin_x;
            bool opposite_row_valid = can_use_opposite &&
                absolute_y >= vi_deint_info[opposite].origin_y &&
                absolute_y < vi_deint_info[opposite].origin_y + vi_deint_info[opposite].height;

            if (upper_y == lower_y) {
                /* Picture edge: only one real neighbour exists. */
                vi_deint_bob_row(destination, &prescale[upper_off], &prescale[lower_off], width);
            } else {
                vi_deint_adaptive_row(destination,
                                      &prescale[upper_off], &prescale[lower_off],
                                      &vi_deint_zone[parity][upper_off],
                                      &vi_deint_zone[parity][lower_off],
                                      opposite_row_valid ? &vi_deint_history[opposite][line_off] : NULL,
                                      opposite_row_valid ? &vi_deint_zone[opposite][line_off] : NULL,
                                      width);
            }
        }
    }

    vi_deint_store_field(fb, origin_x, origin_y, parity);

    fb->pixels = bob_buffer;
    fb->pitch = width;
}

static void vi_deinterlace(struct n64video_frame_buffer* fb)
{
    enum vi_deinterlace_mode mode = vi_deint_effective_mode();

    /* Do not reuse temporal state when the selected algorithm changes. */
    if (mode != vi_deint_active_mode) {
        vi_deint_reset_history();
        vi_deint_active_mode = mode;
    }

    if (!ctrl.serrate || mode == VI_DEINTERLACE_OFF) {
        vi_deint_reset_history();
        return;
    }

    if (mode == VI_DEINTERLACE_BOB) {
        vi_deint_reset_history();
        vi_deinterlace_bob(fb);
    } else if (mode == VI_DEINTERLACE_ADAPTIVE) {
        vi_deinterlace_adaptive(fb);
    } else {
        vi_deint_reset_history();
    }
}

static bool vi_process_full(struct n64video_frame_buffer* fb)
{
    bool isblank = (ctrl.type & 2) == 0;
    bool validinterlace = !isblank && ctrl.serrate;

    if (!validinterlace) {
        vi_deint_reset_history();
    }

    if (validinterlace) {
        if (prevserrate && emucontrolsvicurrent < 0) {
            emucontrolsvicurrent = v_current_line != prevvicurrent;
        }

        if (emucontrolsvicurrent == 1) {
            lowerfield = v_current_line ^ 1;
        } else if (!emucontrolsvicurrent) {
            if (v_start == oldvstart) {
                lowerfield ^= true;
            } else {
                lowerfield = v_start < oldvstart;
            }
        }

        prevvicurrent = v_current_line;
    }

    oldvstart = v_start;
    prevserrate = validinterlace;

    bool validh = hres > 0 && h_start < PRESCALE_WIDTH;
    int32_t h_end = hres + h_start; // note: the result appears to be different to VI_H_END
    int32_t hrightblank = PRESCALE_WIDTH - h_end;

    if (isblank && prevwasblank) {
        vi_deint_reset_history();
        return false;
    }

    prevwasblank = isblank;

    linecount = PRESCALE_WIDTH << ctrl.serrate;
    prescale_ptr = v_start * linecount + h_start + (lowerfield ? PRESCALE_WIDTH : 0);

    int32_t i;
    if (isblank) {
        // blank signal, clear entire screen buffer
        memset(tvfadeoutstate, 0, PRESCALE_HEIGHT * sizeof(uint32_t));
        memset(prescale, 0, sizeof(prescale));
    } else {
        // clear left border
        int32_t j;
        if (h_start > 0 && h_start < PRESCALE_WIDTH) {
            for (i = 0; i < vactivelines; i++) {
                memset(&prescale[i * PRESCALE_WIDTH], 0, h_start * sizeof(uint32_t));
            }
        }

        // clear right border
        if (h_end >= 0 && h_end < PRESCALE_WIDTH) {
            for (i = 0; i < vactivelines; i++) {
                memset(&prescale[i * PRESCALE_WIDTH + h_end], 0, hrightblank * sizeof(uint32_t));
            }
        }

        // clear top border
        for (i = 0; i < ((v_start << ctrl.serrate) + lowerfield); i++) {
            if (tvfadeoutstate[i]) {
                tvfadeoutstate[i]--;
                if (!tvfadeoutstate[i]) {
                    if (validh) {
                        memset(&prescale[i * PRESCALE_WIDTH + h_start], 0, hres * sizeof(uint32_t));
                    } else {
                        memset(&prescale[i * PRESCALE_WIDTH], 0, PRESCALE_WIDTH * sizeof(uint32_t));
                    }
                }
            }
        }

        if (!ctrl.serrate) {
            for(j = 0; j < vres; j++) {
                if (validh) {
                    tvfadeoutstate[i] = 2;
                } else if (tvfadeoutstate[i]) {
                    tvfadeoutstate[i]--;
                    if (!tvfadeoutstate[i]) {
                        memset(&prescale[i * PRESCALE_WIDTH], 0, PRESCALE_WIDTH * sizeof(uint32_t));
                    }
                }

                i++;
            }
        } else {
            for(j = 0; j < vres; j++) {
                if (validh) {
                    tvfadeoutstate[i] = 2;
                } else if (tvfadeoutstate[i]) {
                    tvfadeoutstate[i]--;
                    if (!tvfadeoutstate[i]) {
                        memset(&prescale[i * PRESCALE_WIDTH], 0, PRESCALE_WIDTH * sizeof(uint32_t));
                    }
                }

                if (tvfadeoutstate[i + 1]) {
                    tvfadeoutstate[i + 1]--;
                    if (!tvfadeoutstate[i + 1]) {
                        if (validh) {
                            memset(&prescale[(i + 1) * PRESCALE_WIDTH + h_start], 0, hres * sizeof(uint32_t));
                        } else {
                            memset(&prescale[(i + 1) * PRESCALE_WIDTH], 0, PRESCALE_WIDTH * sizeof(uint32_t));
                        }
                    }
                }

                i += 2;
            }
        }

        // clear bottom border
        for (; i < vactivelines; i++) {
            if (tvfadeoutstate[i]) {
                tvfadeoutstate[i]--;
            }
            if (!tvfadeoutstate[i]) {
                if (validh) {
                    memset(&prescale[i * PRESCALE_WIDTH + h_start], 0, hres * sizeof(uint32_t));
                } else {
                    memset(&prescale[i * PRESCALE_WIDTH], 0, PRESCALE_WIDTH * sizeof(uint32_t));
                }
            }
        }
    }

    if (!validh) {
        vi_deint_reset_history();
        return false;
    }

    // run filter update in parallel if enabled
    if (config.parallel) {
        parallel_run(vi_process_full_parallel);
    } else {
        vi_process_full_parallel(0);
    }

    // finish and send buffer to screen
    fb->pixels = prescale;
    fb->pitch = PRESCALE_WIDTH;

    if (config.vi.hide_overscan) {
        // crop away overscan area from prescale
        fb->width = maxhpass - minhpass;
        fb->height = vres << ctrl.serrate;
        fb->height_out = (vres << 1) * V_SYNC_NTSC / v_sync;
        int32_t x = h_start + minhpass;
        /* The native output shifts the window by one field line for the lower
           field. Deinterlaced output must keep both fields in one fixed
           window, otherwise the whole picture jumps by one field line every
           field. */
        bool field_shift = emucontrolsvicurrent && !(ctrl.serrate && vi_deint_effective_mode() != VI_DEINTERLACE_OFF);
        int32_t y = (v_start + (field_shift ? lowerfield : 0)) << ctrl.serrate;
        fb->pixels += x + y * fb->pitch;
    } else {
        // use entire prescale buffer
        fb->width = PRESCALE_WIDTH;
        fb->height = (ispal ? V_RES_PAL : V_RES_NTSC) >> !ctrl.serrate;
        fb->height_out = V_RES_NTSC;
    }

    // convert to 16:9 if enabled
    if (config.vi.widescreen) {
        fb->height_out = fb->height_out * 3 / 4;
    }

    /* Apply deinterlacing after VI filtering, without changing the RDP data. */
    vi_deinterlace(fb);

    return fb->width > 0 && fb->height > 0;
}

static void vi_process_fast_parallel(uint32_t worker_id)
{
    int32_t y;
    int32_t y_begin = 0;
    int32_t y_end = vres_raw;
    int32_t y_inc = 1;

    // drop every other interlaced frame to avoid "wobbly" output due to the
    // vertical offset
    // TODO: completely skip rendering these frames in unfiltered to improve
    // performance?
    if (ctrl.serrate && v_current_line) {
        return;
    }

    if (config.parallel) {
        y_begin = worker_id;
        y_inc = parallel_num_workers();
    }

    for (y = y_begin; y < y_end; y += y_inc) {
        int32_t x;
        int32_t line = y * vi_width_low;

        struct n64video_pixel* pixel_row = &prescale[y * hres_raw];

        for (x = 0; x < hres_raw; x++) {
            struct n64video_pixel* pixel = &pixel_row[x];

            switch (config.vi.mode) {
                case VI_MODE_COLOR:
                    switch (ctrl.type) {
                        case VI_TYPE_RGBA5551: {
                            uint16_t pix = rdram_read_idx16((frame_buffer >> 1) + line + x);
                            pixel->r = (uint8_t)RGBA16_R(pix);
                            pixel->g = (uint8_t)RGBA16_G(pix);
                            pixel->b = (uint8_t)RGBA16_B(pix);
                            break;
                        }

                        case VI_TYPE_RGBA8888: {
                            uint32_t pix = rdram_read_idx32((frame_buffer >> 2) + line + x);
                            pixel->r = (uint8_t)RGBA32_R(pix);
                            pixel->g = (uint8_t)RGBA32_G(pix);
                            pixel->b = (uint8_t)RGBA32_B(pix);
                            break;
                        }

                        default:
                            return;
                    }

                    gamma_filters(pixel, ctrl.gamma_enable, false, &state[worker_id].vi_rseed);
                    break;

                case VI_MODE_DEPTH: {
                    if (zb_address) {
                        pixel->r = pixel->g = pixel->b = rdram_read_idx16((zb_address >> 1) + line + x) >> 8;
                    }
                    break;
                }

                case VI_MODE_COVERAGE: {
                    // TODO: incorrect for RGBA8888?
                    uint8_t hval;
                    uint16_t pix;
                    rdram_read_pair16(&pix, &hval, (frame_buffer >> 1) + line + x);
                    pixel->r = pixel->g = pixel->b = (((pix & 1) << 2) | hval) << 5;
                    break;
                }

                default:
                    return;
            }
        }
    }
}

static bool vi_process_fast(struct n64video_frame_buffer* fb)
{
    // note: this is probably a very, very crude method to get the frame size,
    // but should hopefully work most of the time
    hres_raw = (int32_t)x_add * hres / 1024;
    vres_raw = (int32_t)y_add * vres / 1024;

    // skip invalid frame sizes
    if (hres_raw <= 0 || vres_raw <= 0) {
        return false;
    }

    // skip blank/invalid modes
    if (!(ctrl.type & 2)) {
        return false;
    }

    // run filter update in parallel if enabled
    if (config.parallel) {
        parallel_run(vi_process_fast_parallel);
    } else {
        vi_process_fast_parallel(0);
    }

    // finish and send buffer to screen
    fb->pixels = prescale;
    fb->width = hres_raw;
    fb->height = vres_raw;
    fb->pitch = hres_raw;

    // get display size of filtered mode
    int32_t filtered_width = maxhpass - minhpass;
    int32_t filtered_height = (vres << 1) * V_SYNC_NTSC / v_sync;

    // re-calculate cropped 8 pixel area on the left and right from filtered mode
    int32_t border_width = (hres - filtered_width) * hres_raw / hres;
    fb->pixels += (border_width / 2) + 1;
    fb->width -= border_width;

    // force aspect ratio of filtered mode
    fb->height_out = fb->width * filtered_height / filtered_width;

    // convert to 16:9 if enabled
    if (config.vi.widescreen) {
        fb->height_out = fb->height_out * 3 / 4;
    }

    return fb->width > 0 && fb->height > 0;
}

void vi_set_zbuffer_address(uint32_t address)
{
    zb_address = address;
}

void n64video_update_screen(struct n64video_frame_buffer* fb)
{
    // check for configuration errors
    if (config.vi.mode >= VI_MODE_NUM) {
        msg_error("Invalid VI mode: %d", config.vi.mode);
    }

    // parse and check some common registers
    vi_reg_ptr = config.gfx.vi_reg;

    v_start = (*vi_reg_ptr[VI_V_START] >> 16) & 0x3ff;
    h_start = (*vi_reg_ptr[VI_H_START] >> 16) & 0x3ff;

    int32_t v_end = *vi_reg_ptr[VI_V_START] & 0x3ff;
    int32_t h_end = *vi_reg_ptr[VI_H_START] & 0x3ff;

    hres =  h_end - h_start;
    vres = (v_end - v_start) >> 1; // vertical is measured in half-lines

    x_add = *vi_reg_ptr[VI_X_SCALE] & 0xfff;
    x_start = (*vi_reg_ptr[VI_X_SCALE] >> 16) & 0xfff;

    y_add = *vi_reg_ptr[VI_Y_SCALE] & 0xfff;
    y_start = (*vi_reg_ptr[VI_Y_SCALE] >> 16) & 0xfff;

    v_sync = *vi_reg_ptr[VI_V_SYNC] & 0x3ff;
    v_current_line = *vi_reg_ptr[VI_V_CURRENT_LINE] & 1;

    vi_width_low = *vi_reg_ptr[VI_WIDTH] & 0xfff;
    frame_buffer = *vi_reg_ptr[VI_ORIGIN] & 0xffffff;

    if (ctrl.aa_mode == VI_AA_REPLICATE && (ctrl.type & 2) && h_start < (ctrl.type == VI_TYPE_RGBA5551 ? 0x80 : 0x40) && x_add <= 0x200) {
        vinnglitch = ctrl.type == VI_TYPE_RGBA5551 ? 0x40 : 0x20;
    } else {
        vinnglitch = 0;
    }

    // cancel if the frame buffer contains no valid address
    if (!frame_buffer) {
        vi_deint_reset_history();
        fb->valid = false;
        return;
    }

    // split up VI_CONTROL bits
    uint32_t vi_control = *vi_reg_ptr[VI_STATUS];
    ctrl.type = vi_control & 3;
    ctrl.gamma_dither_enable = (vi_control >> 2) & 1;
    ctrl.gamma_enable = (vi_control >> 3) & 1;
    ctrl.divot_enable = (vi_control >> 4) & 1;
    ctrl.vbus_clock_enable = (vi_control >> 5) & 1;
    ctrl.serrate = (vi_control >> 6) & 1;
    ctrl.test_mode = (vi_control >> 7) & 1;
    ctrl.aa_mode = (vi_control >> 8) & 3;
    ctrl.kill_we = (vi_control >> 11) & 1;
    ctrl.pixel_advance = (vi_control >> 12) & 0x7;
    ctrl.dither_filter_enable = (vi_control >> 16) & 1;

    // check for unexpected VI type bits set
    if (ctrl.type & ~3) {
        msg_error("Unknown framebuffer format %d", ctrl.type);
    }

    // check for the dangerous vbus_clock_enable flag. it was introduced to
    // configure Ultra 64 prototypes and enabling it on final hardware will
    // enable two output drivers on the same bus at the same time
    if (ctrl.vbus_clock_enable && !onetimewarnings.vbusclock) {
        msg_warning("vi_update: vbus_clock_enable bit set in VI_CONTROL_REG "
                    "register. Never run this code on your N64! It's rumored "
                    "that turning this bit on will result in permanent damage "
                    "to the hardware! Emulation will now continue.");
        onetimewarnings.vbusclock = true;
    }

    // adjust sizes and offsets
    ispal = v_sync > (V_SYNC_NTSC + 25);
    h_start -= (ispal ? 128 : 108);

    bool h_start_clamped = false;

    if (h_start < 0) {
        x_start += (x_add * (-h_start));
        hres += h_start;

        h_start = 0;
        h_start_clamped = true;
    }

    int32_t vstartoffset = ispal ? 44 : 34;
    v_start = (v_start - vstartoffset) / 2;

    if (v_start < 0) {
        y_start += (y_add * (uint32_t)(-v_start));
        v_start = 0;
    }

    bool hres_clamped = false;

    if ((hres + h_start) > PRESCALE_WIDTH) {
        hres = PRESCALE_WIDTH - h_start;
        hres_clamped = true;
    }

    if ((vres + v_start) > PRESCALE_HEIGHT) {
        vres = PRESCALE_HEIGHT - v_start;
        msg_warning("vres = %d v_start = %d v_video_start = %d", vres, v_start, (*vi_reg_ptr[VI_V_START] >> 16) & 0x3ff);
    }

    vactivelines = v_sync - vstartoffset;

    if (vactivelines < 0 || vactivelines > PRESCALE_HEIGHT) {
        vi_deint_reset_history();
        if (vactivelines > PRESCALE_HEIGHT) {
            msg_error("VI_V_SYNC_REG too big");
        }
    }

    fb->valid = true;

    if (vactivelines >= 0) {
        uint32_t lineshifter = !ctrl.serrate;
        vactivelines >>= lineshifter;

        minhpass = h_start_clamped ? 0 : 8;
        maxhpass = hres_clamped ? hres : (hres - 7);

        // run filter update in parallel if enabled
        if (config.vi.mode == VI_MODE_NORMAL) {
            fb->valid = vi_process_full(fb);
        } else {
            /* No field pairing is performed by the unfiltered VI path. */
            vi_deint_reset_history();
            fb->valid = vi_process_fast(fb);
        }
    }
}

static void vi_close(void)
{
    vi_deint_reset_history();
}

#endif // N64VIDEO_C
