/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* NoSound Driver - silent fallback driver                                   *
*                                                                           *
* Consumes the audio stream at real time on a background thread so that    *
* the A/V sync throttle keeps working (the emulation stays at 100% speed)  *
* even when no sound device is available.                                   *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/
#pragma once

#include <new>

#include "common.h"
#include "SoundDriver.h"

class NoSoundDriver :
    public SoundDriver
{
public:
    NoSoundDriver();
    ~NoSoundDriver();

    /* Setup and Teardown */
    Boolean Initialize();
    void DeInitialize();

    /* Management functions */
    void StopAudio();
    void StartAudio();
    void SetFrequency(u32 Frequency);

    /* Factory method */
    static SoundDriverInterface* CreateSoundDriver() { return new (std::nothrow) NoSoundDriver(); }
    static bool ValidateDriver();

protected:
    void ThreadProc();

    bool m_Running;
    u32  m_LastConsumeTick;

#ifdef _WIN32
    HANDLE m_Thread;
#else
    pthread_t m_Thread;
    bool m_ThreadStarted;
#endif
};
