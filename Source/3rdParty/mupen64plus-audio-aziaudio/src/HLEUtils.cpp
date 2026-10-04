/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* HLE Utility Functions - SIMD/vector helpers used by the audio ABI code    *
*                                                                           *
* These functions were declared in audiohle.h but their implementations    *
* were left dead-stripped inside an "#if 0" block in HLEMain.cpp from the   *
* original zilmar-spec source. They are required by ABI_Envmixer,          *
* ABI_Adpcm, ABI_Resample, ABI_MixerInterleave, ABI_Filters and ABI3mp3,    *
* so without this file the plugin fails to link (or fails to load via      *
* dlopen at runtime due to unresolved symbols).                            *
*                                                                           *
* License: GNU/GPLv2                                                       *
*                                                                           *
****************************************************************************/
#include "audiohle.h"

#include <string.h>

/* Pack a 32-bit value into a saturated 16-bit signed value */
s16 pack_signed(s32 slice)
{
#ifdef SSE2_SUPPORT
    __m128i xmm;

    xmm = _mm_cvtsi32_si128(slice);
    xmm = _mm_packs_epi32(xmm, xmm);
    return (s16)_mm_cvtsi128_si32(xmm);
#else
    s32 result;

    result = slice;
    result = sats_under(result);
    result = sats_over(result);
    return (s16)(result & 0x0000FFFFul);
#endif
}

/* Saturate 8x32-bit values into 8x16-bit values */
void vsats128(s16* vd, s32* vs)
{
#ifdef SSE2_SUPPORT
    __m128i result, xmm_hi, xmm_lo;

    xmm_hi = _mm_loadu_si128((__m128i *)&vs[0]);
    xmm_lo = _mm_loadu_si128((__m128i *)&vs[4]);
    result = _mm_packs_epi32(xmm_hi, xmm_lo);
    _mm_storeu_si128((__m128i *)vd, result);
#else
    size_t i;

    for (i = 0; i < 8; i++)
        vd[i] = pack_signed(vs[i]);
#endif
}

/* Copy a 128-bit (8x16-bit) vector */
void copy_vector(void * vd, const void * vs)
{
#if defined(SSE2_SUPPORT)
    _mm_storeu_si128((__m128i *)vd, _mm_loadu_si128((__m128i *)vs));
#else
    memcpy(vd, vs, 8 * sizeof(i16));
#endif
}

/* Swap adjacent 16-bit elements within each 32-bit lane of a 128-bit vector */
void swap_elements(void * vd, const void * vs)
{
#ifdef SSE2_SUPPORT
    __m128i RSP_as_XMM;

    RSP_as_XMM = _mm_loadu_si128((__m128i *)vs);
    RSP_as_XMM = _mm_shufflehi_epi16(RSP_as_XMM, _MM_SHUFFLE(2, 3, 0, 1));
    RSP_as_XMM = _mm_shufflelo_epi16(RSP_as_XMM, _MM_SHUFFLE(2, 3, 0, 1));
    _mm_storeu_si128((__m128i *)vd, RSP_as_XMM);
#else
    i16 temp_vector[8];
    size_t i;

    for (i = 0; i < 8; i++)
        temp_vector[i] = ((i16 *)vs)[i ^ 1];
    copy_vector(vd, temp_vector);
#endif
}
