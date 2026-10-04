/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* HLE Audio header - unchanged HLE processing core                          *
*                                                                           *
* License: GNU/GPLv2                                                        *
*                                                                           *
****************************************************************************/
#pragma once

#include "common.h"

/* Audio flags */
#define A_INIT     0x01
#define A_CONTINUE 0x00
#define A_LOOP     0x02
#define A_OUT      0x02
#define A_LEFT     0x02
#define A_RIGHT    0x00
#define A_VOL      0x04
#define A_RATE     0x00
#define A_AUX      0x08
#define A_NOAUX    0x00
#define A_MAIN     0x00
#define A_MIX      0x10

/* Number of ABI commands */
#define NUM_ABI_COMMANDS 32

/* Number of elements in SIMD vector */
#define N 8

/* Rename k0 to avoid GCC assembler conflicts */
#define k0 GPR_k0

/* Include AudioSpec for AUDIO_INFO */
#include "AudioSpec.h"

/* HLE globals */
extern u32 t9, k0;

extern u8 * DMEM;
extern u8 * IMEM;
extern u8 * DRAM;

extern u32 UCData, UDataLen;

/* ABI Functions */
void ADDMIXER();
void ADPCM(); void ADPCM2(); void ADPCM3();
void CLEARBUFF(); void CLEARBUFF2(); void CLEARBUFF3();
void DMEMMOVE(); void DMEMMOVE2(); void DMEMMOVE3();
void DUPLICATE2();
void ENVMIXER(); void ENVMIXER2(); void ENVMIXER3(); void ENVMIXER_GE();
void ENVSETUP1(); void ENVSETUP2();
void FILTER2();
void HILOGAIN();
void INTERL2();
void INTERLEAVE(); void INTERLEAVE2(); void INTERLEAVE3();
void LOADADPCM(); void LOADADPCM2(); void LOADADPCM3();
void LOADBUFF(); void LOADBUFF2(); void LOADBUFF3();
void MIXER(); void MIXER2(); void MIXER3();
void MP3();
void MP3ADDY();
void POLEF();
void RESAMPLE(); void RESAMPLE2(); void RESAMPLE3();
void SAVEBUFF(); void SAVEBUFF2(); void SAVEBUFF3();
void SEGMENT(); void SEGMENT2();
void SETBUFF(); void SETBUFF2();
void SETLOOP(); void SETLOOP2(); void SETLOOP3();
void SETVOL(); void SETVOL3();
void SPNOOP();
void UNKNOWN();

/* Buffer Space */
extern u8 BufferSpace[0x10000];
extern short hleMixerWorkArea[256];
extern u32 SEGMENTS[0x10];
extern u16 AudioInBuffer, AudioOutBuffer, AudioCount;
extern u16 AudioAuxA, AudioAuxC, AudioAuxE;
extern u32 loopval;
extern bool isMKABI;
extern bool isZeldaABI;

/* Accumulator */
extern s32 acc[32][N];
extern s16 acc_clamped[N];

/* SSE2 support detection */
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define SSE2_SUPPORT
#endif

/* Saturation functions */
INLINE s32 sats_over(s32 slice) {
    if (slice > +32767) return +32767;
    return slice;
}

INLINE s32 sats_under(s32 slice) {
    if (slice < -32768) return -32768;
    return slice;
}

s16 pack_signed(s32 slice);
void vsats128(s16* vd, s32* vs);

/* Vector operations */
void copy_vector(void * vd, const void * vs);
void swap_elements(void * vd, const void * vs);
