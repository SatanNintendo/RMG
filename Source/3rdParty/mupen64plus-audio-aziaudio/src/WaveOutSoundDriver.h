/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* WaveOutSoundDriver.h - WaveOut backend (event-driven audio thread)        *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#pragma once

#ifdef _WIN32

#include <windows.h>
#include <mmsystem.h>

#include <mutex>

#include <new>

#include "SoundDriver.h"
#include "PluginMutex.h"

class WaveOutSoundDriver :
    public SoundDriver
{
public:
    WaveOutSoundDriver();
    ~WaveOutSoundDriver();

    // Setup and Teardown Functions
    BOOL Initialize();
    void DeInitialize();
    void Setup();
    void Teardown();

    // Buffer Functions for the Audio Code
    void SetFrequency(u32 Frequency);           // Sets the Nintendo64 Game Audio Frequency

    // Management functions
    void StopAudio();                                                       // Stops the Audio PlayBack (as if paused)
    void StartAudio();                                                       // Starts the Audio PlayBack (as if unpaused)

    void SetVolume(u32 volume);

    static SoundDriverInterface* CreateSoundDriver() { return new (std::nothrow) WaveOutSoundDriver(); }
    static bool ValidateDriver();

protected:
    /* ---------------------------------------------------------------------------
     * Audio pipeline (v1.0.4 redesign)
     *
     * The original CALLBACK_FUNCTION design was rebuilt: the winmm
     * callback (a) called waveOutUnprepareHeader / waveOutPrepareHeader /
     * waveOutWrite from INSIDE the callback - forbidden by the winmm
     * contract and a deadlock hazard - and (b) called LoadAiBuffer()
     * while ALREADY holding the SoundDriver ring-buffer mutex; that
     * mutex is non-recursive, so the winmm callback thread froze
     * holding it, the emulator thread then blocked forever inside
     * AI_LenChanged/PushSamples and the frontend showed a black
     * screen with WaveOut.
     *
     * The device is now opened with CALLBACK_EVENT and a dedicated
     * audio thread of our own performs the unprepare / fill /
     * reprepare / requeue cycle.  This is legal (no wave API calls
     * from a winmm callback), deadlock-free (the thread takes only
     * m_devMutex and then the separate ring-buffer mutex inside
     * LoadAiBuffer) and teardown is deterministic (the thread is
     * JOINED before the device is closed or any buffer is freed -
     * no static m_Instance, no use-after-free window).
     * ------------------------------------------------------------------------ */
    static DWORD WINAPI AudioThreadProc(LPVOID lpParameter);
    void AudioLoop();

    HWAVEOUT       m_hWave;            /* opened by Setup(), closed by Teardown() */
    HANDLE         m_hEvent;           /* buffer-completion event (CALLBACK_EVENT) */
    HANDLE         m_hAudioThread;
    DWORD          m_dwAudioThreadId;
    volatile bool  m_bDone;            /* audio-thread shutdown flag */

    /* Serialises device / buffer-array access between the emulator
     * thread (Setup / Teardown / StartAudio / StopAudio / SetVolume)
     * and the audio thread.  Deliberately DISTINCT from the
     * SoundDriver ring-buffer mutex (which LoadAiBuffer acquires
     * internally) so the two can never be acquired twice on the
     * same thread. */
    PluginMutex     m_devMutex;

    int            m_numOutputBuffers;
    u32            m_OutputBuffersSize;
    WAVEHDR       *m_OutputBuffers;
    u8            *m_BufferMemory;
    u32            SampleRate;
};

#if !defined(_MSC_VER)
#undef __in
#undef __out
#endif

#endif // _WIN32
