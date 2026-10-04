/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* WaveOutSoundDriver.cpp - WaveOut backend (event-driven audio thread)      *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#include "common.h"
#if defined(ENABLE_BACKEND_WAVEOUT)
#include "WaveOutSoundDriver.h"
#include "AudioSpec.h"
#include <string.h>
#include "SoundDriverRegistrar.h"

REGISTER_DRIVER(WaveOutSoundDriver, SND_DRIVER_WAVEOUT, "WaveOut Driver", 1)

/*
    Will verify the driver can run in the configured environment
*/
bool WaveOutSoundDriver::ValidateDriver()
{
    // This should be available in all versions of Windows in the last 25 years
    WAVEOUTCAPS caps;
    memset(&caps, 0, sizeof(WAVEOUTCAPS));
    if (waveOutGetDevCaps(WAVE_MAPPER, &caps, sizeof(WAVEOUTCAPS)) != MMSYSERR_NOERROR)
        return false;
    if (caps.dwFormats & WAVE_FORMAT_4S16)
        return true;
    else
        return false;
}

WaveOutSoundDriver::WaveOutSoundDriver()
{
    DebugMessage(M64MSG_VERBOSE, "WO Constructor");
    m_hWave = NULL;
    m_hEvent = NULL;
    m_hAudioThread = NULL;
    m_dwAudioThreadId = 0;
    m_bDone = false;
    SampleRate = 0;
    m_numOutputBuffers = 0;
    m_OutputBuffersSize = 0;
    m_OutputBuffers = NULL;
    m_BufferMemory = NULL;
    DebugMessage(M64MSG_VERBOSE, "WO Constructor done");
}

WaveOutSoundDriver::~WaveOutSoundDriver()
{
    DebugMessage(M64MSG_VERBOSE, "WO Deconstructor");
    Teardown();
    DebugMessage(M64MSG_VERBOSE, "WO Deconstructor done");
}

void WaveOutSoundDriver::Teardown()
{
    DebugMessage(M64MSG_VERBOSE, "WO: Teardown()");

    /* 1. Signal the audio thread to stop.  The flag is written under
       m_devMutex so a thread in the middle of a refill cycle always
       sees it before it can touch the buffers again. */
    {
        PluginLockGuard lck(m_devMutex);
        m_bDone = true;
    }

    /* 2. JOIN the audio thread (wake it first so it exits at once).
       The thread is the only other user of the wave device, so after
       the join the device is quiescent and waveOutReset/Close are
       safe to call from this (emulator) thread. */
    if (m_hAudioThread != NULL)
    {
        if (m_hEvent != NULL)
            SetEvent(m_hEvent);
        if (WaitForSingleObject(m_hAudioThread, 2000) != WAIT_OBJECT_0)
        {
            DebugMessage(M64MSG_WARNING, "WO: audio thread did not exit - terminating");
            TerminateThread(m_hAudioThread, 0);
        }
        CloseHandle(m_hAudioThread);
        m_hAudioThread = NULL;
    }

    /* 3. Release the device and the buffers. */
    {
        PluginLockGuard lck(m_devMutex);
        if (m_hWave != NULL)
        {
            waveOutReset(m_hWave);
            waveOutClose(m_hWave);
            m_hWave = NULL;
        }
        if (m_hEvent != NULL)
        {
            CloseHandle(m_hEvent);
            m_hEvent = NULL;
        }
        if (m_OutputBuffers != NULL)
        {
            /* Allocated with new[] - must be freed with delete[]
               (plain delete on a new[] allocation is undefined
               behaviour - original WIP12.2 fix). */
            delete[] m_OutputBuffers;
        }
        if (m_BufferMemory != NULL)
        {
            delete[] m_BufferMemory;
        }
        m_OutputBuffers = NULL;
        m_BufferMemory = NULL;
        m_numOutputBuffers = 0;
        m_OutputBuffersSize = 0;
    }
    DebugMessage(M64MSG_VERBOSE, "WO: Teardown() done");
}

void WaveOutSoundDriver::Setup()
{
    DebugMessage(M64MSG_VERBOSE, "WO: Setup()");

    if (SampleRate == 0)
        return;

    bool deviceOk = false;

    {
        PluginLockGuard lck(m_devMutex);

        WAVEFORMATEX wfm;
        memset(&wfm, 0, sizeof(WAVEFORMATEX));

        wfm.wFormatTag = WAVE_FORMAT_PCM;
        wfm.nChannels = 2;
        wfm.nSamplesPerSec = SampleRate;
        wfm.wBitsPerSample = 16;
        wfm.nBlockAlign = wfm.wBitsPerSample / 8 * wfm.nChannels;
        wfm.nAvgBytesPerSec = wfm.nSamplesPerSec * wfm.nBlockAlign;

        m_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);   /* auto-reset */
        if (m_hEvent == NULL)
        {
            DebugMessage(M64MSG_ERROR, "WO: CreateEvent failed");
            return;
        }

        MMRESULT mr = waveOutOpen(&m_hWave, WAVE_MAPPER, &wfm,
                      (DWORD_PTR)m_hEvent, 0, CALLBACK_EVENT);
        if (mr != MMSYSERR_NOERROR)
        {
            DebugMessage(M64MSG_ERROR, "WO: waveOutOpen failed (%u) at %lu Hz",
                     (unsigned)mr, (unsigned long)SampleRate);
            CloseHandle(m_hEvent);
            m_hEvent = NULL;
            return;
        }
        deviceOk = true;
        m_bDone = false;

        /* Buffer sizing (original AziAudio model): one backend
           frame worth of stereo 16-bit data per buffer, three
           buffers in flight.  BackendFPS is clamped to 15..120 at
           load time; guard the division anyway. */
        u32 fps = Configuration::getBackendFPS();
        if (fps == 0) fps = 60;
        if (fps <= 60)
            m_OutputBuffersSize = (u32)(SampleRate / fps) * 4;
        else
            m_OutputBuffersSize = (u32)(SampleRate / 60) * 4;
        if (m_OutputBuffersSize < 16) m_OutputBuffersSize = 16;

        m_numOutputBuffers = 3;

        m_OutputBuffers = new (std::nothrow) WAVEHDR[m_numOutputBuffers];
        m_BufferMemory = new (std::nothrow) u8[(size_t)m_numOutputBuffers * (size_t)m_OutputBuffersSize];
        if (m_OutputBuffers == NULL || m_BufferMemory == NULL)
        {
            DebugMessage(M64MSG_ERROR, "WO: buffer allocation failed");
            delete[] m_OutputBuffers;
            delete[] m_BufferMemory;
            m_OutputBuffers = NULL;
            m_BufferMemory = NULL;
            waveOutClose(m_hWave);
            m_hWave = NULL;
            return;
        }
        memset(m_BufferMemory, 0, (size_t)m_numOutputBuffers * (size_t)m_OutputBuffersSize);
        memset(m_OutputBuffers, 0, sizeof(WAVEHDR) * m_numOutputBuffers);
        for (int i = 0; i < m_numOutputBuffers; i++)
        {
            m_OutputBuffers[i].lpData = (LPSTR)(m_BufferMemory + (size_t)i * (size_t)m_OutputBuffersSize);
            m_OutputBuffers[i].dwBufferLength = m_OutputBuffersSize;
            m_OutputBuffers[i].dwUser = i;
            waveOutPrepareHeader(m_hWave, &m_OutputBuffers[i], sizeof(WAVEHDR));
        }
        /* Prime the pipeline.  The buffers are zero-filled and
           LoadAiBuffer holds the last sample on underrun, so the
           device stays quiet until real audio arrives. */
        for (int i = 0; i < m_numOutputBuffers; i++)
        {
            waveOutWrite(m_hWave, &m_OutputBuffers[i], sizeof(WAVEHDR));
        }
    }

    if (deviceOk)
    {
        /* 4. Start the refill thread.  It wakes on the completion
           event (or a short timeout backstop) and refills every
           returned buffer - the same cycle the original winmm
           callback performed, but from a thread of our own. */
        m_hAudioThread = CreateThread(NULL, 0, AudioThreadProc, this, 0, &m_dwAudioThreadId);
        if (m_hAudioThread == NULL)
        {
            DebugMessage(M64MSG_ERROR, "WO: CreateThread failed - tearing device back down");
            Teardown();
        }
    }

    DebugMessage(M64MSG_VERBOSE, "WO: Setup() done");
}

/* ---------------------------------------------------------------------------
 * Audio thread - event-driven buffer refill
 * ------------------------------------------------------------------------ */
DWORD WINAPI WaveOutSoundDriver::AudioThreadProc(LPVOID lpParameter)
{
    WaveOutSoundDriver *self = (WaveOutSoundDriver *)lpParameter;
    self->AudioLoop();
    return 0;
}

void WaveOutSoundDriver::AudioLoop()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while (m_bDone == false)
    {
        /* Wake on buffer completion; the timeout is only a
           backstop (lost / coalesced auto-reset events). */
        if (m_hEvent != NULL)
            WaitForSingleObject(m_hEvent, 10);

        if (m_bDone)
            break;

        PluginLockGuard lck(m_devMutex);
        if (m_hWave == NULL)
            continue;

        for (int i = 0; i < m_numOutputBuffers; i++)
        {
            WAVEHDR *hdr = &m_OutputBuffers[i];

            if ((hdr->dwFlags & WHDR_DONE) == 0)
                continue;               /* still queued/playing */

            waveOutUnprepareHeader(m_hWave, hdr, sizeof(WAVEHDR));

            /* Pull new data.  LoadAiBuffer locks the
               SoundDriver ring-buffer mutex internally;
               m_devMutex is a DIFFERENT mutex, so this can
               never self-deadlock. */
            LoadAiBuffer((u8 *)hdr->lpData, m_OutputBuffersSize);

            waveOutPrepareHeader(m_hWave, hdr, sizeof(WAVEHDR));
            waveOutWrite(m_hWave, hdr, sizeof(WAVEHDR));
        }
    }

    DebugMessage(M64MSG_VERBOSE, "WO: audio thread terminated");
}

BOOL WaveOutSoundDriver::Initialize()
{
    DebugMessage(M64MSG_VERBOSE, "WO: Initialize()");
    Teardown();
    SampleRate = 0;
    DebugMessage(M64MSG_VERBOSE, "WO: Initialize() done");
    return TRUE;
}

void WaveOutSoundDriver::DeInitialize()
{
    DebugMessage(M64MSG_VERBOSE, "WO: DeInitialize()");
    Teardown();
    SampleRate = 0;
    DebugMessage(M64MSG_VERBOSE, "WO: DeInitialize() done");
}

void WaveOutSoundDriver::SetFrequency(u32 Frequency)
{
    DebugMessage(M64MSG_VERBOSE, "WO: SetFrequency(%lu)", (unsigned long)Frequency);
    if (SampleRate == Frequency && m_hWave != NULL)
        return;

    Teardown();
    SampleRate = Frequency;
    Setup();

    /* If the device could not be opened at this rate (rare), keep the
       emulator at full speed by draining the ring buffer in real
       time - the base-class fallback drain thread. */
    if (m_hWave == NULL)
        StartFallbackDrain();
    else
        StopFallbackDrain();
}

void WaveOutSoundDriver::StopAudio()
{
    DebugMessage(M64MSG_VERBOSE, "WO: StopAudio()");
    PluginLockGuard lck(m_devMutex);
    if (m_hWave != NULL)
        waveOutPause(m_hWave);
    DebugMessage(M64MSG_VERBOSE, "WO: StopAudio() done");
}

void WaveOutSoundDriver::StartAudio()
{
    DebugMessage(M64MSG_VERBOSE, "WO: StartAudio()");
    PluginLockGuard lck(m_devMutex);
    if (m_hWave != NULL)
        waveOutRestart(m_hWave);
    DebugMessage(M64MSG_VERBOSE, "WO: StartAudio() done");
}

/* Volume: 0..100, 100 = unity. waveOutSetVolume: 0=silent,
 * 0xFFFF=full per channel, packed as (R<<16)|L */
void WaveOutSoundDriver::SetVolume(u32 volume)
{
    SoundDriver::SetVolume(volume);

    DWORD level = (DWORD)(((float)GetVolume() / 100.0f) * 0xFFFF);
    DWORD result = (level & 0xFFFF) | ((level & 0xFFFF) << 16);
    PluginLockGuard lck(m_devMutex);
    if (m_hWave != NULL)
        waveOutSetVolume(m_hWave, result);
}

#endif
