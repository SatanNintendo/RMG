/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* DirectSoundDriver.cpp - DirectSound 8 backend implementation              *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#include "common.h"
#if defined(ENABLE_BACKEND_DIRECTSOUND8)
#include <string.h>
#include <math.h>
#include "DirectSoundDriver.h"
#include "AudioSpec.h"
#include "Configuration.h"
#include "SoundDriverRegistrar.h"

REGISTER_DRIVER(DirectSoundDriver, SND_DRIVER_DS8, "DirectSound 8 Driver", 6)

/* No file-scope audio state: every cursor lives in the class (see the
 * LOCK_SIZE shadowing bug note in DirectSoundDriver.h). */

bool DirectSoundDriver::ValidateDriver()
{
    bool retVal = false;
    const GUID CLSID_DirectSound8_Test = { 0x3901cc3f, 0x84b5, 0x4fa4, 0xba, 0x35, 0xaa, 0x81, 0x72, 0xb8, 0xa0, 0x9b };
    const GUID IID_IDirectSound8_Test = { 0xC50A7E93, 0xF395, 0x4834, 0x9E, 0xF6, 0x7F, 0xA9, 0x9D, 0xE5, 0x09, 0x66 };

    /* Validate a DirectSound8 object will initialize.
       COM BALANCE FIX (same as WASAPI): this probe runs on the
       frontend thread at PluginStartup.  CoInitializeEx(MT) fails
       with RPC_E_CHANGED_MODE on Qt hosts (STA already initialised);
       the old unconditional CoUninitialize() then destroyed the
       host's apartment.  Only unbalance what we actually
       initialised. */
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IUnknown* obj;
    HRESULT hr = CoCreateInstance(CLSID_DirectSound8_Test,
        NULL, CLSCTX_INPROC_SERVER, IID_IDirectSound8_Test, (void**)&obj);
    if (SUCCEEDED(hr))
    {
        obj->Release();
        retVal = true;
    }
    if (SUCCEEDED(hrInit))
        CoUninitialize();
    return retVal;
}

DirectSoundDriver::DirectSoundDriver()
{
    lpdsbuf = NULL;
    lpdsb = NULL;
    lpds = NULL;
    audioIsDone = false;
    handleAudioThread = NULL;
    dwAudioThreadId = 0;
    audioIsPlaying = FALSE;
    SampleRate = 0;
    SegmentSize = 0;
    m_dwLockSize = 0;
    m_dwLastPos = 0;
    m_dwWritePos = 0;
    m_dwPlayPos = 0;
    m_dwNextPos = 0;
    m_lpvLock1 = NULL;
    m_lpvLock2 = NULL;
    m_dwLockBytes1 = 0;
    m_dwLockBytes2 = 0;
}

/* ---------------------------------------------------------------------------
 * Audio thread - keeps the DirectSound circular buffer fed
 *
 * NOTE (bug fix): this function previously used file-scope statics for
 * every cursor; the segment size macro resolved to a never-written
 * static (0) here, the compiler folded the write-position computation
 * to a constant 0 and the thread never wrote a single byte (silence).
 * All state now comes from the driver instance (ac->...).
 * ------------------------------------------------------------------------ */
DWORD WINAPI AudioThreadProc(DirectSoundDriver *ac) {

    while (ac->lpdsbuf == NULL)
    {
        Sleep(1);
        if (ac->audioIsDone == true)
            return 0;
    }
    DebugMessage(M64MSG_VERBOSE, "DS8: Audio Thread Started...");

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while (ac->audioIsDone == false) {  /* While the thread is still alive */
        while (ac->m_dwLastPos == ac->m_dwWritePos) {  /* Cycle until a new buffer position is available */
            if (ac->audioIsDone == true)
                return 0;
            if (ac->lpdsbuf == NULL)
                return ~0u;

            if ((Configuration::getDisallowSleepDS8() == false) || (ac->m_dwWritePos == 0))
                Sleep(1);

            {
                PluginLockGuard lck(ac->dsMutex);
                if FAILED(ac->lpdsbuf->GetCurrentPosition(&ac->m_dwPlayPos, NULL)) {
                    DebugMessage(M64MSG_ERROR, "DS8: Error getting audio position...");
                    return 0;
                }
            }

            if (ac->m_dwPlayPos < ac->m_dwLockSize)
                ac->m_dwWritePos = (ac->m_dwLockSize * DS_SEGMENTS) - ac->m_dwLockSize;
            else
                ac->m_dwWritePos = ((ac->m_dwPlayPos / ac->m_dwLockSize) * ac->m_dwLockSize) - ac->m_dwLockSize;
        }

        /* A segment was skipped - diagnostics only */
        if (ac->m_dwNextPos != ac->m_dwWritePos) {
            DebugMessage(M64MSG_VERBOSE, "DS8: skipped segment");
        }

        ac->m_dwLastPos = ac->m_dwWritePos;

        /* Set our next anticipated segment */
        ac->m_dwNextPos = ac->m_dwWritePos + ac->m_dwLockSize;
        if (ac->m_dwNextPos >= (ac->m_dwLockSize*DS_SEGMENTS)) {
            ac->m_dwNextPos -= (ac->m_dwLockSize*DS_SEGMENTS);
        }
        if (ac->audioIsDone == true) break;

        /* Time to write out to the buffer */
        {
            PluginLockGuard lck(ac->dsMutex);
            if (DS_OK != ac->lpdsbuf->Lock(ac->m_dwWritePos, ac->m_dwLockSize, &ac->m_lpvLock1, &ac->m_dwLockBytes1, &ac->m_lpvLock2, &ac->m_dwLockBytes2, 0)) {
                DebugMessage(M64MSG_ERROR, "DS8: Error locking sound buffer");
                return 0;
            }
            /* Fill the locked region from the ring buffer.
               LoadAiBuffer locks the SoundDriver ring-buffer
               mutex internally - dsMutex is a different mutex,
               so this can never self-deadlock. */
            ac->LoadAiBuffer((BYTE *)ac->m_lpvLock1, ac->m_dwLockBytes1);
            if (ac->m_dwLockBytes2) ac->LoadAiBuffer((BYTE *)ac->m_lpvLock2, ac->m_dwLockBytes2);
            if FAILED(ac->lpdsbuf->Unlock(ac->m_lpvLock1, ac->m_dwLockBytes1, ac->m_lpvLock2, ac->m_dwLockBytes2)) {
                DebugMessage(M64MSG_ERROR, "DS8: Error unlocking sound buffer");
                return 0;
            }
        }
    }

    DebugMessage(M64MSG_VERBOSE, "DS8: Audio Thread Terminated...");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Setup and Teardown Functions
 * ------------------------------------------------------------------------ */

/* Creates the streaming buffer with the given segment size */
void DirectSoundDriver::SetSegmentSize(DWORD length) {

    DSBUFFERDESC        dsbdesc;
    WAVEFORMATEX        wfm;
    HRESULT             hr;

    if (SampleRate == 0) { return; }
    SegmentSize = length;

    {
        PluginLockGuard lck(dsMutex);

        /* Release a buffer from a previous frequency change:
           overwriting lpdsbuf without releasing it leaked the
           interface (one leak per DAC-rate change). */
        if (lpdsbuf != NULL) {
            IDirectSoundBuffer_Stop(lpdsbuf);
            IDirectSoundBuffer_Release(lpdsbuf);
            lpdsbuf = NULL;
        }

        memset(&wfm, 0, sizeof(WAVEFORMATEX));

        wfm.wFormatTag = WAVE_FORMAT_PCM;
        wfm.nChannels = 2;
        wfm.nSamplesPerSec = SampleRate;
        wfm.wBitsPerSample = 16;
        wfm.nBlockAlign = wfm.wBitsPerSample / 8 * wfm.nChannels;
        wfm.nAvgBytesPerSec = wfm.nSamplesPerSec * wfm.nBlockAlign;

        memset(&dsbdesc, 0, sizeof(DSBUFFERDESC));
        dsbdesc.dwSize = sizeof(DSBUFFERDESC);
        dsbdesc.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCSOFTWARE;
        dsbdesc.dwBufferBytes = SegmentSize * DS_SEGMENTS;
        dsbdesc.lpwfxFormat = &wfm;

        hr = IDirectSound_CreateSoundBuffer(lpds, &dsbdesc, &lpdsbuf, NULL);
        if (FAILED(hr))
        {
            DebugMessage(M64MSG_ERROR, "DS8: CreateSoundBuffer failed (0x%08lX)", (unsigned long)hr);
            return;
        }

        IDirectSoundBuffer_Play(lpdsbuf, 0, 0, DSBPLAY_LOOPING);

        /* Fresh cursor state for the new buffer (the audio thread
           starts right after this in SetFrequency). */
        m_dwLastPos = 0;
        m_dwWritePos = 0;
        m_dwPlayPos = 0;
        m_dwNextPos = 0;
        m_lpvLock1 = NULL;
        m_lpvLock2 = NULL;
        m_dwLockBytes1 = 0;
        m_dwLockBytes2 = 0;
    }
}

BOOL DirectSoundDriver::Initialize() {

    DSBUFFERDESC        dsPrimaryBuff;
    WAVEFORMATEX        wfm;
    HRESULT             hr;

    DeInitialize(); /* Release just in case... */
    SampleRate = 0;

    DebugMessage(M64MSG_VERBOSE, "DS8: Initialize()");

    {
        PluginLockGuard lck(dsMutex);

        hr = DirectSoundCreate8(NULL, &lpds, NULL);
        if (FAILED(hr))
        {
            DebugMessage(M64MSG_ERROR, "DS8: Unable to DirectSoundCreate (no sound device?)");
            return FALSE;
        }

        /* The Mupen64Plus AUDIO_INFO has no window handle (unlike
           the zilmar spec), so use the desktop window. */
        if (FAILED(hr = IDirectSound_SetCooperativeLevel(lpds, GetDesktopWindow(), DSSCL_PRIORITY))) {
            DebugMessage(M64MSG_ERROR, "DS8: Failed to SetCooperativeLevel");
            IDirectSound_Release(lpds);
            lpds = NULL;
            return FALSE;
        }

        if (lpdsbuf) {
            IDirectSoundBuffer_Release(lpdsbuf);
            lpdsbuf = NULL;
        }
        memset(&dsPrimaryBuff, 0, sizeof(DSBUFFERDESC));

        dsPrimaryBuff.dwSize = sizeof(DSBUFFERDESC);
        dsPrimaryBuff.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME;
        dsPrimaryBuff.dwBufferBytes = 0;
        dsPrimaryBuff.lpwfxFormat = NULL;
        memset(&wfm, 0, sizeof(WAVEFORMATEX));

        wfm.wFormatTag = WAVE_FORMAT_PCM;
        wfm.nChannels = 2;
        wfm.nSamplesPerSec = 44100;
        wfm.wBitsPerSample = 16;
        wfm.nBlockAlign = wfm.wBitsPerSample / 8 * wfm.nChannels;
        wfm.nAvgBytesPerSec = wfm.nSamplesPerSec * wfm.nBlockAlign;

        hr = IDirectSound_CreateSoundBuffer(lpds, &dsPrimaryBuff, &lpdsb, NULL);

        if (SUCCEEDED(hr)) {
            IDirectSoundBuffer_SetFormat(lpdsb, &wfm);
            IDirectSoundBuffer_Play(lpdsb, 0, 0, DSBPLAY_LOOPING);
        }
    }

    if (SampleRate > 0)
        SetFrequency(SampleRate);

    DebugMessage(M64MSG_VERBOSE, "DS8: Init Success...");

    return TRUE;
}

void DirectSoundDriver::DeInitialize() {

    DebugMessage(M64MSG_VERBOSE, "DS8: DeInitialize()");
    /* StopAudio() joins the audio thread first (if running). */
    StopAudio();
    {
        PluginLockGuard lck(dsMutex);
        if (lpdsbuf) {
            IDirectSoundBuffer_Stop(lpdsbuf);
            IDirectSoundBuffer_Release(lpdsbuf);
            lpdsbuf = NULL;
        }
        if (lpdsb) {
            IDirectSoundBuffer_Stop(lpdsb);
            IDirectSoundBuffer_Release(lpdsb);
            lpdsb = NULL;
        }
        if (lpds) {
            IDirectSound_Release(lpds);
            lpds = NULL;
        }
    }

    audioIsDone = false;
    audioIsPlaying = FALSE;
    m_dwLastPos = 0;
    m_dwWritePos = 0;
    m_dwPlayPos = 0;
    m_dwNextPos = 0;
    DebugMessage(M64MSG_VERBOSE, "DS8: DeInitialize() complete");
}

/* ---------------------------------------------------------------------------
 * Buffer Functions for the Audio Code
 * ------------------------------------------------------------------------ */
void DirectSoundDriver::SetFrequency(u32 Frequency2) {

    DWORD Frequency = Frequency2;

    DebugMessage(M64MSG_INFO, "DS8: SetFrequency(%lu)", (unsigned long)Frequency);
    StopAudio();

    /* Segment size: one backend frame worth of stereo 16-bit data.
       BackendFPS is clamped to 15..120 at load time; guard anyway. */
    DWORD fps = Configuration::getBackendFPS();
    if (fps == 0) fps = 60;
    m_dwLockSize = (Frequency / fps) * 4;
    if (m_dwLockSize < 16) m_dwLockSize = 16;
    SampleRate = Frequency;
    SegmentSize = 0;  /* re-created by SetSegmentSize */

    SetSegmentSize(m_dwLockSize);
    DebugMessage(M64MSG_VERBOSE, "DS8: Frequency: %lu - SegmentSize: %lu", (unsigned long)Frequency, (unsigned long)SegmentSize);

    StartAudio();
    DebugMessage(M64MSG_VERBOSE, "DS8: SetFrequency() Complete");
}

/* ---------------------------------------------------------------------------
 * Management functions
 *
 * StopAudio: signal the thread via audioIsDone, then JOIN it properly
 * with WaitForSingleObject.  The old code polled a flag the thread
 * never clears, always burned 100 ms, then called TerminateThread on
 * the (usually already exited) thread and leaked the handle.  Worse,
 * TerminateThread landing while the thread held dsMutex would have
 * deadlocked the emulator thread forever.  The thread checks
 * audioIsDone inside its inner wait loop too, so it exits within a
 * couple of milliseconds.
 * ------------------------------------------------------------------------ */
void DirectSoundDriver::StopAudio() {
    if (!audioIsPlaying && handleAudioThread == NULL) return;
    DebugMessage(M64MSG_VERBOSE, "DS8: StopAudio()");
    audioIsDone = true;

    if (lpdsbuf != NULL)
    {
        PluginLockGuard lck(dsMutex);
        if (lpdsbuf != NULL)
            lpdsbuf->Stop();
    }

    if (this->handleAudioThread != NULL)
    {
        if (WaitForSingleObject(this->handleAudioThread, 2000) != WAIT_OBJECT_0)
        {
            DebugMessage(M64MSG_WARNING, "DS8: Unsafe Thread Termination");
            TerminateThread(this->handleAudioThread, 0);
        }
        CloseHandle(this->handleAudioThread);
        this->handleAudioThread = NULL;
    }
    audioIsPlaying = FALSE;
    DebugMessage(M64MSG_VERBOSE, "DS8: StopAudio() complete");
}

void DirectSoundDriver::StartAudio() {
    if (audioIsPlaying) return;
    DebugMessage(M64MSG_VERBOSE, "DS8: StartAudio()");
    audioIsPlaying = TRUE;
    audioIsDone = false;
    if (this->handleAudioThread != NULL)
    {
        DebugMessage(M64MSG_WARNING, "DS8: audio thread handle already exists");
    }
    else
    {
        this->handleAudioThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AudioThreadProc, this, 0, &this->dwAudioThreadId);
    }
    {
        PluginLockGuard lck(dsMutex);
        if (lpdsbuf != NULL)
        {
            IDirectSoundBuffer_Play(lpdsbuf, 0, 0, DSBPLAY_LOOPING);
        }
    }
    DebugMessage(M64MSG_VERBOSE, "DS8: StartAudio() Complete");
}

/* Volume: 0..100, 100 = unity.
 *
 * VOLUME 1.0.13 fix: DSBVOLUME is an attenuation in hundredths of a decibel
 * (0 = full scale, -10000 = silence) - a LOGARITHMIC scale.  The previous
 * code mapped the 0..100 percentage linearly into centi-dB
 * ((vol/100 - 1) * 10000), which makes the amplitude fall off
 * exponentially: 80% produced -20 dB (1/10 of the original plugin's
 * amplitude), 50% produced -50 dB (1/316).  That is why this backend
 * sounded drastically quieter than the original AziAudio-Plus (whose
 * default volume equals DSBVOLUME_MAX = 0 dB) and quieter than the
 * linear backends of this port at the same slider value.
 *
 * Convert the percentage to the same amplitude the linear backends
 * produce: amplitude = vol/100 -> dB = 20*log10(vol/100) ->
 * centi-dB = 2000*log10(vol/100).  100% = 0 dB (unity), 80% = -1.9 dB,
 * 50% = -6.0 dB - identical loudness to XAudio2/WASAPI/waveOut at the
 * same setting. */
void DirectSoundDriver::SetVolume(u32 volume) {
    SoundDriver::SetVolume(volume);

    LONG dsVolume;
    if (GetVolume() >= 100)
        dsVolume = 0;                                          /* unity: 0 dB   */
    else if (GetVolume() == 0)
        dsVolume = -10000;                                     /* silence       */
    else
        dsVolume = (LONG)(2000.0f * log10f((float)GetVolume() / 100.0f));
    if (dsVolume < -10000) dsVolume = -10000;
    if (dsVolume > 0) dsVolume = 0;
    if (lpdsb != NULL) lpdsb->SetVolume(dsVolume);
}

#endif
