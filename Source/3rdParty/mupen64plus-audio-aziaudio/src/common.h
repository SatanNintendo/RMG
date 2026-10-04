/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* http://www.apollo64.com/                                                  *
* Copyright (C) 2000-2026 Azimer. All rights reserved.                      *
* Mupen64Plus port (c) 2026 - AziAudio-Plus for Mupen64Plus contributors    *
*                                                                           *
* License:                                                                  *
* GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html                        *
*                                                                           *
****************************************************************************/

#ifndef _COMMON_DOT_H_
#define _COMMON_DOT_H_

#include <stddef.h> /* size_t */
#include <assert.h>
#include <string.h>
#include <stdint.h>

/* Mupen64Plus API headers (bundled with the plugin) */
#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_common.h"
#include "m64p_config.h"
#include "m64p_plugin.h"
#include "m64p_types.h"

/* ---------------------------------------------------------------------------
 * Debug output
 *
 * DebugMessage() routes messages to the Mupen64Plus core debug callback
 * (visible in the emulator's log / console window).  It is always safe to
 * call - when the callback is not set the message is dropped.
 * ------------------------------------------------------------------------ */
void DebugMessage(int level, const char *message, ...);

#ifdef _DEBUG
#define DEBUG_OUTPUT DebugMessage
#else
/* Keep zero-cost call sites even in release: DebugMessage checks the
 * callback itself and returns immediately when it is NULL. */
#define DEBUG_OUTPUT DebugMessage
#endif

/* ---------------------------------------------------------------------------
 * Base types (standard C types - portable across MSVC / MinGW / GCC)
 * ------------------------------------------------------------------------ */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef float    f32;
typedef double   f64;

typedef s8*  ps8;
typedef s16* ps16;
typedef s32* ps32;
typedef s64* ps64;
typedef u8*  pu8;
typedef u16* pu16;
typedef u32* pu32;
typedef u64* pu64;

typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef void(*p_func)(void);        /* ABI command handler            */

/* Boolean type */
typedef int Boolean;
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/* ---------------------------------------------------------------------------
 * Export macros
 *
 * On Windows m64p_types.h already defines EXPORT (__declspec(dllexport))
 * and CALL (__cdecl).  On other platforms provide equivalents.
 * ------------------------------------------------------------------------ */
#ifndef _WIN32
#undef  EXPORT
#define EXPORT __attribute__((visibility("default")))
#undef  CALL
#define CALL
#endif

/* Inline / NOINLINE */
#ifdef _MSC_VER
#define INLINE __forceinline
#define NOINLINE __declspec(noinline)
#else
#define INLINE inline
#define NOINLINE __attribute__((noinline))
#endif

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(msg) (void)(msg)
#endif

/* ---------------------------------------------------------------------------
 * Plugin identification
 * ------------------------------------------------------------------------ */
#define PLUGIN_NAME_STR     "AziAudio-Plus"
#define PLUGIN_RELEASE      " v0.71"
#define PLUGIN_BUILD        " WIP13 Mupen64Plus (1.0.19)"
#ifdef _DEBUG
#define PLUGIN_DEBUG_STR    " (Debug)"
#else
#define PLUGIN_DEBUG_STR    ""
#endif
#define PLUGIN_NAME         "Audio"
#define PLUGIN_VERSION      PLUGIN_NAME_STR PLUGIN_RELEASE PLUGIN_BUILD PLUGIN_DEBUG_STR
#define PLUGIN_VERSION_STR  PLUGIN_VERSION

/* Mupen64Plus plugin/API version numbers */
#define AZI_AUDIO_PLUGIN_VERSION  0x00710013   /* AziAudio-Plus WIP13 (M64P port, v1.0.19) */
#define AUDIO_PLUGIN_API_VERSION  0x020000     /* current mupen64plus audio API   */
#define CONFIG_API_VERSION        0x020100     /* current mupen64plus config API  */
#define CONFIG_PARAM_VERSION      1.00f

/* ---------------------------------------------------------------------------
 * Sound driver types
 * ------------------------------------------------------------------------ */
typedef enum SoundDriverType
{
    SND_DRIVER_NOSOUND    = 0x0000,

    /* Windows-only drivers */
    SND_DRIVER_DS8L       = 0x1000,   /* DirectSound 8, legacy path           */
    SND_DRIVER_DS8        = 0x1001,   /* DirectSound 8                        */
    SND_DRIVER_XA2L       = 0x1002,   /* XAudio 2.7 (legacy, Win7)            */
    SND_DRIVER_XA2        = 0x1003,   /* XAudio 2.8 via XAudio2Create         */
    SND_DRIVER_WASAPI     = 0x1004,   /* WASAPI shared mode (experimental)    */
    SND_DRIVER_WAVEOUT    = 0x1005,   /* classic waveOut                      */
    SND_DRIVER_XA2_MODERN = 0x1006,   /* XAudio2 xaudio2_9.dll / 2.8 (Win8+)  */

    /* Cross-platform drivers */
    SND_DRIVER_SDL2       = 0x2000
} SoundDriverType;

/* Driver string names used in the configuration file */
#define DRIVER_DEFAULT_STR  "default"
#define DRIVER_NOSOUND_STR  "nosound"
#define DRIVER_DS8L_STR     "ds8legacy"
#define DRIVER_DS8_STR      "ds8"
#define DRIVER_XA2L_STR     "xa2legacy"
#define DRIVER_XA2_STR      "xa2"
#define DRIVER_XA2M_STR     "xa2modern"
#define DRIVER_WASAPI_STR   "wasapi"
#define DRIVER_WAVEOUT_STR  "waveout"
#define DRIVER_SDL2_STR     "sdl2"

/* Windows backend enable flags - only active when compiling for Windows.
   The plugin keeps every backend of the original AziAudio-Plus WIP12 plus
   an SDL2 backend for non-Windows builds.  Guarded so build systems may
   also pass them on the command line. */
#if defined(_WIN32) || defined(__WIN32__)
#ifndef ENABLE_BACKEND_DIRECTSOUND8_LEGACY
#define ENABLE_BACKEND_DIRECTSOUND8_LEGACY
#endif
#ifndef ENABLE_BACKEND_XAUDIO2_LEGACY
#define ENABLE_BACKEND_XAUDIO2_LEGACY
#endif
#ifndef ENABLE_BACKEND_DIRECTSOUND8
#define ENABLE_BACKEND_DIRECTSOUND8
#endif
#ifndef ENABLE_BACKEND_XAUDIO2
#define ENABLE_BACKEND_XAUDIO2
#endif
#ifndef ENABLE_BACKEND_XAUDIO2_MODERN
#define ENABLE_BACKEND_XAUDIO2_MODERN
#endif
#ifndef ENABLE_BACKEND_WASAPI
#define ENABLE_BACKEND_WASAPI
#endif
#ifndef ENABLE_BACKEND_WAVEOUT
#define ENABLE_BACKEND_WAVEOUT
#endif
#endif

/* safe strcpy used by several translation units */
int safe_strcpy(char* dst, size_t limit, const char* src);

/* ---------------------------------------------------------------------------
 * Endian helper macros for indexing the emulated DMEM/BufferSpace arrays.
 * The emulator cores (both Project64 and Mupen64Plus) store RDRAM/DMEM
 * words in host byte order, so on a little-endian host the 8/16-bit
 * elements inside a 32-bit word are byte-swapped.  These macros convert
 * an emulated address into an index that reads the right element.
 * ------------------------------------------------------------------------ */
#ifndef ENDIAN_M
#if defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define ENDIAN_M    (0)
#else
#define ENDIAN_M    (~0)
#endif
#endif

#define ENDIAN_SWAP_BYTE    (ENDIAN_M & 0x7 & 3)
#define ENDIAN_SWAP_HALF    (ENDIAN_M & 0x6 & 2)
#define ENDIAN_SWAP_BIMI    (ENDIAN_M & 0x5 & 1)
#define ENDIAN_SWAP_WORD    (ENDIAN_M & 0x4 & 0)

#define BES(address)    ((address) ^ ENDIAN_SWAP_BYTE)   /*  8-bit access  */
#define HES(address)    ((address) ^ ENDIAN_SWAP_HALF)   /* 16-bit access  */
#define MES(address)    ((address) ^ ENDIAN_SWAP_BIMI)   /* mixed access   */
#define WES(address)    ((address) ^ ENDIAN_SWAP_WORD)   /* 32-bit access  */

#endif /* _COMMON_DOT_H_ */
