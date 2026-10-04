/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* WASAPISoundDriver.cpp - WASAPI shared-mode backend implementation         *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#include "common.h"
#if defined(ENABLE_BACKEND_WASAPI)
#include "WASAPISoundDriver.h"
#include "AudioSpec.h"
#include "Configuration.h"
#include <string.h>
#include "SoundDriverRegistrar.h"
#include <audioclient.h>
#include <mmdeviceapi.h>

REGISTER_DRIVER(WASAPISoundDriver, SND_DRIVER_WASAPI, "WASAPI Driver (experimental)", 0)

/* REFERENCE_TIME time units per second and per millisecond */
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

#define EXIT_ON_ERROR(hres)  \
        if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
        if ((punk) != NULL)  \
        { (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

/* KSDATAFORMAT_SUBTYPE_IEEE_FLOAT {00000003-0000-0010-8000-00AA00389B71}:
   in MinGW headers the GUID only materialises with INITGUID, so define it
   locally to avoid depending on the exact SDK behaviour. */
static const GUID LOCAL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT =
{ 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

bool WASAPISoundDriver::ValidateDriver()
{
    bool retVal = false;
    /* Validate a Windows audio services end point enumerator object
       will initialize.

       COM BALANCE FIX (frontend crash at ROM close, confirmed by
       minidump analysis): this probe runs on the FRONTEND thread at
       PluginStartup time (SoundDriverFactory::Initialize -> all
       RegisterSoundDriver probes).  On Qt frontends (RMG) that thread
       already has COM initialised as STA by Qt.  CoInitializeEx(MT)
       then fails with RPC_E_CHANGED_MODE, and the previous revision
       called CoUninitialize() UNCONDITIONALLY - decrementing the
       frontend's own COM balance and destroying its apartment behind
       the host's back.  Later apartment churn (RomClosed teardown)
       then cascaded through ole32/MMDevAPI and crashed the emulator
       with a DEP execute violation on a garbage pointer.
       Balance CoUninitialize() strictly against a SUCCESSFUL
       CoInitializeEx(). */
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const GUID CLSID_MMDeviceEnumerator_Test = { 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E };
    const GUID IID_IMMDeviceEnumerator_Test = { 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 };
    IUnknown* obj;

    HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator_Test,
        NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator_Test, (void**)&obj);
    if (SUCCEEDED(hr))
    {
        obj->Release();
        retVal = true;
    }
    if (SUCCEEDED(hrInit))
        CoUninitialize();
    return retVal;
}

WASAPISoundDriver::WASAPISoundDriver()
{
    bInitialized = false;
    hAudioThread = NULL;
    bStopAudioThread = false;
}

WASAPISoundDriver::~WASAPISoundDriver()
{
    DeInitialize();
}

BOOL WASAPISoundDriver::Initialize()
{
    /* THREAD-MODEL FIX (frontend crash at ROM close, confirmed by
       minidump analysis): COM is now initialised ONLY on the audio
       render thread, which owns the entire WASAPI object graph
       (enumerator, device, audio client, render client) and releases
       it before its own CoUninitialize().  The frontend's calling
       thread never enters an apartment on the plugin's behalf:
       initialising/destroying an MTA on the frontend thread (the
       previous design) corrupted apartment state in hosts that keep
       COM initialised there (Qt STA in RMG; Windows 7 + VxKex
       especially) and crashed the emulator during the ROM-close
       teardown cascade.  The startup probe (ValidateDriver) already
       proved the enumerator can be created; failures at runtime are
       reported by the render thread via DebugMessage. */
    if (bInitialized == true)
        return TRUE;
    bInitialized = true;
    return TRUE;
}

void WASAPISoundDriver::DeInitialize()
{
    DWORD exitCode;
    if (bInitialized == true)
    {
        bStopAudioThread = true;
        if (hAudioThread != NULL)
        {
            WaitForSingleObject(hAudioThread, 5000);
            GetExitCodeThread(hAudioThread, &exitCode);
            if (exitCode == STILL_ACTIVE)
            {
                /* Last resort only: the render thread is
                   stuck inside a blocked audio-service
                   call.  Its COM objects leak (they belong
                   to the render thread's own apartment,
                   so the corruption is contained). */
                TerminateThread(hAudioThread, (DWORD)-1);
                DebugMessage(M64MSG_ERROR, "WASAPI: render thread had to be terminated");
            }
            CloseHandle(hAudioThread);
            hAudioThread = NULL;
        }
    }
    bInitialized = false;
}

void WASAPISoundDriver::SetFrequency(u32 Frequency)
{
    UNREFERENCED_PARAMETER(Frequency);
    if (hAudioThread == NULL)
    {
        bStopAudioThread = false;
        hAudioThread = CreateThread(NULL, 0, AudioThreadProc, this, 0, NULL);
        if (hAudioThread == NULL)
            DebugMessage(M64MSG_ERROR, "WASAPI: CreateThread failed (%lu)", GetLastError());
    }
}

void WASAPISoundDriver::StopAudio()
{
    /* The render loop exits through bStopAudioThread; actual teardown
       happens in DeInitialize(). */
}

void WASAPISoundDriver::StartAudio()
{
    SetFrequency(m_SamplesPerSecond);
}

/* Volume: 0..100, 100 = unity (applied in LoadAiBufferResample) */
void WASAPISoundDriver::SetVolume(u32 volume)
{
    SoundDriver::SetVolume(volume);
}

DWORD WINAPI WASAPISoundDriver::AudioThreadProc(LPVOID lpParameter)
{
    WASAPISoundDriver* driver = (WASAPISoundDriver*)lpParameter;
    /* NOTE: all declarations stay BEFORE the first goto (C++ forbids
       jumping over initialisations); the macros below may jump to
       the Exit label at any point. */
    REFERENCE_TIME hnsRequestedDuration;
    REFERENCE_TIME hnsActualDuration;
    HRESULT hr;
    IMMDeviceEnumerator *pEnumerator;
    IMMDevice *pDevice;
    IAudioClient *pAudioClient;
    IAudioRenderClient *pRenderClient;
    WAVEFORMATEXTENSIBLE *pwfx;
    UINT32 bufferFrameCount;
    UINT32 numFramesAvailable;
    UINT32 numFramesPadding;
    BYTE *pData;
    DWORD flags;
    WAVEFORMATEXTENSIBLE AudioFormat;
    float ratio;
    u32 n64rate;
    u32 curRate;
    BOOL coInitialized = FALSE;

    pEnumerator = NULL;
    pDevice = NULL;
    pAudioClient = NULL;
    pRenderClient = NULL;
    pwfx = NULL;
    flags = 0;
    memset(&AudioFormat, 0, sizeof(AudioFormat));
    hnsRequestedDuration = REFTIMES_PER_MILLISEC * (1000 / Configuration::getBackendFPS()) * Configuration::getBufferLevel();

    /* COM lives and dies on THIS thread only: the render thread owns
       the whole WASAPI object graph and releases it below before its
       own CoUninitialize() - the frontend thread is never involved
       (see Initialize()/DeInitialize() notes).  Balance the
       CoUninitialize() strictly against a successful init so an
       early CoInitializeEx failure can not underflow the apartment
       count. */
    hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr))
    {
        coInitialized = TRUE;
    }
    else
    {
        goto Exit;
    }

    /* Get ourselves a device enumerator.  This can be used to
       determine which device we want to write to */
    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, IID_IMMDeviceEnumerator,
        (void**)&pEnumerator);
    EXIT_ON_ERROR(hr);

    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    EXIT_ON_ERROR(hr);

    hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient); EXIT_ON_ERROR(hr);

    hr = pAudioClient->GetMixFormat((WAVEFORMATEX **)&pwfx);        EXIT_ON_ERROR(hr);
    AudioFormat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    AudioFormat.Format.nChannels = 2;
    AudioFormat.Format.nSamplesPerSec = pwfx->Format.nSamplesPerSec;
    AudioFormat.Format.wBitsPerSample = 32;
    AudioFormat.Format.nBlockAlign = (AudioFormat.Format.wBitsPerSample / 8) * AudioFormat.Format.nChannels;
    AudioFormat.Format.nAvgBytesPerSec = AudioFormat.Format.nSamplesPerSec * AudioFormat.Format.nBlockAlign;
    AudioFormat.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    AudioFormat.dwChannelMask = 0x3;
    AudioFormat.Samples.wValidBitsPerSample = 32;
    AudioFormat.Samples.wSamplesPerBlock = 32;
    AudioFormat.Samples.wReserved = 32;
    AudioFormat.SubFormat = LOCAL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,      /* AUDCLNT_STREAMFLAGS_RATEADJUST | AUTOCONVERTPCM could
               be used to let the mixer resample; we resample ourselves */
        hnsRequestedDuration,
        0,
        (WAVEFORMATEX *)&AudioFormat,
        NULL);
    EXIT_ON_ERROR(hr)

    /* Get the actual size of the allocated buffer. */
    hr = pAudioClient->GetBufferSize(&bufferFrameCount); EXIT_ON_ERROR(hr);
    hr = pAudioClient->GetService(IID_IAudioRenderClient,(void**)&pRenderClient); EXIT_ON_ERROR(hr);

    /* Grab the entire buffer for the initial fill operation. */
    hr = pRenderClient->GetBuffer(bufferFrameCount, &pData); EXIT_ON_ERROR(hr);
    n64rate = driver->m_SamplesPerSecond;
    if (n64rate == 0) n64rate = 44100;
    ratio = (float)n64rate / (float)AudioFormat.Format.nSamplesPerSec;

    /* Load the initial data into the shared buffer. */
    driver->LoadAiBufferResample(pData, bufferFrameCount, ratio);

    hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags);     EXIT_ON_ERROR(hr);

    /* Calculate the actual duration of the allocated buffer. */
    hnsActualDuration = ((REFTIMES_PER_SEC * bufferFrameCount) / AudioFormat.Format.nSamplesPerSec);
    DebugMessage(M64MSG_VERBOSE, "WASAPI: requested duration %i ms, actual %i ms, buffer frames %u",
        (int)(1000 / Configuration::getBackendFPS()), (int)(hnsActualDuration / REFTIMES_PER_MILLISEC), (unsigned)bufferFrameCount);

    hr = pAudioClient->Start(); EXIT_ON_ERROR(hr);  /* Start playing. */

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    /* Each loop fills about half of the shared buffer. */
    while (driver->bStopAudioThread == false)
    {
        /* See how much buffer space is available. */
        hr = pAudioClient->GetCurrentPadding(&numFramesPadding); EXIT_ON_ERROR(hr);

        numFramesAvailable = bufferFrameCount - numFramesPadding;

        if (numFramesAvailable > 0)
        {
            /* Grab all the available space in the shared buffer. */
            hr = pRenderClient->GetBuffer(numFramesAvailable, &pData); EXIT_ON_ERROR(hr);

            /* Re-read the N64 rate each iteration so a mid-game
               DACRATE change is picked up without restarting
               the device. */
            curRate = driver->m_SamplesPerSecond;
            if (curRate == 0) curRate = 44100;
            ratio = (float)curRate / (float)AudioFormat.Format.nSamplesPerSec;
            driver->LoadAiBufferResample(pData, numFramesAvailable, ratio);
            hr = pRenderClient->ReleaseBuffer(numFramesAvailable, flags); EXIT_ON_ERROR(hr);
            hnsActualDuration = ((REFTIMES_PER_SEC * numFramesAvailable) / AudioFormat.Format.nSamplesPerSec);
        }
        else
        {
            Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));
            hnsActualDuration = REFTIMES_PER_MILLISEC * 2;  /* at least ~1 ms sleep */
        }
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    DebugMessage(M64MSG_VERBOSE, "WASAPI: render loop ended");

    Sleep(10);

    hr = pAudioClient->Stop(); EXIT_ON_ERROR(hr);  /* Stop playing. */

    /* Graceful exit */
    goto NiceExit;
Exit:
    DebugMessage(M64MSG_ERROR, "WASAPI: error in the audio thread (HRESULT 0x%08lX) - switching to silent drain mode (no audio, but A/V sync and full emulation speed are kept)", (unsigned long)hr);

    /* RENDER-FAILURE STALL FIX: the old code exited the thread here,
       which left the ring buffer without any consumer - with the A/V
       sync throttle enabled the emulator then crawled at ~1 DMA per
       second.  Stay on THIS thread and consume the buffer at
       real-time rate instead; DeInitialize() still waits for this
       thread via bStopAudioThread, so teardown is unaffected. */
    {
        u32 lastTick = (u32)GetTickCount();
        while (driver->bStopAudioThread == false)
        {
            Sleep(5);
            u32 now = (u32)GetTickCount();
            u32 elapsed = now - lastTick;
            if (elapsed == 0)
                continue;
            if (elapsed > 500)
                elapsed = 500;      /* catch-up guard */
            lastTick = now;
            u32 rate = driver->m_SamplesPerSecond;
            if (rate == 0) rate = 44100;
            u32 bytes = (u32)((u64)rate * 4ull * elapsed / 1000ull);
            driver->ConsumeBuffered(bytes);
        }
        DebugMessage(M64MSG_VERBOSE, "WASAPI: drain mode ended");
    }
NiceExit:
    CoTaskMemFree(pwfx);
    SAFE_RELEASE(pEnumerator);
    SAFE_RELEASE(pDevice);
    SAFE_RELEASE(pAudioClient);
    SAFE_RELEASE(pRenderClient);
    if (coInitialized)
        CoUninitialize();

    DebugMessage(M64MSG_VERBOSE, "WASAPI: render thread exited");
    return 0;
}

#endif
