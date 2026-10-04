/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* WASAPISoundDriver - WASAPI shared-mode backend (experimental)             *
*                                                                           *
* Pull model: the WASAPI render thread pulls float samples from the        *
* SoundDriver ring buffer through LoadAiBufferResample() (linear           *
* resampling from the N64 rate to the device mix rate).                    *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#pragma once

#ifdef _WIN32

#include "windows.h"

#include <new>

#include "SoundDriver.h"

class WASAPISoundDriver :
    public SoundDriver
{
public:
    WASAPISoundDriver();
    ~WASAPISoundDriver();

    /* Setup and Teardown Functions */
    BOOL Initialize();
    void DeInitialize();

    /* Buffer Functions for the Audio Code */
    void SetFrequency(u32 Frequency);           /* N64 sample rate      */

    /* Management functions */
    void StopAudio();
    void StartAudio();

    void SetVolume(u32 volume);

    static SoundDriverInterface* CreateSoundDriver() { return new (std::nothrow) WASAPISoundDriver(); }

    static bool ValidateDriver();

protected:
    static DWORD WINAPI AudioThreadProc(LPVOID lpParameter);

    bool bInitialized;
    bool bStopAudioThread;

private:
    HANDLE hAudioThread;
};

#if !defined(_MSC_VER)
#undef __in
#undef __out
#endif

#endif // _WIN32
