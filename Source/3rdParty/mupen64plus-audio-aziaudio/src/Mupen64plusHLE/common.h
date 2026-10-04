/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* Mupen64plusHLE common.h - C-compatible definitions                        *
*                                                                           *
* License: GNU/GPLv2                                                        *
*                                                                           *
****************************************************************************/

#ifndef MUPEN64PLUS_HLE_COMMON_H
#define MUPEN64PLUS_HLE_COMMON_H

#include <stdint.h>
#include <string.h>

/* Boolean type */
typedef int Boolean;
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/* N64 type aliases */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef float    f32;
typedef double   f64;

typedef void(*p_func)(void);

/* Endianness swap macros */
#ifndef ENDIAN_M
#if defined(__BIG_ENDIAN__) || (__BYTE_ORDER != __LITTLE_ENDIAN)
#define ENDIAN_M    ( 0)
#else
#define ENDIAN_M    (~0)
#endif
#endif

#define ENDIAN_SWAP_BYTE    (ENDIAN_M & 0x7 & 3)
#define ENDIAN_SWAP_HALF    (ENDIAN_M & 0x6 & 2)
#define ENDIAN_SWAP_BIMI    (ENDIAN_M & 0x5 & 1)
#define ENDIAN_SWAP_WORD    (ENDIAN_M & 0x4 & 0)

#define BES(address)    ((address) ^ ENDIAN_SWAP_BYTE)
#define HES(address)    ((address) ^ ENDIAN_SWAP_HALF)
#define MES(address)    ((address) ^ ENDIAN_SWAP_BIMI)
#define WES(address)    ((address) ^ ENDIAN_SWAP_WORD)

/* AI register constants */
#define AI_STATUS_FIFO_FULL  0x80000000
#define AI_STATUS_DMA_BUSY   0x40000000
#define MI_INTR_AI           0x04
#define AI_CONTROL_DMA_ON    0x01
#define AI_CONTROL_DMA_OFF   0x00

#define SYSTEM_NTSC  0
#define SYSTEM_PAL   1
#define SYSTEM_MPAL  2

/* Audio command definitions */
#define NUM_ABI_COMMANDS 32
#define N 8
#define k0 GPR_k0

/* Export macros */
#define EXPORT __attribute__((visibility("default")))
#define CALL

/* Profiling macros - disabled */
#define StartProfile(profile)
#define EndProfile(profile)

/* Debug output */
#define DEBUG_OUTPUT(...)

/* Unreferenced parameter */
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(msg) (void)(msg)
#endif

/* Macro for unused variable warning suppression */
#ifdef __GNUC__
#  define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define UNUSED(x) UNUSED_ ## x
#endif

/* Plugin version string */
#define PLUGIN_NAME_STR "AziAudio-Plus"
#define PLUGIN_VERSION PLUGIN_NAME_STR

#endif
