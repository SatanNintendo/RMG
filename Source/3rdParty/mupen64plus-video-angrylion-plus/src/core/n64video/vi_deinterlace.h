#ifndef ANGRYLION_PLUS_VI_DEINTERLACE_H
#define ANGRYLION_PLUS_VI_DEINTERLACE_H

#include "../n64video.h"
#include <stddef.h>

/* Shared thresholds and pixel helpers used by the VI implementation and the
   standalone tests. Keep this header free of renderer/plugin dependencies. */
#define VI_MAD_ENTER_THRESHOLD 20u
#define VI_MAD_EXIT_THRESHOLD 11u
#define VI_EDI_MIN_COST_GAIN 18u
#define VI_EDI_MAX_BEST_COST 96u

static inline uint32_t vi_deint_abs_difference(uint32_t a, uint32_t b)
{
    return a > b ? a - b : b - a;
}

/* Temporal difference includes luma and chroma so isoluminant changes are
   not silently treated as static. */
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

/* Per-pixel motion state for the fine-grained mode:
   0 = static, 1 = new/weak change, 2 = strong or temporally confirmed change.
   Hysteresis is applied per sample, not to a large image block. */
static inline uint8_t vi_deint_update_motion_state(uint32_t delta,
                                                   uint8_t previous_state)
{
    uint32_t threshold = previous_state ? VI_MAD_EXIT_THRESHOLD
                                        : VI_MAD_ENTER_THRESHOLD;

    if (delta < threshold) {
        return 0;
    }
    return (previous_state != 0u || delta >= threshold * 2u) ? 2u : 1u;
}

/* Decide whether a missing output pixel is close enough to temporal motion
   to avoid Weave. The 3x2 support window follows the two current-field rows
   that border the missing line. A single strong or temporally confirmed
   change is sufficient; two new weak changes are required to reject isolated
   dither/noise. */
static inline bool vi_deint_local_motion(const uint8_t* upper_state,
                                         const uint8_t* lower_state,
                                         uint32_t x, uint32_t width)
{
    uint32_t x0;
    uint32_t x1;
    uint32_t changed = 0;

    if (!upper_state || !lower_state || width == 0u || x >= width) {
        return false;
    }

    x0 = x > 0u ? x - 1u : x;
    x1 = x + 1u < width ? x + 1u : x;

    for (uint32_t sample_x = x0; sample_x <= x1; ++sample_x) {
        uint8_t upper = upper_state[sample_x];
        uint8_t lower = lower_state[sample_x];

        if (upper >= 2u || lower >= 2u) {
            return true;
        }
        changed += (upper != 0u) ? 1u : 0u;
        changed += (lower != 0u) ? 1u : 0u;
    }

    return changed >= 2u;
}

static inline uint32_t vi_edi_clamp_x(int32_t x, uint32_t width)
{
    if (x < 0) {
        return 0;
    }
    if ((uint32_t)x >= width) {
        return width - 1u;
    }
    return (uint32_t)x;
}

/* Spatial color distance for ELA. Alpha is not part of edge direction. */
static inline uint32_t vi_edi_color_delta(const struct n64video_pixel* a,
                                          const struct n64video_pixel* b)
{
    uint32_t dr = vi_deint_abs_difference(a->r, b->r);
    uint32_t dg = vi_deint_abs_difference(a->g, b->g);
    uint32_t db = vi_deint_abs_difference(a->b, b->b);
    uint32_t delta = dr > dg ? dr : dg;
    return delta > db ? delta : db;
}

static inline uint32_t vi_edi_pair_cost(const struct n64video_pixel* upper,
                                        const struct n64video_pixel* lower,
                                        int32_t x, uint32_t width,
                                        int32_t offset)
{
    uint32_t upper_x = vi_edi_clamp_x(x + offset, width);
    uint32_t lower_x = vi_edi_clamp_x(x - offset, width);
    return vi_edi_color_delta(&upper[upper_x], &lower[lower_x]);
}

/* Edge-directed interpolation for one missing scanline pixel. Candidate
   diagonals are accepted only after a three-pixel support check. */
static inline void vi_edi_interpolate_pixel(struct n64video_pixel* destination,
                                            const struct n64video_pixel* upper,
                                            const struct n64video_pixel* lower,
                                            uint32_t x, uint32_t width)
{
    static const int32_t offsets[] = { 0, -1, 1, -2, 2 };
    uint32_t vertical_center;
    uint32_t best_center;
    uint32_t vertical_cost;
    uint32_t best_cost;
    int32_t best_offset = 0;
    size_t i;
    uint32_t upper_x;
    uint32_t lower_x;

    if (!destination || !upper || !lower || width == 0u || x >= width) {
        return;
    }

    vertical_center = vi_edi_pair_cost(upper, lower, (int32_t)x, width, 0);
    best_center = vertical_center;

    if (width >= 3u) {
        for (i = 1; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
            uint32_t cost = vi_edi_pair_cost(upper, lower, (int32_t)x,
                                             width, offsets[i]);
            if (cost < best_center) {
                best_center = cost;
                best_offset = offsets[i];
            }
        }
    }

    if (best_offset != 0) {
        vertical_cost = vertical_center +
            vi_edi_pair_cost(upper, lower, (int32_t)x - 1, width, 0) +
            vi_edi_pair_cost(upper, lower, (int32_t)x + 1, width, 0);
        best_cost = best_center +
            vi_edi_pair_cost(upper, lower, (int32_t)x - 1, width, best_offset) +
            vi_edi_pair_cost(upper, lower, (int32_t)x + 1, width, best_offset);

        if (best_cost <= VI_EDI_MAX_BEST_COST &&
            vertical_cost > best_cost + VI_EDI_MIN_COST_GAIN) {
            upper_x = vi_edi_clamp_x((int32_t)x + best_offset, width);
            lower_x = vi_edi_clamp_x((int32_t)x - best_offset, width);
        } else {
            upper_x = x;
            lower_x = x;
        }
    } else {
        upper_x = x;
        lower_x = x;
    }

    destination->r = (uint8_t)(((uint16_t)upper[upper_x].r + lower[lower_x].r + 1u) >> 1);
    destination->g = (uint8_t)(((uint16_t)upper[upper_x].g + lower[lower_x].g + 1u) >> 1);
    destination->b = (uint8_t)(((uint16_t)upper[upper_x].b + lower[lower_x].b + 1u) >> 1);
    destination->a = (uint8_t)(((uint16_t)upper[upper_x].a + lower[lower_x].a + 1u) >> 1);
}

#endif /* ANGRYLION_PLUS_VI_DEINTERLACE_H */
