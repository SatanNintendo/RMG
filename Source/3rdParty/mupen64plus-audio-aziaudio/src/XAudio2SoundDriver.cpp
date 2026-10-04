/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* XAudio2SoundDriver - classic XAudio 2.7 backend (COM / redist)            *
*                                                                           *
* Requires the XAudio 2.7 runtime (xaudio2_7.dll, "DirectX End-User         *
* Runtime June 2010" redistributable).  On clean Windows 10/11 installs      *
* the factory automatically prefers the modern xaudio2_9 driver instead;    *
* this backend is kept for Windows 7 / 8 with the redist and for            *
* compatibility with the original plugin.                                   *
*                                                                           *
* Pull model: a dedicated audio thread submits segments pulled from the    *
* SoundDriver ring buffer (LoadAiBuffer) to an XAudio2 source voice.       *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#include "common.h"
#if defined(ENABLE_BACKEND_XAUDIO2)
#include "XAudio2SoundDriver.h"
#include "AudioSpec.h"
#include "Configuration.h"
#include <string.h>
#include <psapi.h>
#include "SoundDriverRegistrar.h"

REGISTER_DRIVER(XAudio2SoundDriver, SND_DRIVER_XA2, "XAudio 2.7 (COM, needs xaudio2_7 redist)", 15)

static IXAudio2* g_engine;
static IXAudio2SourceVoice* g_source;
static IXAudio2MasteringVoice* g_master;

static bool audioIsPlaying = false;
static bool canPlay = false;

static int cacheSize = 0;
static VoiceCallback voiceCallback;

/* Submission buffers (4 rotating slots).  Slot 0 size must cover the
   largest possible cacheSize (freq/backendFPS * 4 bytes). */
static u8 bufferData[4][48000 / 15 * 4 + 64];

static PluginMutex hMutex;

/* XAudio2 2.7 DLL pinning: after a successful CoCreateInstance the
   xaudio2_7.dll is loaded into the process.  We take an extra reference
   and intentionally never release it, so the DLL cannot be unloaded
   while our objects (created from its code) still exist. */
static HMODULE g_hXAudioDLL = NULL;

static void PinXAudioDLL()
{
    HMODULE hMods[1024];
    DWORD cbNeeded;
    HANDLE hProcess = GetCurrentProcess();

    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
    {
        for (DWORD i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
        {
            WCHAR szModName[MAX_PATH];
            if (GetModuleFileNameExW(hProcess, hMods[i], szModName, MAX_PATH))
            {
                WCHAR* pName = szModName;
                WCHAR* pSlash = wcsrchr(szModName, L'\\');
                if (pSlash != NULL)
                    pName = pSlash + 1;

                if (_wcsicmp(pName, L"xaudio2_7.dll") == 0)
                {
                    /* LoadLibrary to increment the reference count */
                    g_hXAudioDLL = LoadLibraryExW(szModName, NULL, 0);
                    break;
                }
            }
        }
    }
}

bool XAudio2SoundDriver::ValidateDriver()
{
    bool retVal = false;
    /* Validate an XAudio2 2.7 object will initialize (requires the
       legacy DirectX redistributable).
       COM BALANCE FIX (same as WASAPI/DirectSound): the probe runs on
       the frontend thread at PluginStartup; on Qt hosts (STA)
       CoInitializeEx(MT) fails with RPC_E_CHANGED_MODE and the old
       unconditional CoUninitialize() destroyed the host's apartment.
       Only unbalance a successful init. */
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const GUID CLSID_XAudio2_Test = { 0x5a508685, 0xa254, 0x4fba, 0x9b, 0x82, 0x9a, 0x24, 0xb0, 0x03, 0x06, 0xaf };
    const GUID IID_IXAudio2_Test = { 0x8bcf1f58, 0x9fe7, 0x4583, 0x8a, 0xc6, 0xe2, 0xad, 0xc4, 0x65, 0xc8, 0xbb };
    IUnknown* obj;

    HRESULT hr = CoCreateInstance(CLSID_XAudio2_Test,
        NULL, CLSCTX_INPROC_SERVER, IID_IXAudio2_Test, (void**)&obj);
    if (SUCCEEDED(hr))
    {
        obj->Release();
        retVal = true;
    }
    if (SUCCEEDED(hrInit))
        CoUninitialize();
    return retVal;
}

XAudio2SoundDriver::XAudio2SoundDriver()
{
    g_engine = NULL;
    g_source = NULL;
    g_master = NULL;
    dllInitialized = false;
    bStopAudioThread = false;
    hAudioThread = NULL;
    /* COM BALANCE FIX: only remember a successful CoInitializeEx.
       On a Qt host the frontend thread is already STA-initialised,
     CoInitializeEx(MT) fails with RPC_E_CHANGED_MODE and the
     destructor must NOT decrement a balance we do not own
     (this exact underflow corrupted frontends at ROM close). */
    m_CoUninit = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
}

XAudio2SoundDriver::~XAudio2SoundDriver()
{
    DeInitialize();
    /* Balances the CoInitializeEx in the constructor - but only if
       it actually succeeded there.  Never call CoUninitialize on the
       Setup() failure paths either (the destructor would then
       decrement an initialisation we do not own - original WIP12.2
       fix). */
    if (m_CoUninit)
    {
        CoUninitialize();
        m_CoUninit = false;
    }
}

BOOL XAudio2SoundDriver::Initialize()
{
    if (g_source != NULL)
        g_source->Start();

    audioIsPlaying = false;
    canPlay = (g_source != NULL);
    cacheSize = 0;

    /* Restore the per-submission buffer size when a frequency is
       already known (after a ROM reset that does not re-program
       DACRATE) - otherwise the audio thread would submit nothing
       (original WIP12.1 fix, mirrors the Modern driver). */
    if (m_SamplesPerSecond > 0 && Configuration::getBackendFPS() > 0)
        cacheSize = (int)((m_SamplesPerSecond / Configuration::getBackendFPS())) * 4;

    return TRUE;
}

BOOL XAudio2SoundDriver::Setup()
{
    if (dllInitialized == true) return TRUE;
    dllInitialized = true;
    hAudioThread = NULL;
    audioIsPlaying = false;
    canPlay = false;
    cacheSize = 0;

    if (FAILED(XAudio2Create(&g_engine)))
    {
        g_engine = NULL;    /* no dangling pointer for Teardown (WIP12.2 fix) */
        dllInitialized = false;
        return FALSE;
    }

    if (FAILED(g_engine->CreateMasteringVoice(&g_master)))
    {
        g_engine->Release();
        g_engine = NULL;    /* prevent a double Release() in Teardown (WIP12.2 fix) */
        dllInitialized = false;
        return FALSE;
    }

    WAVEFORMATEX wfm;
    memset(&wfm, 0, sizeof(WAVEFORMATEX));
    wfm.wFormatTag = WAVE_FORMAT_PCM;
    wfm.nChannels = 2;
    wfm.nSamplesPerSec = (m_SamplesPerSecond != 0) ? m_SamplesPerSecond : 44100;
    wfm.wBitsPerSample = 16;
    wfm.nBlockAlign = wfm.wBitsPerSample / 8 * wfm.nChannels;
    wfm.nAvgBytesPerSec = wfm.nSamplesPerSec * wfm.nBlockAlign;

    if (FAILED(g_engine->CreateSourceVoice(&g_source, &wfm, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &voiceCallback, NULL, NULL)))
    {
        /* Destroy the mastering voice instead of leaking it, and
           null the engine so Teardown cannot release it twice
           (original WIP12.2 fix). */
        if (g_master != NULL)
        {
            g_master->DestroyVoice();
            g_master = NULL;
        }
        g_engine->Release();
        g_engine = NULL;
        dllInitialized = false;
        return FALSE;
    }

    /* Only advertise playability once the source voice really
       exists (GetState()/PlayBuffer dereference g_source). */
    canPlay = true;

    PinXAudioDLL();

    g_source->Start();
    iCurFrequency = (int)wfm.nSamplesPerSec;

    if (Configuration::getBackendFPS() > 0 && wfm.nSamplesPerSec > 0)
        cacheSize = (int)((wfm.nSamplesPerSec / Configuration::getBackendFPS())) * 4;

    SetVolume(GetVolume());

    return TRUE;
}

void XAudio2SoundDriver::DeInitialize()
{
    Teardown();
}

void XAudio2SoundDriver::Teardown()
{
    if (dllInitialized == false) return;
    canPlay = false;
    StopAudioThread();
    {
        PluginLockGuard lck(hMutex);
        if (g_source != NULL)
        {
            g_source->Stop();
            g_source->FlushSourceBuffers();
            g_source->DestroyVoice();
        }
        if (g_master != NULL) g_master->DestroyVoice();
        if (g_engine != NULL)
        {
            g_engine->StopEngine();
            g_engine->Release();
        }
    }
    g_engine = NULL;
    g_master = NULL;
    g_source = NULL;
    dllInitialized = false;
    canPlay = false;
    /* Do not free g_hXAudioDLL - pinned intentionally. */
    g_hXAudioDLL = NULL;
}

void XAudio2SoundDriver::PlayBuffer(u8* data, int bufferSize)
{
    if (g_source == NULL || canPlay == false)
        return;

    XAUDIO2_BUFFER xa2buff;

    xa2buff.Flags = 0;          /* 0 for continuous streaming playback */
    xa2buff.PlayBegin = 0;
    xa2buff.PlayLength = 0;
    xa2buff.LoopBegin = 0;
    xa2buff.LoopLength = 0;
    xa2buff.LoopCount = 0;
    xa2buff.pContext = NULL;
    xa2buff.AudioBytes = bufferSize;
    xa2buff.pAudioData = data;

    if (FAILED(g_source->SubmitSourceBuffer(&xa2buff)))
        return;

    XAUDIO2_VOICE_STATE xvs;
    g_source->GetState(&xvs);
    if (xvs.BuffersQueued >= 5)
        DebugMessage(M64MSG_WARNING, "XAudio2(2.7): %u buffers queued", (unsigned)xvs.BuffersQueued);
}

void XAudio2SoundDriver::SetFrequency(u32 Frequency)
{
    if (Setup() == FALSE)
        return;

    /* ALWAYS recompute the submission size (original WIP12.1 fix):
       computing it only inside the frequency-changed branch left it
       zero after a ROM reset with an unchanged DACRATE, resulting
       in complete silence. */
    if (Configuration::getBackendFPS() > 0 && Frequency > 0)
        cacheSize = (int)((Frequency / Configuration::getBackendFPS())) * 4;
    else
        cacheSize = 0;

    if (iCurFrequency != (int)Frequency)
    {
        iCurFrequency = (int)Frequency;
        if (g_source != NULL)
        {
            g_source->Stop();
            g_source->FlushSourceBuffers();
            g_source->SetSourceSampleRate(Frequency);
            g_source->Start();
        }
    }

    StartAudioThread();
}

DWORD WINAPI XAudio2SoundDriver::AudioThreadProc(LPVOID lpParameter)
{
    XAudio2SoundDriver* driver = (XAudio2SoundDriver*)lpParameter;

    /* Local submission index (a static one would be shared between
       driver instances). */
    int idx = 0;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while (driver->bStopAudioThread == false)
    {
        /* Guard against a zero submission size (e.g. SetFrequency
           not yet called or its Setup() failed) - previously this
           spun at TIME_CRITICAL priority submitting nothing
           (original WIP12.2 fix). */
        if (g_source != NULL && cacheSize > 0 && cacheSize <= (int)sizeof(bufferData[0]))
        {
            XAUDIO2_VOICE_STATE xvs;
            g_source->GetState(&xvs);
            /* Queued buffers: latency vs buffering knob. */
            while (xvs.BuffersQueued < 3 && driver->bStopAudioThread == false)
            {
                u32 len = driver->LoadAiBuffer(bufferData[idx], (u32)cacheSize);
                if (len > 0)
                {
                    driver->PlayBuffer(bufferData[idx], (int)len);
                    idx = (idx + 1) % 4;
                }
                else
                {
                    if (Configuration::getDisallowSleepXA2() == false)
                        Sleep(0);       /* yield the timeslice */
                }
                g_source->GetState(&xvs);
            }
        }
        if (Configuration::getDisallowSleepXA2() == false)
            Sleep(1);
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    return 0;
}

void XAudio2SoundDriver::StartAudioThread()
{
    if (hAudioThread == NULL && dllInitialized == true)
    {
        bStopAudioThread = false;
        hAudioThread = CreateThread(NULL, 0, AudioThreadProc, this, 0, NULL);
        if (hAudioThread == NULL)
            DebugMessage(M64MSG_ERROR, "XAudio2(2.7): CreateThread failed (%lu)", GetLastError());
    }
}

void XAudio2SoundDriver::StopAudioThread()
{
    if (hAudioThread != NULL)
    {
        bStopAudioThread = true;
        DWORD result = WaitForSingleObject(hAudioThread, 5000);
        if (result != WAIT_OBJECT_0)
        {
            TerminateThread(hAudioThread, 0);
            DebugMessage(M64MSG_WARNING, "XAudio2(2.7): audio thread had to be terminated");
        }
        CloseHandle(hAudioThread);
    }
    hAudioThread = NULL;
    bStopAudioThread = false;
}

void XAudio2SoundDriver::StopAudio()
{
    audioIsPlaying = false;
    StopAudioThread();
    if (g_source != NULL)
    {
        g_source->Stop();
        g_source->FlushSourceBuffers();
    }
}

void XAudio2SoundDriver::StartAudio()
{
    audioIsPlaying = true;
    if (Setup() == FALSE)
        return;
    if (g_source != NULL)
        g_source->Start();
    StartAudioThread();
}

/* Volume: 0..100, 100 = unity (XAudio2: 0.0 silent .. 1.0 full)
 *
 * VOLUME: plain unity at 100%, matching WASAPI and the XAudio 2.9 backend
 * (see their SetVolume for why the x1.4 boost they briefly carried was
 * removed - it hard-clips louder passages, audible as crackle). All four
 * Windows backends are now consistently unity-at-100%; DirectSound and
 * waveOut were already there, since their native volume APIs cannot
 * express anything above unity in the first place. */
void XAudio2SoundDriver::SetVolume(u32 volume)
{
    SoundDriver::SetVolume(volume);
    float xaVolume = (float)GetVolume() / 100.0f;
    if (g_source != NULL) g_source->SetVolume(xaVolume);
}

void __stdcall VoiceCallback::OnBufferEnd(void * pBufferContext)
{
    UNREFERENCED_PARAMETER(pBufferContext);
}

void __stdcall VoiceCallback::OnVoiceProcessingPassStart(UINT32 SamplesRequired)
{
    UNREFERENCED_PARAMETER(SamplesRequired);
}
#endif
