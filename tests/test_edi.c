#include "../Source/3rdParty/mupen64plus-video-angrylion-plus/src/core/n64video/vi_deinterlace.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char* name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    } else {
        printf("PASS: %s\n", name);
    }
}

static void fill(struct n64video_pixel* pixels, uint32_t width, uint8_t value)
{
    for (uint32_t x = 0; x < width; ++x) {
        pixels[x].r = value;
        pixels[x].g = value;
        pixels[x].b = value;
        pixels[x].a = 255u;
    }
}

static void fill_rgb(struct n64video_pixel* pixels, uint32_t width,
                     uint8_t r, uint8_t g, uint8_t b)
{
    for (uint32_t x = 0; x < width; ++x) {
        pixels[x].r = r;
        pixels[x].g = g;
        pixels[x].b = b;
        pixels[x].a = 255u;
    }
}

static void test_edge_directed_interpolation(void)
{
    struct n64video_pixel upper[64];
    struct n64video_pixel lower[64];
    struct n64video_pixel output;

    fill(upper, 16u, 0u);
    fill(lower, 16u, 0u);
    for (uint32_t x = 6u; x < 16u; ++x) {
        upper[x].r = upper[x].g = upper[x].b = 255u;
    }
    for (uint32_t x = 4u; x < 16u; ++x) {
        lower[x].r = lower[x].g = lower[x].b = 255u;
    }
    vi_edi_interpolate_pixel(&output, upper, lower, 5u, 16u);
    check(output.r >= 250u && output.g >= 250u && output.b >= 250u,
          "left-to-right diagonal follows edge");

    fill(upper, 16u, 0u);
    fill(lower, 16u, 0u);
    for (uint32_t x = 4u; x < 16u; ++x) {
        upper[x].r = upper[x].g = upper[x].b = 255u;
    }
    for (uint32_t x = 6u; x < 16u; ++x) {
        lower[x].r = lower[x].g = lower[x].b = 255u;
    }
    vi_edi_interpolate_pixel(&output, upper, lower, 5u, 16u);
    check(output.r >= 250u && output.g >= 250u && output.b >= 250u,
          "right-to-left diagonal follows edge");

    fill(upper, 16u, 0u);
    fill(lower, 16u, 0u);
    upper[5].r = upper[5].g = upper[5].b = 255u;
    lower[5].r = lower[5].g = lower[5].b = 255u;
    vi_edi_interpolate_pixel(&output, upper, lower, 5u, 16u);
    check(output.r == 255u && output.g == 255u && output.b == 255u,
          "vertical edge remains unshifted");

    fill(upper, 16u, 20u);
    fill(lower, 16u, 40u);
    vi_edi_interpolate_pixel(&output, upper, lower, 5u, 16u);
    check(output.r == 30u && output.g == 30u && output.b == 30u,
          "flat region falls back to vertical Bob");

    fill(upper, 1u, 40u);
    fill(lower, 1u, 80u);
    vi_edi_interpolate_pixel(&output, upper, lower, 0u, 1u);
    check(output.r == 60u, "one-pixel-wide image uses vertical fallback");

    fill(upper, 16u, 10u);
    fill(lower, 16u, 30u);
    vi_edi_interpolate_pixel(&output, upper, lower, 0u, 16u);
    vi_edi_interpolate_pixel(&output, upper, lower, 15u, 16u);
    check(output.r == 20u, "frame-edge pixels clamp safely");

    fill_rgb(upper, 16u, 0u, 130u, 0u);
    fill_rgb(lower, 16u, 0u, 130u, 0u);
    for (uint32_t x = 6u; x < 16u; ++x) {
        upper[x].r = 255u;
        upper[x].g = 0u;
        upper[x].b = 0u;
    }
    for (uint32_t x = 4u; x < 16u; ++x) {
        lower[x].r = 255u;
        lower[x].g = 0u;
        lower[x].b = 0u;
    }
    vi_edi_interpolate_pixel(&output, upper, lower, 5u, 16u);
    check(output.r > 240u && output.g < 10u,
          "equal-luma chroma edge is preserved");

    /* Widths 1..64 exercise every valid output coordinate, including edges. */
    {
        uint32_t seed = 0x6D2B79F5u;
        int alpha_ok = 1;
        for (uint32_t width = 1u; width <= 64u; ++width) {
            for (uint32_t x = 0; x < width; ++x) {
                seed = seed * 1664525u + 1013904223u;
                upper[x].r = (uint8_t)(seed >> 24);
                upper[x].g = (uint8_t)(seed >> 16);
                upper[x].b = (uint8_t)(seed >> 8);
                upper[x].a = 255u;
                seed = seed * 1664525u + 1013904223u;
                lower[x].r = (uint8_t)(seed >> 24);
                lower[x].g = (uint8_t)(seed >> 16);
                lower[x].b = (uint8_t)(seed >> 8);
                lower[x].a = 255u;
            }
            for (uint32_t x = 0; x < width; ++x) {
                vi_edi_interpolate_pixel(&output, upper, lower, x, width);
                if (output.a != 255u) {
                    alpha_ok = 0;
                }
            }
        }
        check(alpha_ok, "randomized widths 1..64 remain bounds-safe");
    }

    /* The shared production helper rejects invalid empty/null requests. */
    output.r = 77u;
    vi_edi_interpolate_pixel(&output, upper, lower, 0u, 0u);
    check(output.r == 77u, "zero-width interpolation is a safe no-op");
    vi_edi_interpolate_pixel(NULL, upper, lower, 0u, 1u);
    check(vi_edi_clamp_x(0, 1u) == 0u && vi_edi_clamp_x(7, 1u) == 0u,
          "single-pixel clamping stays within bounds");
}

static void test_temporal_motion_helpers(void)
{
    struct n64video_pixel red = { 255u, 0u, 0u, 255u };
    struct n64video_pixel green = { 0u, 130u, 0u, 255u };
    struct n64video_pixel gray_a = { 100u, 100u, 100u, 255u };
    struct n64video_pixel gray_b = { 125u, 125u, 125u, 255u };

    check(vi_deint_pixel_delta(&red, &green) > VI_MAD_ENTER_THRESHOLD,
          "temporal delta detects isoluminant chroma changes");
    check(vi_deint_pixel_delta(&gray_a, &gray_b) >= 24u,
          "temporal delta detects luminance changes");

    check(vi_deint_update_motion_state(19u, 0u) == 0u,
          "motion enter threshold rejects sub-threshold noise");
    check(vi_deint_update_motion_state(20u, 0u) == 1u,
          "motion enter threshold detects ordinary change");
    check(vi_deint_update_motion_state(39u, 0u) == 1u,
          "ordinary change remains below strong threshold");
    check(vi_deint_update_motion_state(40u, 0u) == 2u,
          "strong isolated change gets high confidence");
    check(vi_deint_update_motion_state(11u, 1u) == 2u,
          "motion hysteresis confirms a persistent weak pixel change");
    check(vi_deint_update_motion_state(10u, 1u) == 0u,
          "motion hysteresis releases below exit threshold");
    check(vi_deint_update_motion_state(21u, 2u) == 2u,
          "strong motion state remains confirmed inside hysteresis band");
}

static void test_local_motion_support(void)
{
    uint8_t upper[7] = { 0u };
    uint8_t lower[7] = { 0u };

    upper[3] = 1u;
    check(!vi_deint_local_motion(upper, lower, 3u, 7u),
          "one weak changed sample is rejected as isolated noise");
    upper[3] = 2u;
    check(vi_deint_local_motion(upper, lower, 3u, 7u),
          "one strong or persistent changed sample protects a thin moving edge");
    upper[3] = 1u;
    lower[4] = 1u;
    check(vi_deint_local_motion(upper, lower, 3u, 7u),
          "two nearby ordinary changes trigger local motion");
    upper[3] = 0u;
    lower[4] = 0u;
    lower[6] = 2u;
    check(!vi_deint_local_motion(upper, lower, 3u, 7u),
          "motion outside the local support does not spread");
    check(!vi_deint_local_motion(upper, lower, 0u, 0u),
          "empty local window is safe");
    check(!vi_deint_local_motion(NULL, lower, 0u, 7u),
          "null motion row is safe");

    upper[0] = 1u;
    lower[0] = 1u;
    check(vi_deint_local_motion(upper, lower, 0u, 1u),
          "left/right image boundaries clamp the local window");
}

int main(void)
{
    test_edge_directed_interpolation();
    test_temporal_motion_helpers();
    test_local_motion_support();

    if (failures) {
        fprintf(stderr, "%d test(s) failed.\n", failures);
        return 1;
    }

    puts("All production VI deinterlacing helper tests passed.");
    return 0;
}
