#ifndef ANGRYLION_PLUS_VI_DEINTERLACE_H
#define ANGRYLION_PLUS_VI_DEINTERLACE_H

#include "../n64video.h"
#include <stddef.h>
#include <string.h>

/*
 * Row-level deinterlacing primitives used by vi.c. This header has no
 * renderer/plugin dependencies.
 *
 * Bob:      a missing scanline is the average of the two real scanlines of
 *           the CURRENT field that border it. Real scanlines are never changed.
 *
 * Adaptive: a missing scanline starts from the Bob value (s) and the real
 *           scanline of the previous (opposite) field (w, "weave"). The result
 *           is clamp(s, w - B, w + B) per channel, where B is an upper bound of
 *           how much the picture may have changed at this pixel:
 *             - B = 0      -> exactly w (static picture, full vertical detail)
 *             - B >= |s-w| -> exactly s (moving picture, identical to Bob)
 *           B grows continuously with measured motion, so there is no hard
 *           switch between two different looks (the cause of visible
 *           distortion with threshold based detectors).
 *           B is derived from the "motion zone": the per-pixel temporal change
 *           (same field parity, t vs t-2) dilated horizontally and over the
 *           neighbouring real scanlines, taken from the current field and from
 *           the opposite field one field earlier.
 */

#define VI_DEINT_DELTA_UNKNOWN 255u

/* Gains are expressed in 1/4 units: B = (value * gain) / 4. */
#ifndef VI_DEINT_TEMPORAL_GAIN
#define VI_DEINT_TEMPORAL_GAIN 8u   /* 2.0 x motion (see header comment) */
#endif
#ifndef VI_DEINT_OPPOSITE_SHIFT
#define VI_DEINT_OPPOSITE_SHIFT 0u  /* opposite-field motion shift */
#endif
#ifndef VI_DEINT_NOISE_FLOOR
#define VI_DEINT_NOISE_FLOOR 6u     /* temporal change below this is dither noise */
#endif
#ifndef VI_DEINT_ZONE_RADIUS
#define VI_DEINT_ZONE_RADIUS 3u     /* horizontal dilation of the motion zone */
#endif

static inline uint32_t vi_deint_abs_difference(uint32_t a, uint32_t b)
{
    return a > b ? a - b : b - a;
}

/* Temporal difference includes luma and chroma so isoluminant changes are
   not silently treated as static. Result is in 0..255. */
static inline uint32_t vi_deint_pixel_delta(const struct n64video_pixel* current,
                                           const struct n64video_pixel* previous)
{
    uint32_t current_y = (77u * current->r + 150u * current->g + 29u * current->b) >> 8;
    uint32_t previous_y = (77u * previous->r + 150u * previous->g + 29u * previous->b) >> 8;
    uint32_t luma_delta = vi_deint_abs_difference(current_y, previous_y);
    uint32_t r_delta = vi_deint_abs_difference(current->r, previous->r);
    uint32_t g_delta = vi_deint_abs_difference(current->g, previous->g);
    uint32_t b_delta = vi_deint_abs_difference(current->b, previous->b);
    uint32_t chroma_delta = r_delta;

    if (g_delta > chroma_delta) chroma_delta = g_delta;
    if (b_delta > chroma_delta) chroma_delta = b_delta;
    chroma_delta = (chroma_delta * 3u + 2u) >> 2;

    return luma_delta > chroma_delta ? luma_delta : chroma_delta;
}

/* Per-pixel temporal change of one real scanline against the same scanline of
   the previous field of the same parity. previous == NULL marks "unknown". */
static inline void vi_deint_motion_row(uint8_t* delta,
                                       const struct n64video_pixel* current,
                                       const struct n64video_pixel* previous,
                                       uint32_t width)
{
    uint32_t x;

    if (!previous) {
        memset(delta, (int)VI_DEINT_DELTA_UNKNOWN, width);
        return;
    }
    for (x = 0; x < width; ++x) {
        uint32_t d = vi_deint_pixel_delta(&current[x], &previous[x]);
        delta[x] = (uint8_t)(d > 255u ? 255u : d);
    }
}

static inline void vi_deint_bob_row(struct n64video_pixel* destination,
                                    const struct n64video_pixel* upper,
                                    const struct n64video_pixel* lower,
                                    uint32_t width)
{
    uint32_t x;

    for (x = 0; x < width; ++x) {
        destination[x].r = (uint8_t)(((uint16_t)upper[x].r + lower[x].r + 1u) >> 1);
        destination[x].g = (uint8_t)(((uint16_t)upper[x].g + lower[x].g + 1u) >> 1);
        destination[x].b = (uint8_t)(((uint16_t)upper[x].b + lower[x].b + 1u) >> 1);
        destination[x].a = (uint8_t)(((uint16_t)upper[x].a + lower[x].a + 1u) >> 1);
    }
}

static inline uint32_t vi_deint_clamp_x(int32_t x, uint32_t width)
{
    if (x < 0) {
        return 0u;
    }
    if ((uint32_t)x >= width) {
        return width - 1u;
    }
    return (uint32_t)x;
}

static inline uint32_t vi_deint_max3(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t m = a > b ? a : b;
    return m > c ? m : c;
}

static inline uint8_t vi_deint_clamp_channel(int32_t value, int32_t low, int32_t high)
{
    if (low < 0) low = 0;
    if (high > 255) high = 255;
    if (value < low) value = low;
    if (value > high) value = high;
    return (uint8_t)value;
}

/* Motion zone of one real scanline: maximum of the per-pixel temporal change
   over a horizontal radius and over the same scanline plus the real scanlines
   directly above and below it (same parity, two rows apart). Missing
   neighbours are passed as NULL. */
static inline void vi_deint_zone_row(uint8_t* destination,
                                     const uint8_t* row,
                                     const uint8_t* row_above,
                                     const uint8_t* row_below,
                                     uint32_t width)
{
    uint8_t vertical[1024];
    uint32_t x;
    uint32_t radius = VI_DEINT_ZONE_RADIUS;

    if (width == 0u || width > sizeof(vertical)) {
        memset(destination, (int)VI_DEINT_DELTA_UNKNOWN, width);
        return;
    }

    for (x = 0; x < width; ++x) {
        uint8_t m = row[x];
        if (row_above && row_above[x] > m) m = row_above[x];
        if (row_below && row_below[x] > m) m = row_below[x];
        vertical[x] = m;
    }
    for (x = 0; x < width; ++x) {
        uint32_t x0 = x > radius ? x - radius : 0u;
        uint32_t x1 = x + radius < width ? x + radius : width - 1u;
        uint8_t m = 0u;
        uint32_t i;
        for (i = x0; i <= x1; ++i) {
            if (vertical[i] > m) m = vertical[i];
        }
        destination[x] = m;
    }
}

/*
 * Reconstruct one missing scanline.
 *   upper/lower          real rows of the current field bordering the line
 *   delta_upper/lower    temporal change of those rows (vi_deint_motion_row)
 *   woven                real row of the opposite field at this line, or NULL
 *   delta_woven          temporal change of the woven row, or NULL
 * With woven == NULL the result is exactly Bob.
 */
static inline void vi_deint_adaptive_row(struct n64video_pixel* destination,
                                         const struct n64video_pixel* upper,
                                         const struct n64video_pixel* lower,
                                         const uint8_t* delta_upper,
                                         const uint8_t* delta_lower,
                                         const struct n64video_pixel* woven,
                                         const uint8_t* delta_woven,
                                         uint32_t width)
{
    uint32_t x;

    if (!woven || !delta_woven || width == 0u) {
        vi_deint_bob_row(destination, upper, lower, width);
        return;
    }

    for (x = 0; x < width; ++x) {
        uint32_t motion = delta_upper[x];
        uint32_t opposite = delta_woven[x];
        uint32_t bound;
        int32_t s;
        int32_t b;

        if (delta_lower[x] > motion) motion = delta_lower[x];
        motion = motion > VI_DEINT_NOISE_FLOOR ? motion - VI_DEINT_NOISE_FLOOR : 0u;
        opposite = opposite > VI_DEINT_NOISE_FLOOR ? opposite - VI_DEINT_NOISE_FLOOR : 0u;

        bound = (motion * VI_DEINT_TEMPORAL_GAIN) >> 2;
        opposite = ((opposite >> VI_DEINT_OPPOSITE_SHIFT) * VI_DEINT_TEMPORAL_GAIN) >> 2;
        if (opposite > bound) bound = opposite;
        if (bound > 255u) bound = 255u;
        b = (int32_t)bound;

        s = ((int32_t)upper[x].r + lower[x].r + 1) >> 1;
        destination[x].r = vi_deint_clamp_channel(s, (int32_t)woven[x].r - b, (int32_t)woven[x].r + b);
        s = ((int32_t)upper[x].g + lower[x].g + 1) >> 1;
        destination[x].g = vi_deint_clamp_channel(s, (int32_t)woven[x].g - b, (int32_t)woven[x].g + b);
        s = ((int32_t)upper[x].b + lower[x].b + 1) >> 1;
        destination[x].b = vi_deint_clamp_channel(s, (int32_t)woven[x].b - b, (int32_t)woven[x].b + b);
        destination[x].a = (uint8_t)(((uint16_t)upper[x].a + lower[x].a + 1u) >> 1);
    }
}

#endif /* ANGRYLION_PLUS_VI_DEINTERLACE_H */
