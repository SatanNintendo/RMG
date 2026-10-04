/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* Sound Driver Interface                                                    *
*                                                                           *
* In the Mupen64Plus architecture the core owns the complete AI state       *
* machine (DMA FIFO, timing, interrupts).  A driver is therefore a plain    *
* audio sink: the plugin copies samples out of RDRAM immediately in         *
* AI_LenChanged() and pushes them into the driver's ring buffer; the        *
* backend pulls data on its own audio thread / callback.                    *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/
#pragma once

#include "common.h"
#include "Configuration.h"

class SoundDriverInterface
{
public:
    virtual ~SoundDriverInterface() {};

    /* Setup and teardown (called on the frontend/emulator thread) */
    virtual Boolean Initialize() = 0;
    virtual void DeInitialize() = 0;

    /* Management functions */
    virtual void StopAudio() = 0;
    virtual void StartAudio() = 0;
    virtual void SetFrequency(u32 Frequency) = 0;   /* N64 sample rate change */

    /* Audio-spec entry points implemented by the SoundDriver base class */
    virtual void AI_SetFrequency(u32 Frequency) = 0;
    virtual void AI_LenChanged(const u8 *start, u32 length) = 0;
    virtual void AI_Startup() = 0;
    virtual void AI_Shutdown() = 0;
    virtual void AI_ResetAudio() = 0;
    virtual void AI_SetSpeedFactor(u32 speed) = 0;

    /* Volume, 0..100, 100 = unity (Mupen64Plus Volume API semantics) */
    virtual void SetVolume(u32 volume) { UNREFERENCED_PARAMETER(volume); }
};
