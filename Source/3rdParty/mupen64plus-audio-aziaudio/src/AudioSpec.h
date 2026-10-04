/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* Audio Spec - bridge between the Mupen64Plus AUDIO_INFO and the plugin     *
*                                                                           *
* The system type constants (SYSTEM_NTSC / SYSTEM_PAL / SYSTEM_MPAL) come   *
* from m64p_types.h (enum m64p_system_type).  The AUDIO_INFO structure is   *
* defined in m64p_plugin.h and is filled in by the Mupen64Plus core when   *
* it calls InitiateAudio().                                                 *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/
#ifndef _AUDIO_H_INCLUDED__
#define _AUDIO_H_INCLUDED__

#include "common.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Global audio info - stored by InitiateAudio (called by the core) */
extern AUDIO_INFO AudioInfo;

/* HLE Audio entry point (called by the RSP plugin when the core forwards
 * audio task lists to the audio plugin - requires rsp-hle configured with
 * AudioListToAudioPlugin = True). */
void HLEStart(void);

#if defined(__cplusplus)
}
#endif

#endif /* _AUDIO_H_INCLUDED__ */
