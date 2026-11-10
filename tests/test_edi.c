#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
struct n64video_pixel { uint8_t r,g,b,a; };
#define VI_EDI_MIN_COST_GAIN 18u
#define VI_EDI_MAX_BEST_COST 96u
static uint32_t vi_edi_clamp_x(int32_t x, uint32_t width)
{
    if (x < 0) {
        return 0;
    }
    if ((uint32_t)x >= width) {
        return width - 1u;
    }
    return (uint32_t)x;
}

/* Spatial color distance for ELA. The maximum RGB-channel difference
   preserves isoluminant color edges without the luma multiplications used by
   the temporal motion detector. Alpha is not part of the VI edge direction. */
static uint32_t vi_edi_color_delta(const struct n64video_pixel* a,
                                   const struct n64video_pixel* b)
{
    uint32_t dr = a->r > b->r ? a->r - b->r : b->r - a->r;
    uint32_t dg = a->g > b->g ? a->g - b->g : b->g - a->g;
    uint32_t db = a->b > b->b ? a->b - b->b : b->b - a->b;
    uint32_t delta = dr > dg ? dr : dg;
    return delta > db ? delta : db;
}

/* Compare the two current-field lines at one candidate diagonal. The
   caller clamps the logical sample coordinate, so the helper can also be
   used for the two support samples next to the output pixel. */
static uint32_t vi_edi_pair_cost(const struct n64video_pixel* upper,
                                 const struct n64video_pixel* lower,
                                 int32_t x, uint32_t width, int32_t offset)
{
    uint32_t upper_x = vi_edi_clamp_x(x + offset, width);
    uint32_t lower_x = vi_edi_clamp_x(x - offset, width);
    return vi_edi_color_delta(&upper[upper_x], &lower[lower_x]);
}

/* Edge-directed interpolation for one missing scanline pixel. First, only
   the central sample is tested against the four candidate diagonals. The
   winner is then validated over a 3-sample support window against vertical
   Bob. This keeps the decision conservative while avoiding 15 full pixel
   comparisons on every moving pixel. */
static void vi_edi_interpolate_pixel(struct n64video_pixel* destination,
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
        /* Validate both the vertical and selected diagonal over x-1..x+1. */
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

static int failures = 0;
static void check(int ok, const char* name) { if (!ok) { fprintf(stderr, "FAIL: %s\n", name); ++failures; } else { printf("PASS: %s\n", name); } }
static void fill(struct n64video_pixel* p, unsigned w, unsigned v) { for (unsigned i=0;i<w;i++) p[i]=(struct n64video_pixel){(uint8_t)v,(uint8_t)v,(uint8_t)v,255}; }
static void fill_rgb(struct n64video_pixel* p, unsigned w, uint8_t r, uint8_t g, uint8_t b) { for (unsigned i=0;i<w;i++) p[i]=(struct n64video_pixel){r,g,b,255}; }
int main(void) {
    struct n64video_pixel up[16], lo[16], out;
    /* One-pixel diagonal whose correct missing-line center is x=5. */
    fill(up,16,0); fill(lo,16,0); for (unsigned i=6;i<16;i++) up[i].r=up[i].g=up[i].b=255; for (unsigned i=4;i<16;i++) lo[i].r=lo[i].g=lo[i].b=255;
    vi_edi_interpolate_pixel(&out,up,lo,5,16);
    check(out.r>=250 && out.g>=250 && out.b>=250, "left-to-right diagonal follows edge");
    /* Opposite diagonal. */
    fill(up,16,0); fill(lo,16,0); for (unsigned i=4;i<16;i++) up[i].r=up[i].g=up[i].b=255; for (unsigned i=6;i<16;i++) lo[i].r=lo[i].g=lo[i].b=255;
    vi_edi_interpolate_pixel(&out,up,lo,5,16);
    check(out.r>=250 && out.g>=250 && out.b>=250, "right-to-left diagonal follows edge");
    /* Vertical alignment must not be shifted. */
    fill(up,16,0); fill(lo,16,0); up[5].r=up[5].g=up[5].b=255; lo[5].r=lo[5].g=lo[5].b=255;
    vi_edi_interpolate_pixel(&out,up,lo,5,16);
    check(out.r==255 && out.g==255 && out.b==255, "vertical edge remains unshifted");
    /* Flat values should fall back to exact vertical Bob. */
    fill(up,16,20); fill(lo,16,40);
    vi_edi_interpolate_pixel(&out,up,lo,5,16);
    check(out.r==30 && out.g==30 && out.b==30, "flat region falls back to vertical Bob");
    /* Narrow image and first/last pixel are bounds-safe. */
    fill(up,1,40); fill(lo,1,80);
    vi_edi_interpolate_pixel(&out,up,lo,0,1);
    check(out.r==60, "one-pixel-wide image uses vertical fallback");
    fill(up,16,10); fill(lo,16,30);
    vi_edi_interpolate_pixel(&out,up,lo,0,16);
    vi_edi_interpolate_pixel(&out,up,lo,15,16);
    check(out.r==20, "frame-edge pixels clamp safely");
    /* Equal-luma red/green transition: the chroma component must guide ELA. */
    fill_rgb(up,16,0,130,0); fill_rgb(lo,16,0,130,0);
    for (unsigned i=6;i<16;i++) up[i]=(struct n64video_pixel){255,0,0,255};
    for (unsigned i=4;i<16;i++) lo[i]=(struct n64video_pixel){255,0,0,255};
    vi_edi_interpolate_pixel(&out,up,lo,5,16);
    check(out.r>240 && out.g<10, "equal-luma chroma edge is preserved");

    /* Exercise every visible x on widths 1..64 with deterministic pseudo-random rows. */
    {
        uint32_t seed=0x6D2B79F5u;
        struct n64video_pixel random_up[64], random_lo[64];
        int bounds_ok=1;
        for (unsigned w=1;w<=64;w++) {
            for (unsigned x=0;x<w;x++) {
                seed=seed*1664525u+1013904223u;
                random_up[x]=(struct n64video_pixel){(uint8_t)(seed>>24),(uint8_t)(seed>>16),(uint8_t)(seed>>8),255};
                seed=seed*1664525u+1013904223u;
                random_lo[x]=(struct n64video_pixel){(uint8_t)(seed>>24),(uint8_t)(seed>>16),(uint8_t)(seed>>8),255};
            }
            for (unsigned x=0;x<w;x++) {
                vi_edi_interpolate_pixel(&out,random_up,random_lo,x,w);
                if (out.a != 255u) bounds_ok=0;
            }
        }
        check(bounds_ok, "randomized widths 1..64 remain bounds-safe");
    }
    return failures ? 1 : 0;
}
