/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* XAudio2SoundDriverModern.cpp - true XAudio 2.9 backend implementation     *
*                                                                           *
* WALL-CLOCK PACING REDESIGN - see the header (XAudio2SoundDriverModern.h)  *
* for the full rationale.  Short version:                                   *
*                                                                           *
*   OLD: ring buffer drained only as fast as the XAudio2 2.9 engine         *
*        retired queued buffers (GetState polling) -> AI_LenChanged()       *
*        throttled the whole emulation to the engine's pace -> "sound       *
*        works but everything runs in slow motion" on machines where        *
*        xaudio2_9 consumes slower than real time.                          *
*                                                                           *
*   NEW: the ring buffer is drained at wall-clock rate (QPC); the engine    *
*        is fed event-driven (OnBufferEnd); if the engine falls behind,     *
*        the excess is dropped instead of stalling the emulator.  A slow    *
*        or partially dead engine now degrades the AUDIO, never the GAME.   *
*                                                                           *
*   SESSION 7 (v1.0.11): the emergency-only GetState reconcile could never  *
*   fire while the feeder was actively feeding (its no-recent-submission    *
*   safety gate excluded exactly that state), so a partially dead           *
*   OnBufferEnd channel froze a phantom in-flight debt equal to the whole   *
*   150 ms budget - submissions capped at ~75% of real time, 25% of the     *
*   game's audio dropped as gaps, and with the A/V throttle on the whole    *
*   game dragged to ~56% speed.  The reconcile is now continuous (~16 ms    *
*   cadence, snapshot under m_SubmitMutex, 2-buffer safety margin), the     *
*   dry-ring guard stops feeding held-sample padding, and the drop          *
*   accounting counts only bytes that really left the ring.                 *
*                                                                           *
*   SESSION 7 RESTORED (v1.0.15): the 1.0.13 "cleanup" rewrote the          *
*   reconcile into a ~500 ms watchdog that recreated the voice instead of   *
*   healing - reintroducing the debt freeze plus a recreation storm (27x    *
*   "sustained queue/accounting mismatch" warnings, jerky audio, slow       *
*   game).  Continuous heal-only reconcile restored; voice recreation      *
*   stays exclusively on the DACRATE-change path (SESSION 5).               *
*                                                                           *
*   SESSION 8 (v1.0.16): minidump forensics of the follow-up report         *
*   ("no more slowdown, but a slight crackle + the emulator crashes after   *
*   any plugin manipulation") on the Windows 7 / VxKex machine showed      *
*   xaudio2_9.dll there is a VxKex SHIM (wrapping the real XAudio 2.7)     *
*   with its own ~40-thread worker pool.  Three hardening fixes:           *
*   1. Teardown is an ordered, callback-detached shutdown (late pool       *
*      dispatches can no longer write into freed memory -> the heap        *
*      corruption crash), and the shim DLL is pinned for the process.      *
*   2. The token ring is a bounded MPMC queue (the shim may dispatch      *
*      callbacks from several pool threads at once; the SPSC ring lost     *
*      a token per race - the root of the "partially dead" channel).       *
*   3. The reconcile is age-gated (a slot is only heal-retirable after     *
*      max(40 ms, 2x chunk duration), so an under-reporting GetState      *
*      can never cause live PCM to be recycled mid-playback - the         *
*      crackle).  See the class header (point SESSION 8) for details.      *
*                                                                           *
* Steady state with a healthy engine (identical latency to the old         *
* driver): ~3 chunks queued in the voice, the feeder submits exactly one    *
* chunk per chunk of wall time.  The "pacing credit" goes slightly          *
* negative while the comfort queue is being topped up and is repaid by the  *
* clock - in equilibrium credit, submissions and retirements all advance    *
* at 1x real time, and the queue depth never drops below the comfort        *
* floor, so scheduler jitter cannot underrun the voice.                      *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#include "common.h"
#if defined(ENABLE_BACKEND_XAUDIO2_MODERN)

#include "XAudio2SoundDriverModern.h"
#include "AudioSpec.h"
#include "Configuration.h"
#include "SoundDriverRegistrar.h"

#include <string.h>
#include <new>

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

/* Register with the factory.  Highest priority on modern Windows -
* the factory only calls ValidateDriver() (a plain LoadLibrary probe),
* the engine itself is created later in Setup(). */
REGISTER_DRIVER(XAudio2SoundDriverModern, SND_DRIVER_XA2_MODERN, "XAudio 2.9 (xaudio2_9.dll, Win10/11)", 16)

/* QPC helpers for the wall-clock pacing. */

static u64 xa2_qpc_now(void)
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return (u64)li.QuadPart;
}

static u64 xa2_qpc_freq(void)
{
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    return (u64)li.QuadPart;
}

/* ---------------------------------------------------------------------------
 * Validation - probe for xaudio2_9.dll without creating any object
 * ------------------------------------------------------------------------ */
bool XAudio2SoundDriverModern::ValidateDriver()
{
    HMODULE hMod = LoadLibraryExW(L"xaudio2_9.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hMod == NULL)
        return false;
    FreeLibrary(hMod);
    return true;
}

/* ---------------------------------------------------------------------------
 * XACallbackShard::Init - fresh ring state + wake event (SESSION 9).
 * Called once per Setup(); the shard is then never freed - see the header.
 * ------------------------------------------------------------------------ */
bool XACallbackShard::Init()
{
    for (u32 i = 0; i < kXaPendingCount; i++)
        seq[i].store(i, std::memory_order_relaxed);
    enqueuePos.store(0, std::memory_order_relaxed);
    dequeuePos.store(0, std::memory_order_relaxed);
    retired.store(false, std::memory_order_relaxed);
    deviceLost.store(false, std::memory_order_relaxed);
    lastVoiceErrorHr.store(0, std::memory_order_relaxed);
    wakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    return (wakeEvent != NULL);
}

/* ---------------------------------------------------------------------------
 * Construction / destruction
 * ------------------------------------------------------------------------ */
XAudio2SoundDriverModern::XAudio2SoundDriverModern()
{
    dllInitialized = false;
    bStopAudioThread = false;
    hAudioThread = NULL;
    iCurFrequency = 0;
    bCoInitialized = false;
    m_Engine = NULL;
    m_Master = NULL;
    m_Source = NULL;
    m_hXAudioDLL = NULL;
    m_pfnXAudio2Create = NULL;
    cacheSize = 0;

    /* SESSION 9: callback infrastructure lives outside this object now -
     * allocated in Setup(), deliberately abandoned in Teardown(). */
    m_CB = NULL;
    m_pVoiceCallback = NULL;
    m_pEngineCallback = NULL;

    m_Slots = NULL;
    m_SlotCount = 0;
    memset(m_FreeSlots, 0, sizeof(m_FreeSlots));
    m_FreeSlotCount = 0;
    memset(m_OutstandingSlots, 0, sizeof(m_OutstandingSlots));
    m_OutstandingCount = 0;
    m_PacingCredit = 0;
    m_ResyncPacingClock.store(false, std::memory_order_relaxed);
    m_BytesSubmitted.store(0, std::memory_order_relaxed);
    m_BytesRetired.store(0, std::memory_order_relaxed);
    m_LagWarned = false;
    m_ReconcileHealedLogged = false;
    m_HealthyHealStreak = 0;
}

XAudio2SoundDriverModern::~XAudio2SoundDriverModern()
{
    DeInitialize();
}

/* ---------------------------------------------------------------------------
 * SESSION 9 callback-infrastructure helpers.  AllocateCallbackInfra()
 * creates the shard (ring + event + flags) and the two interface objects;
 * AbandonCallbackInfra() detaches and deliberately LEAKS them - the safety
 * contract that makes every late engine/shim callback harmless.
 * ------------------------------------------------------------------------ */
bool XAudio2SoundDriverModern::AllocateCallbackInfra()
{
    if (m_CB != NULL)
        return true;    /* already allocated (idempotent) */

    m_CB = new(std::nothrow) XACallbackShard;
    if (m_CB == NULL)
        return false;
    if (m_CB->Init() == false)
        return false;

    m_pVoiceCallback  = new(std::nothrow) VoiceCallbackModern(m_CB);
    m_pEngineCallback = new(std::nothrow) EngineCallbackModern(m_CB);
    if (m_pVoiceCallback == NULL || m_pEngineCallback == NULL)
        return false;

    return true;
}

void XAudio2SoundDriverModern::AbandonCallbackInfra()
{
    if (m_CB != NULL)
        m_CB->retired.store(true, std::memory_order_release);
    /* Deliberate leak (SESSION 9): the shard, the wake event and both
     * interface objects stay mapped until process exit.  A VxKex shim
     * worker that was preempted mid-dispatch may still enter them at any
     * later time; freeing here would turn that into a use-after-free /
     * heap corruption (the exact "sometimes silently closes" residual of
     * v1.0.16).  ~3 KB and one event handle per engine creation. */
    m_CB = NULL;
    m_pVoiceCallback = NULL;
    m_pEngineCallback = NULL;
}

/* ---------------------------------------------------------------------------
 * Setup - create the XAudio2 engine (2.9) via GetProcAddress
 * ------------------------------------------------------------------------ */
BOOL XAudio2SoundDriverModern::Setup()
{
    /* A prior critical-error callback (device unplugged/changed while
     * running) means the whole engine is dead and every further
     * XAudio2 call on it will fail.  Tear it down so the code below
     * rebuilds it against the (possibly new) default device, instead of
     * quietly staying silent forever. */
    if (dllInitialized == true && m_CB != NULL &&
        m_CB->deviceLost.load(std::memory_order_relaxed) == true)
    {
        long voiceErrHr = m_CB->lastVoiceErrorHr.load(std::memory_order_relaxed);
        if (voiceErrHr != 0)
        {
            DebugMessage(M64MSG_ERROR,
                "XAudio2.9: rebuilding the engine because a voice reported OnVoiceError (0x%08X) earlier",
                (unsigned)voiceErrHr);
        }
        else
        {
            DebugMessage(M64MSG_WARNING,
                "XAudio2.9: rebuilding the engine because OnCriticalError fired earlier (device changed/removed)");
        }
        Teardown();
    }

    if (dllInitialized == true)
        return TRUE;

    dllInitialized = true;
    bStopAudioThread = false;
    hAudioThread = NULL;
    cacheSize = 0;
    bCoInitialized = false;

    /* COM: multithreaded apartment.  If the host already initialised COM
       with another apartment model (RPC_E_CHANGED_MODE) XAudio2 still
       works, but we must not balance it with CoUninitialize later. */
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hrCo))
    {
        bCoInitialized = true;
    }
    else if (hrCo == RPC_E_CHANGED_MODE)
    {
        bCoInitialized = false;
    }
    else
    {
        dllInitialized = false;
        return FALSE;
    }

    /* SESSION 9: allocate the callback infrastructure FIRST - everything
     * the engine will point at (and anything a late shim callback can
     * touch) must exist before the first engine object is created, and
     * must outlive this driver object (which the frontend deletes right
     * after AI_Shutdown).  On failure the allocations are abandoned, not
     * freed (see AbandonCallbackInfra). */
    if (AllocateCallbackInfra() == false)
    {
        DebugMessage(M64MSG_ERROR, "XAudio2.9: callback infrastructure allocation failed - cannot start the engine");
        AbandonCallbackInfra();
        if (bCoInitialized) { CoUninitialize(); bCoInitialized = false; }
        dllInitialized = false;
        return FALSE;
    }

    /* Load xaudio2_9.dll (present on Windows 10/11) and resolve
     * XAudio2Create.  No COM class registration (the 2.7 CLSID) and no
     * import library are involved.
     *
     * SESSION 8 - PROCESS-LIFETIME PIN: the module is loaded ONCE and
     * NEVER FreeLibrary'd.  On the VxKex machine from the minidump the
     * shim keeps a ~40-thread worker pool; unloading its code while any
     * of those threads is still executing (or parked inside) it would
     * be a guaranteed crash, and even a correctly-quiesced shim gains
     * nothing from the unload - the plugin DLL itself stays loaded for
     * the whole frontend session anyway.  A static handle makes repeat
     * Setup()/Teardown() cycles reuse the one pinned load. */
    static HMODULE s_pinnedXAudioDLL = NULL;
    if (s_pinnedXAudioDLL == NULL)
        s_pinnedXAudioDLL = LoadLibraryExW(L"xaudio2_9.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    m_hXAudioDLL = s_pinnedXAudioDLL;
    if (m_hXAudioDLL == NULL)
    {
        DebugMessage(M64MSG_ERROR, "XAudio2: LoadLibrary(xaudio2_9.dll) failed (%lu). XAudio 2.9 requires Windows 10/11 - on Windows 7 use the 'xa2' (2.7) or WASAPI backend", GetLastError());
        AbandonCallbackInfra();
        if (bCoInitialized) { CoUninitialize(); bCoInitialized = false; }
        dllInitialized = false;
        return FALSE;
    }

    m_pfnXAudio2Create = (M_PFN_XAudio2Create)GetProcAddress(m_hXAudioDLL, "XAudio2Create");
    if (m_pfnXAudio2Create == NULL)
    {
        DebugMessage(M64MSG_ERROR, "XAudio2: GetProcAddress(XAudio2Create) failed");
        m_hXAudioDLL = NULL;   /* the pinned module itself stays loaded */
        AbandonCallbackInfra();
        if (bCoInitialized) { CoUninitialize(); bCoInitialized = false; }
        dllInitialized = false;
        return FALSE;
    }

    /* Real documented default for XAudio 2.8/2.9 is XAUDIO2_ANY_PROCESSOR
       (0xFFFFFFFF) - see the comment on M_XAUDIO2_DEFAULT_PROCESSOR in
       XAudio2ModernApi.h.  "0" is not a valid processor bitmask; keep it
       only as a defensive second attempt for oddball shims that may
       expect it. */
    HRESULT hrCreate = (*m_pfnXAudio2Create)(&m_Engine, 0, M_XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hrCreate))
    {
        DebugMessage(M64MSG_WARNING, "XAudio2: XAudio2Create(processor=0x%08X) failed (0x%08X), retrying with processor=0", (unsigned)M_XAUDIO2_DEFAULT_PROCESSOR, (unsigned)hrCreate);
        m_Engine = NULL;
        hrCreate = (*m_pfnXAudio2Create)(&m_Engine, 0, M_XAUDIO2_LEGACY_ZERO_PROCESSOR);
    }
    if (FAILED(hrCreate))
    {
        DebugMessage(M64MSG_ERROR, "XAudio2: XAudio2Create failed (0x%08X)", (unsigned)hrCreate);
        m_Engine = NULL;
        m_hXAudioDLL = NULL;   /* the pinned module itself stays loaded */
        AbandonCallbackInfra();
        if (bCoInitialized) { CoUninitialize(); bCoInitialized = false; }
        dllInitialized = false;
        return FALSE;
    }

    /* XAudio 2.8+ requires an explicit StartEngine(). */
    if (FAILED(m_Engine->StartEngine()))
    {
        DebugMessage(M64MSG_ERROR, "XAudio2: StartEngine failed");
        m_Engine->Release();
        m_Engine = NULL;
        m_hXAudioDLL = NULL;   /* the pinned module itself stays loaded */
        AbandonCallbackInfra();
        if (bCoInitialized) { CoUninitialize(); bCoInitialized = false; }
        dllInitialized = false;
        return FALSE;
    }

    /* Report critical device errors so Setup() can rebuild the engine
     * when the default device disappears (harmless failure to register -
     * we just lose the automatic-recovery signal, Setup() still works
     * without it).  SESSION 9: the engine callback object lives in the
     * shard bundle; its deviceLost flag starts false on a fresh Init(). */
    m_Engine->RegisterForCallbacks(m_pEngineCallback);

    /* Mastering voice - device defaults, EXCEPT the stream category.
     *
     * ROOT CAUSE OF "XAudio 2.9 backend: complete silence, other backends
     * fine" (this round's investigation): the previous revision omitted
     * the Category argument, so it defaulted to AudioCategory_GameEffects
     * (the default value baked into M_IXAudio2::CreateMasteringVoice's
     * declaration in XAudio2ModernApi.h).  Per Microsoft's own XAudio 2.9
     * documentation ("Developer guide for redistributable version of
     * XAudio 2.9" / "Spatial sound and virtual surround"):
     *
     *   - Starting with Windows 10 1903, XAudio 2.9 automatically routes
     *     a mastering voice through the user's system-wide spatial sound
     *     encoder (Windows Sonic, Dolby Atmos for Headphones, DTS
     *     Headphone:X, ...) whenever BOTH (a) the process is recognised
     *     as a "game" by the Game Bar/Xbox Game Monitoring service, and
     *     (b) the voice's stream category is one of the "spatial sound
     *     enabled" categories.
     *   - AudioCategory_GameEffects is explicitly on that list (along
     *     with GameMedia, Movie, Media, ForegroundOnlyMedia).
     *   - AudioCategory_Other, AudioCategory_SoundEffects,
     *     AudioCategory_Communications, AudioCategory_Alerts and
     *     AudioCategory_GameChat are explicitly NOT.
     *
     *   RMG/mupen64plus-core is picked up by Xbox Game Bar as a "game"
     *   on many systems (it is a full-screen emulator).  When the user
     *   also has a spatial sound format enabled in Windows' Sound
     *   settings (very common - Windows actively suggests "Windows
     *   Sonic for Headphones" on first headphone/USB-DAC connection),
     *   XAudio 2.9 hands our plain 16-bit stereo PCM buffers to the
     *   spatial encoder instead of the normal fold-down/passthrough
     *   path.  On affected driver/device/APO combinations that pipeline
     *   silently produces nothing for a plain (non-3D, already
     *   downmixed) stereo source: SubmitSourceBuffer/GetState report
     *   total success (which is exactly what the "no error, no crash,
     *   just silence" symptom looks like from the plugin's side - there
     *   is nothing to catch with an HRESULT check), the game runs at
     *   full speed, and only the actual audio output is dropped.
     *
     *   None of the other backends can hit this: DirectSound and
     *   WaveOut predate the whole spatial-sound feature, WASAPI (as
     *   implemented in WASAPISoundDriver.cpp) opens the endpoint
     *   directly via IAudioClient and never goes through IXAudio2 at
     *   all, and the classic XAudio2SoundDriver.cpp (2.7 COM) calls
     *   CreateMasteringVoice() with only the output pointer - the 2.7
     *   interface does not even HAVE a Category parameter, so it can
     *   never opt into this path either.  This is why the user sees
     *   "only XAudio 2.9 is silent, every other backend (and the
     *   original zilmar-spec AziAudio under Project64, which never
     *   requests GameEffects) works fine on the same machine".
     *
     * FIX: explicitly request AudioCategory_Other - the same "plain
     * PCM output" category implicitly used by every backend that
     * already works - so the mastering voice never enters the spatial
     * pipeline, regardless of Game Bar detection or the user's Windows
     * Sound settings.  This is also the more semantically correct
     * category for the plugin's output: it is a single pre-mixed
     * stereo PCM stream produced by HLE audio microcode emulation, not
     * a 3D/positional "game effects" source that would benefit from
     * spatial processing in the first place.
     * Reference: https://learn.microsoft.com/windows/win32/xaudio2/xaudio2-redistributable
     * ("Testing" > "Spatial sound in newer versions of Windows 10").
     *
     * DeviceId and pEffectChain are passed explicitly (both NULL, the
     * documented defaults) purely because C++ default arguments cannot
     * be skipped positionally - Category is the LAST parameter. */
    if (FAILED(m_Engine->CreateMasteringVoice(&m_Master, M_XAUDIO2_DEFAULT_CHANNELS, M_XAUDIO2_DEFAULT_SAMPLERATE, 0, NULL, NULL, M_AudioCategory_Other)))
    {
        DebugMessage(M64MSG_ERROR, "XAudio2: CreateMasteringVoice failed");
        m_Engine->UnregisterForCallbacks(m_pEngineCallback);
        m_Engine->StopEngine();
        m_Engine->Release();
        m_Engine = NULL;
        m_hXAudioDLL = NULL;   /* the pinned module itself stays loaded */
        AbandonCallbackInfra();
        if (bCoInitialized) { CoUninitialize(); bCoInitialized = false; }
        dllInitialized = false;
        return FALSE;
    }

    /* Source voice: 16-bit stereo PCM at the N64 rate.  The rate is
       re-programmed whenever the game changes it.  Created WITHOUT
       XAUDIO2_VOICE_NOPITCH / XAUDIO2_VOICE_NOSRC - SetSourceSampleRate()
       requires exactly that (per the XAudio2 documentation). */
    WAVEFORMATEX wfm;
    memset(&wfm, 0, sizeof(wfm));
    wfm.wFormatTag = WAVE_FORMAT_PCM;
    wfm.nChannels = 2;
    wfm.nSamplesPerSec = (m_SamplesPerSecond != 0) ? m_SamplesPerSecond : 44100;
    wfm.wBitsPerSample = 16;
    wfm.nBlockAlign = (wfm.wBitsPerSample / 8) * wfm.nChannels;
    wfm.nAvgBytesPerSec = wfm.nSamplesPerSec * wfm.nBlockAlign;

    if (FAILED(m_Engine->CreateSourceVoice(&m_Source, &wfm, 0, M_XAUDIO2_DEFAULT_FREQ_RATIO, m_pVoiceCallback, NULL, NULL)))
    {
        DebugMessage(M64MSG_ERROR, "XAudio2: CreateSourceVoice failed");
        m_Master->DestroyVoice();
        m_Master = NULL;
        m_Engine->UnregisterForCallbacks(m_pEngineCallback);
        m_Engine->StopEngine();
        m_Engine->Release();
        m_Engine = NULL;
        m_hXAudioDLL = NULL;   /* the pinned module itself stays loaded */
        AbandonCallbackInfra();
        if (bCoInitialized) { CoUninitialize(); bCoInitialized = false; }
        dllInitialized = false;
        return FALSE;
    }

    m_Source->Start();
    iCurFrequency = (int)wfm.nSamplesPerSec;

    /* Feeder wake-up event: created together with the callback shard
     * BEFORE any engine object existed (SESSION 9) - a CreateEvent
     * failure aborts Setup before the engine is built, so no separate
     * error path is needed here. */

    /* Keep the submission cache size consistent with the current rate
       (original WIP12.1 fix - see SetFrequency()). */
    u32 backendFps = Configuration::getBackendFPS();
    if (backendFps > 0 && wfm.nSamplesPerSec > 0)
        cacheSize = (u32)(wfm.nSamplesPerSec / backendFps) * 4;

    SetVolume(GetVolume());

    return TRUE;
}

/* ---------------------------------------------------------------------------
 * Teardown
 * ------------------------------------------------------------------------ */
void XAudio2SoundDriverModern::Teardown()
{
    if (dllInitialized == false)
    {
        /* Emergency path (destructor without AI_Shutdown): a fallback
         * drain thread must never outlive this object.  Non-virtual
         * base call - safe from a destructor.
         *
         * SESSION 9: retire the callback shard here too - this also
         * covers the "Setup() failed halfway" states (Setup's own error
         * paths already abandoned it, but be idempotent): from this point
         * on, any callback the engine/shim may still dispatch is a cheap
         * no-op that touches only still-mapped shard memory. */
        AbandonCallbackInfra();
        StopFallbackDrain();
        return;
    }

    /* ------------------------------------------------------------------
     * SESSION 8/9 - HARDENED, ORDERED SHUTDOWN (see the class header,
     * points SESSION 8 (a) and SESSION 9 (a)).  The minidump of the
     * "crash after any plugin manipulation" report showed heap
     * free-list corruption that a LATE callback, dispatched by the VxKex
     * shim's worker pool after the engine was released and the object
     * deleted, can produce by writing its 8-byte token into freed
     * memory.  v1.0.16 ordered the shutdown and added a 30 ms grace;
     * v1.0.17 removes the remaining TIMING ASSUMPTION entirely: every
     * callback-reachable allocation now lives in the shard bundle and
     * is never freed, so a preempted worker re-entering at ANY later
     * time is still safe.
     *
     *   1. join the feeder thread FIRST (its stop-wake still uses the
     *      shard's event, which is alive) -> no more submissions;
     *   2. retire the callback shard -> late calls become no-ops that
     *      touch only never-freed memory;
     *   3. Stop -> Flush -> (bounded) queue-drain wait -> DestroyVoice
     *      under m_SubmitMutex (uniform with StopAudio/RecreateSourceVoice)
     *      -> the voice is never destroyed mid-playback (also gentler
     *      to the shim than the old "destroy with a live queue");
     *   4. StopEngine, then a 30 ms politeness pause, then Release the
     *      engine;
     *   5. ABANDON (leak) the callback shard + interface objects -
     *      ~3 KB + one event handle per engine creation stay mapped
     *      until process exit (the price of provable safety);
     *   6. free the slot pool (no callback can ever touch it - tokens
     *      carry slot INDICES, never pointers);
     *   7. the shim DLL stays pinned for the process (never FreeLibrary).
     * ------------------------------------------------------------------ */
    StopAudioThread();

    AbandonCallbackInfra();

    if (m_Source != NULL)
    {
        PluginLockGuard lck(m_SubmitMutex);

        m_Source->Stop();
        m_Source->FlushSourceBuffers();

        /* The documented reset condition (see WaitVoiceQueueEmpty).  At
         * this point the feeder is joined, so the wait cannot race a
         * submission; bounded to 100 ms as everywhere else.  Destroying
         * only from a drained queue is also far gentler to the shim than
         * the old "destroy with a live queue" - a mid-playback destroy
         * is exactly the kind of edge a wrapper implementation gets
         * wrong. */
        if (WaitVoiceQueueEmpty(100) == false)
        {
            M_XAUDIO2_VOICE_STATE xvs;
            memset(&xvs, 0, sizeof(xvs));
            m_Source->GetState(&xvs, M_XAUDIO2_VOICE_NOSAMPLESPLAYED);
            DebugMessage(M64MSG_WARNING,
                "XAudio2.9: voice queue did not drain within 100 ms during shutdown (BuffersQueued=%u) - "
                "destroying the voice anyway; if the next session starts silent this points at the device/APO level",
                (unsigned)xvs.BuffersQueued);
        }

        m_Source->DestroyVoice();
        m_Source = NULL;
    }
    if (m_Master != NULL)
    {
        m_Master->DestroyVoice();
        m_Master = NULL;
    }
    if (m_Engine != NULL)
    {
        m_Engine->UnregisterForCallbacks(m_pEngineCallback);
        m_Engine->StopEngine();

        /* Politeness pause (SESSION 8, kept in SESSION 9): after
         * StopEngine the real engine stops dispatching callbacks
         * entirely; the pause lets a shim worker pool finish calls it
         * already dispatched.  NOTE: this is NO LONGER load-bearing for
         * safety - everything those calls can touch (shard, interface
         * objects, event) stays mapped for the whole process lifetime
         * now - it simply keeps the shim's own bookkeeping clean. */
        Sleep(30);

        m_Engine->Release();
        m_Engine = NULL;
    }

    /* xaudio2_9.dll is PINNED for the whole process (SESSION 8, see
     * Setup()): with a ~40-thread worker pool inside the VxKex shim,
     * unloading its code after Release would be a crash lottery.  Drop
     * only our reference; the module itself stays mapped. */
    m_hXAudioDLL = NULL;
    m_pfnXAudio2Create = NULL;

    /* Free the submission slot pool: safe unconditionally - the feeder
     * is joined and no callback can ever dereference a slot (tokens are
     * VALUES, not pointers).  The callback shard itself is NOT freed -
     * that is the SESSION 9 safety contract (AbandonCallbackInfra above). */
    {
        PluginLockGuard lck(m_SubmitMutex);
        delete[] m_Slots;
        m_Slots = NULL;
        m_SlotCount = 0;
        m_FreeSlotCount = 0;
        m_OutstandingCount = 0;
        m_BytesSubmitted.store(0, std::memory_order_relaxed);
        m_BytesRetired.store(0, std::memory_order_relaxed);
        m_PacingCredit = 0;
    }

    if (bCoInitialized)
    {
        CoUninitialize();
        bCoInitialized = false;
    }

    /* Force a full voice re-init on the next Setup (original WIP12.2
       fix - restarting a ROM that uses the same DACRATE must not skip
       the voice/cacheSize setup). */
    iCurFrequency = 0;
    dllInitialized = false;

    /* Stop the fallback drain last (non-virtual, destructor-safe). */
    StopFallbackDrain();
}

Boolean XAudio2SoundDriverModern::Initialize()
{
    /* Restore the per-submission buffer size when a frequency is
       already known (e.g. after a ROM reset where the game does not
       rewrite DACRATE).  Stale zero here previously produced complete
       silence (original WIP12.1 fix). */
    if (m_SamplesPerSecond > 0 && Configuration::getBackendFPS() > 0)
        cacheSize = (u32)(m_SamplesPerSecond / Configuration::getBackendFPS()) * 4;
    else
        cacheSize = 0;

    return TRUE;
}

void XAudio2SoundDriverModern::DeInitialize()
{
    Teardown();
}

/* ---------------------------------------------------------------------------
 * Slot bookkeeping helpers
 * ------------------------------------------------------------------------ */
void XAudio2SoundDriverModern::ResetSlotsLocked()
{
    /* Caller holds m_SubmitMutex.  Invalidates every outstanding buffer
     * token (generation bump) and returns all slots to the free list.
     * Used after the voice was flushed/recreated: whether or not the
     * engine fires OnBufferEnd for the removed buffers, any late
     * callback fails the generation check and is ignored; the caller
     * resets the byte accounting.  SESSION 5: the outstanding-FIFO is
     * part of the same bookkeeping and is reset with it (slot in FIFO
     * <-> slot busy is an invariant everywhere else). */
    for (u32 i = 0; i < m_SlotCount; i++)
        m_Slots[i].generation.fetch_add(1, std::memory_order_acq_rel);
    m_FreeSlotCount = 0;
    for (u32 i = 0; i < m_SlotCount; i++)
        m_FreeSlots[m_FreeSlotCount++] = i;
    m_OutstandingCount = 0;
}

bool XAudio2SoundDriverModern::SubmitChunk(u32 len)
{
    /* Pull 'len' bytes from the ring buffer (LoadAiBuffer pads with the
     * held last sample on underrun, so playback never clicks) and hand
     * them to the engine as one buffer.  Returns false when the engine
     * refused the buffer or no slot is free; the caller accounts for
     * the chunk only on success.
     *
     * NOTE on locking: m_SubmitMutex is held across the whole
     * submission, which includes LoadAiBuffer() taking the base class
     * ring mutex (m_mutex).  The lock ORDER m_SubmitMutex -> m_mutex is
     * the only order ever used (the core thread's AI_LenChanged /
     * AI_SetFrequency take m_mutex without m_SubmitMutex, and the
     * voice-control paths in SetFrequency/StopAudio take m_SubmitMutex
     * WITHOUT calling LoadAiBuffer), so no inversion is possible.
     * SubmitSourceBuffer() itself never re-enters our callbacks
     * (OnBufferEnd fires on the engine's processing thread), so the
     * non-recursive mutex cannot deadlock here. */

    if (m_Source == NULL || m_Slots == NULL || len == 0 || len > kMaxSegment)
        return false;

    PluginLockGuard lck(m_SubmitMutex);

    if (m_FreeSlotCount == 0)
        return false;                   /* every slot still queued */

    /* FIFO: take the slot that has been free the LONGEST, so a slot that
     * was retired a moment ago (possibly a hair before the engine really
     * stopped reading it) is not overwritten until every other free slot
     * has been used - see the free-list note in the class header. */
    u32 idx = m_FreeSlots[0];
    m_FreeSlotCount--;
    for (u32 i = 0; i < m_FreeSlotCount; i++)
        m_FreeSlots[i] = m_FreeSlots[i + 1];

    u32 pulled = LoadAiBuffer(m_Slots[idx].data, len);
    if (pulled == 0)
    {
        /* Defensive: never submit an empty buffer. */
        m_FreeSlots[m_FreeSlotCount++] = idx;
        return false;
    }

    BufferSlot *slot = &m_Slots[idx];
    u32 generation = slot->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    slot->lastLen = pulled;
    /* SESSION 8: submission timestamp for the reconcile's age gate -
     * written under m_SubmitMutex, read by the heal under the same
     * mutex, so a plain member is sufficient. */
    slot->submitTickMs = GetTickCount64();

    M_XAUDIO2_BUFFER xa2buff;
    memset(&xa2buff, 0, sizeof(xa2buff));
    xa2buff.AudioBytes = pulled;              /* 0 flags: streaming */
    xa2buff.pAudioData = slot->data;
    /* Token: generation in the high 32 bits, slot index in the low 32 -
     * decoded and validated by OnBufferEnd. */
    xa2buff.pContext = (void *)(uintptr_t)(((u64)generation << 32) | (u64)idx);

    if (FAILED(m_Source->SubmitSourceBuffer(&xa2buff, NULL)))
    {
        /* Engine refused the buffer (voice failed underneath us):
         * recycle the slot immediately, do not account for it. */
        m_FreeSlots[m_FreeSlotCount++] = idx;
        return false;
    }

    m_BytesSubmitted.fetch_add(pulled, std::memory_order_acq_rel);

    /* SESSION 5: this slot is now genuinely outstanding inside the
     * engine (SubmitSourceBuffer succeeded).  Track it in the FIFO so
     * HealAccountingWithEngine() can later compare our outstanding count
     * with the engine's own GetState().BuffersQueued. */
    m_OutstandingSlots[m_OutstandingCount++] = idx;
    return true;
}

/* ---------------------------------------------------------------------------
 * OnBufferEnd - engine retired a buffer: defer the bookkeeping.
 * Runs on the engine's callback dispatch context - on the VxKex
 * xaudio2_9 shim that is a WORKER POOL, so several OnBufferEnd calls may
 * run CONCURRENTLY (SESSION 8; the old SPSC assumption lost a token per
 * race).  It therefore takes NO locks and never blocks: the buffer token
 * is pushed onto the lock-free bounded MPMC ring (dropping it only if
 * the ring is full, which needs ~3 seconds of an unresponsive audio
 * thread - the age-gated reconcile then retires the slot anyway) and the
 * feeder is woken via the event.  The audio thread retires the slot when
 * it drains the ring.
 * ------------------------------------------------------------------------ */
void XAudio2SoundDriverModern::VoiceCallbackModern::OnBufferEnd(void *pBufferContext)
{
    XACallbackShard *s = m_shard;
    if (s == NULL)
        return;

    /* SESSION 9: from here on this callback touches ONLY shard memory,
     * which is never freed while the process lives - a worker preempted
     * at ANY point and resuming after the driver object was deleted is
     * still safe.  The retired flag is a fast-path courtesy: after
     * Teardown there is no consumer for the ring anyway. */
    if (s->retired.load(std::memory_order_relaxed))
        return;

    u64 token = (u64)(uintptr_t)pBufferContext;

    /* Vyukov-style bounded MPMC enqueue.  The position is claimed with a
     * CAS loop ONLY after the occupancy check passes, so an enqueue
     * position never advances without a writer committed to it - a
     * dropped token (ring full) leaves no phantom position behind that
     * would stall the consumer's strict-FIFO drain.  Claiming via CAS
     * makes every position unique, so the cell write itself needs no
     * further synchronisation; seq's release store publishes the token.
     * The consumer cursor is reloaded inside the loop, so a full ring is
     * re-tested against the consumer's ACTUAL progress. */
    for (;;)
    {
        u32 e = s->enqueuePos.load(std::memory_order_acquire);
        u32 d = s->dequeuePos.load(std::memory_order_acquire);
        if (e - d >= kXaPendingCount)
            break;                                     /* full: drop token  */

        u32 cell = e % kXaPendingCount;
        if (!s->enqueuePos.compare_exchange_weak(
                e, e + 1u, std::memory_order_acq_rel, std::memory_order_acquire))
            continue;                                  /* another producer  */

        s->tokens[cell] = token;                       /* we own the cell   */
        s->seq[cell].store(e + 1u, std::memory_order_release);
        break;                                         /* published         */
    }

    /* Wake the feeder even when the token was dropped (ring full): the
     * wake also drives the reconcile/drop decisions, not just retires.
     * The event handle belongs to the shard and is never closed while
     * the process lives, so this SetEvent can never hit a recycled
     * handle value. */
    if (s->wakeEvent != NULL)
        SetEvent(s->wakeEvent);
}

/* ---------------------------------------------------------------------------
 * DrainPendingRetires - retire every publishable token (audio thread only,
 * single consumer).  One lock acquisition for the whole batch; tokens whose
 * generation no longer matches (flushed or recycled slots) are ignored.
 * A cell claimed by a producer but not yet published (seq check) stops the
 * drain at that position - order is preserved, the rest follows on the
 * next wake (the producer publishes within nanoseconds of claiming).
 * ------------------------------------------------------------------------ */
void XAudio2SoundDriverModern::DrainPendingRetires()
{
    if (m_CB == NULL)
        return;

    u32 d = m_CB->dequeuePos.load(std::memory_order_relaxed);
    u32 e = m_CB->enqueuePos.load(std::memory_order_acquire);
    if (d == e)
        return;

    PluginLockGuard lck(m_SubmitMutex);

    while (d != e)
    {
        u32 cell = d % kXaPendingCount;
        if (m_CB->seq[cell].load(std::memory_order_acquire) != d + 1u)
            break;      /* claimed but not yet published: strict FIFO order */

        u64 token = m_CB->tokens[cell];
        /* Release the cell for the producer that will claim position
         * d + kXaPendingCount (the classic Vyukov recycle). */
        m_CB->seq[cell].store(d + kXaPendingCount, std::memory_order_release);
        d++;

        u32 idx   = (u32)(token & 0xFFFFFFFFull);
        u32 gen   = (u32)(token >> 32);

        if (m_Slots == NULL || idx >= m_SlotCount)
            continue;

        BufferSlot *slot = &m_Slots[idx];
        /* Recycle only when this buffer is still the CURRENT submission
         * of its slot: a generation mismatch means the token is late
         * (the buffer was flushed or the slot was already recycled).
         * SESSION 5: the outstanding-FIFO doubles as the authoritative
         * "this slot is really busy" set - a token whose slot is not in
         * it is a duplicate/late callback and must NOT free the slot
         * again (a double push into m_FreeSlots would corrupt the
         * pool).  SESSION 9: a normal retirement no longer re-arms the
         * degraded-channel warning latch directly - that re-arm now
         * needs a SUSTAINED healthy streak (HealAccountingWithEngine),
         * because a partially-alive channel re-fired the warning within
         * milliseconds under the old rule. */
        if (slot->generation.load(std::memory_order_acquire) != gen)
            continue;

        bool inFifo = false;
        u32 fifoPos = 0;
        for (u32 i = 0; i < m_OutstandingCount; i++)
        {
            if (m_OutstandingSlots[i] == idx)
            {
                inFifo = true;
                fifoPos = i;
                break;
            }
        }
        if (!inFifo)
            continue;   /* not outstanding per our accounting - duplicate token */
        for (u32 i = fifoPos; i + 1 < m_OutstandingCount; i++)
            m_OutstandingSlots[i] = m_OutstandingSlots[i + 1];
        m_OutstandingCount--;

        m_BytesRetired.fetch_add(slot->lastLen, std::memory_order_acq_rel);
        m_FreeSlots[m_FreeSlotCount++] = idx;
    }

    m_CB->dequeuePos.store(d, std::memory_order_release);
}

/* ---------------------------------------------------------------------------
 * Frequency changes
 * ------------------------------------------------------------------------ */
void XAudio2SoundDriverModern::SetFrequency(u32 Frequency)
{
    if (Setup() == FALSE)
    {
        /* WIN7/VXKEX SLOW-GAME FIX: xaudio2_9.dll may exist (shimmed by
         * VxKex on Windows 7) while the engine does not actually run.
         * With no real consumer the ring buffer stays full and the A/V
         * sync throttle would drag the whole game down to a crawl.
         * Run the real-time fallback drain instead - the game keeps full
         * speed (audio silent) until a later retry succeeds. */
        StartFallbackDrain();
        return;
    }

    /* A real engine is running now - stop the fallback drain if it was
     * active from an earlier failed attempt. */
    StopFallbackDrain();

    /* ALWAYS recompute the per-submission buffer size.  Computing it
       only when the frequency changed left cacheSize == 0 after a ROM
       reset/restart (AI_ResetAudio -> Initialize() zeroes it) when the
       game re-programmed the same DACRATE - the audio thread then
       submitted nothing and the game went completely silent
       (Zelda: OoT, original WIP12.1 fix). */
    u32 backendFps = Configuration::getBackendFPS();
    if (backendFps > 0 && Frequency > 0)
        cacheSize = (u32)(Frequency / backendFps) * 4;
    else
        cacheSize = 0;

    /* m_Source == NULL means an earlier voice (re)creation failed: retry it
     * even when the rate itself did not change, otherwise the driver would
     * stay silent until the game happened to program a different DAC rate. */
    if (iCurFrequency != (int)Frequency || m_Source == NULL)
    {
        /* SESSION 5 FIX - see RecreateSourceVoice() and the class header
         * (point 6).  The old path (Stop -> FlushSourceBuffers ->
         * IMMEDIATE SetSourceSampleRate -> Start, all with m_SubmitMutex
         * released) violated XAudio2's documented protocol twice:
         * (a) Microsoft's FlushSourceBuffers remarks state that calling
         *     SetSourceSampleRate right after Stop+Flush is ILLEGAL
         *     until the flush has actually completed (BuffersQueued == 0
         *     or the flush's OnBufferEnd callbacks arrived) - observed
         *     live in the user's log as XAUDIO2_E_INVALID_CALL
         *     (0x88960001);
         * (b) the concurrent feeder could race SubmitSourceBuffer calls
         *     into the not-yet-completed flush - the docs' "stop a
         *     voice, flush it, then submit new data RESETS ALL OF THE
         *     VOICE'S INTERNAL COUNTERS" undefined zone, in which
         *     submitted buffers can leave the voice queue without an
         *     OnBufferEnd ever reaching the plugin.
         * Together those wedged this driver's OnBufferEnd-based
         * accounting permanently: the user's diagnostic lines showed
         * queued=0 (engine truth: nothing queued) while the plugin's own
         * slot bookkeeping froze and in-flight pegged at 149 ms for the
         * ENTIRE session - total silence with the feeder dropping
         * everything.
         * The transition is now a full, mutex-protected voice swap to a
         * FRESH voice at the new rate.  A freshly created voice is the
         * empirically proven-good state on the affected machine (the
         * pre-DACRATE phase cycled buffers and callbacks correctly; the
         * breakage only ever began at the DACRATE transition), and it
         * cannot carry any post-flush/counter-reset residue. */
        RecreateSourceVoice(Frequency);
    }

    StartAudioThread();
}

/* ---------------------------------------------------------------------------
 * SESSION 5 - the documented voice transition for a DACRATE change
 * ------------------------------------------------------------------------ */
bool XAudio2SoundDriverModern::WaitVoiceQueueEmpty(u32 timeoutMs)
{
    /* Caller holds m_SubmitMutex (the feeder is parked on it, so nothing
     * else can submit while we wait).  This implements Microsoft's own
     * completion condition for FlushSourceBuffers: "A voice's state is
     * not considered reset after calling FlushSourceBuffers until ...
     * GetState returns with XAUDIO2_VOICE_STATE.BuffersQueued == 0".
     * The engine applies the flush asynchronously on its ~10 ms
     * processing quanta, so this normally returns within one or two
     * quanta; the timeout is a defensive bound only. */
    u64 start = GetTickCount64();
    for (;;)
    {
        M_XAUDIO2_VOICE_STATE xvs;
        memset(&xvs, 0, sizeof(xvs));
        m_Source->GetState(&xvs, M_XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (xvs.BuffersQueued == 0)
            return true;
        if ((u32)(GetTickCount64() - start) >= timeoutMs)
            return false;
        Sleep(2);
    }
}

void XAudio2SoundDriverModern::RecreateSourceVoice(u32 Frequency)
{
    /* Runs on the emulator thread (SetFrequency), with m_SubmitMutex
     * held for the WHOLE transition.  That is deliberately different
     * from the old code's "voice calls without the mutex" pattern: the
     * OnBufferEnd callback has been lock-free (SPSC token ring + event,
     * no m_SubmitMutex) since the wall-clock pacing redesign, so
     * holding the mutex here can neither deadlock the engine's callback
     * thread nor be re-entered from one.  What it DOES do is park the
     * audio thread's SubmitChunk()/DrainPendingRetires() for the few
     * milliseconds of the transition, which is exactly what closes the
     * documented "submit new data while the flush is still pending"
     * race that ate this session's buffers. */

    PluginLockGuard lck(m_SubmitMutex);

    if (m_Source != NULL)
    {
        m_Source->Stop();
        m_Source->FlushSourceBuffers();

        /* The documented reset condition - see WaitVoiceQueueEmpty().
         * On failure (a genuinely stuck queue) we log it and tear the
         * voice down anyway: a fresh voice is created below, so any
         * residue dies with the old one. */
        if (WaitVoiceQueueEmpty(100) == false)
        {
            M_XAUDIO2_VOICE_STATE xvs;
            memset(&xvs, 0, sizeof(xvs));
            m_Source->GetState(&xvs, M_XAUDIO2_VOICE_NOSAMPLESPLAYED);
            DebugMessage(M64MSG_WARNING,
                "XAudio2.9: voice queue did not drain within 100 ms after flush (BuffersQueued=%u) - "
                "recreating the voice anyway; if silence persists this line points at the device/APO level",
                (unsigned)xvs.BuffersQueued);
        }

        m_Source->DestroyVoice();
        m_Source = NULL;
    }

    /* Fresh source voice at the NEW rate: none of the old voice's
     * post-flush internal state can survive into it, the source rate
     * matches the PCM the feeder will submit, and the documented
     * SetSourceSampleRate preconditions (and their observed failure,
     * 0x88960001) are sidestepped entirely.  Same parameters as the
     * original creation in Setup(): 16-bit stereo, no NOPITCH/NOSRC,
     * default frequency ratio, our (reused) voice callback. */
    WAVEFORMATEX wfm;
    memset(&wfm, 0, sizeof(wfm));
    wfm.wFormatTag = WAVE_FORMAT_PCM;
    wfm.nChannels = 2;
    wfm.nSamplesPerSec = Frequency;
    wfm.wBitsPerSample = 16;
    wfm.nBlockAlign = (wfm.wBitsPerSample / 8) * wfm.nChannels;
    wfm.nAvgBytesPerSec = wfm.nSamplesPerSec * wfm.nBlockAlign;

    if (FAILED(m_Engine->CreateSourceVoice(&m_Source, &wfm, 0, M_XAUDIO2_DEFAULT_FREQ_RATIO, m_pVoiceCallback, NULL, NULL)))
    {
        DebugMessage(M64MSG_ERROR,
            "XAudio2.9: CreateSourceVoice(%u Hz) failed during the rate change - the feeder stays idle and "
            "the change is retried on the next SetFrequency/StartAudio",
            (unsigned)Frequency);
        m_Source = NULL;
        /* iCurFrequency deliberately NOT updated: the next call retries. */
    }
    else
    {
        m_Source->Start();
        iCurFrequency = (int)Frequency;
        SetVolume(GetVolume());   /* fresh voice starts at unity: reapply */
    }

    /* Everything the old voice still held died with it (DestroyVoice
     * discards its queue WITHOUT delivering the pending OnBufferEnd
     * callbacks), and any token already sitting in the pending ring will
     * fail the generation check below.  Resync the whole engine-side
     * accounting with engine truth: zero in-flight, every slot free.
     * The base class cleared the ring buffer before calling
     * SetFrequency (AI_SetFrequency resets it on a rate change), so
     * nothing is owed yet. */
    m_BytesRetired.store(m_BytesSubmitted.load(std::memory_order_acquire),
                         std::memory_order_release);
    ResetSlotsLocked();
    m_PacingCredit = 0;

    /* CRACKLE FIX: this whole function just blocked the audio thread on
     * m_SubmitMutex for up to ~100 ms. Tell it to re-anchor its
     * wall-clock reference instead of charging that blocked interval
     * as "audio owed" on its very next iteration (see m_ResyncPacingClock
     * in the header for the full chain to the audible symptom). */
    m_ResyncPacingClock.store(true, std::memory_order_release);
}

/* ---------------------------------------------------------------------------
 * SESSION 7 (v1.0.15, restored) / SESSION 8 (v1.0.16, age gate) -
 * continuous reconcile of the plugin's retirement accounting with the
 * engine's own queue depth (audio thread only, ~16 ms cadence)
 * ------------------------------------------------------------------------ */
u32 XAudio2SoundDriverModern::HealMinAgeMs() const
{
    /* SESSION 8: the reconcile may only retire a slot whose buffer has
     * had time to physically finish playing even on an engine running a
     * full 1x BEHIND real time (which would be loudly announced by the
     * "engine consumes slower than real time" drop warning).  A chunk of
     * cacheSize bytes at the current rate lasts cacheSize/(freq*4)
     * seconds; twice that is a sound upper bound on when its PCM is no
     * longer being read.  The 40 ms floor keeps small-chunk
     * configurations safe against GetState snapshots that lag by a
     * couple of engine quanta.  Audio thread only. */
    u32 freq = (iCurFrequency > 0) ? (u32)iCurFrequency : 44100;
    u64 chunkMs = 0;
    if (freq > 0 && cacheSize > 0)
        chunkMs = (u64)cacheSize * 1000ull / ((u64)freq * 4ull);

    u32 minAge = 40;
    if (chunkMs * 2ull > (u64)minAge)
        minAge = (u32)(chunkMs * 2ull);
    return minAge;
}

void XAudio2SoundDriverModern::HealAccountingWithEngine()
{
    if (m_Source == NULL || m_Slots == NULL)
        return;

    /* Snapshot the engine state under m_SubmitMutex so no submission can
     * interleave: 'queued' and our FIFO are then a consistent pair, and the
     * comparison below cannot mistake a buffer the engine has not yet
     * counted for one it has already retired. */
    PluginLockGuard lck(m_SubmitMutex);

    if (m_Source == NULL || m_Slots == NULL)
        return;

    M_XAUDIO2_VOICE_STATE xvs;
    memset(&xvs, 0, sizeof(xvs));
    m_Source->GetState(&xvs, M_XAUDIO2_VOICE_NOSAMPLESPLAYED);

    const u32 ours = m_OutstandingCount;
    const u32 safetyMargin = 2;

    if (ours <= xvs.BuffersQueued + safetyMargin)
    {
        /* Engine and plugin are close enough: a buffer that just completed
         * but whose OnBufferEnd token is still sitting in the pending ring,
         * or the engine's ~10 ms processing quantum, can legitimately skew
         * the two counters by one or two.  Do not touch the pool.
         *
         * SESSION 9: a sustained run of these clean checks (~1 s) is the
         * only thing that re-arms the degraded-channel warning latch -
         * see the latch comment in the header.  A single surviving
         * retirement proved nothing on the VxKex machine, whose channel
         * is only PARTIALLY alive. */
        m_HealthyHealStreak++;
        if (m_HealthyHealStreak >= 64)
        {
            m_HealthyHealStreak = 0;
            m_ReconcileHealedLogged = false;   /* genuinely healthy again */
        }
        return;
    }
    m_HealthyHealStreak = 0;

    /* Buffers the engine has already fully consumed without delivering
     * OnBufferEnd (the callback-dead-channel case documented in the file
     * header, point 7).  XAudio2 retires a voice's buffers strictly in
     * submission order, so the engine holds the NEWEST 'queued' entries of
     * our FIFO; everything OLDER than queued+safetyMargin is finished -
     * Microsoft's OnBufferEnd contract ("the buffer can now be reused or
     * destroyed") applies to them even though the callback itself never
     * arrived.  Retire exactly that excess, from the FRONT of the FIFO, so
     * the margin always leaves the buffers the engine may still be reading
     * untouched (no crackle, no recycled live PCM). */
    const u32 healCount = ours - (xvs.BuffersQueued + safetyMargin);

    /* Visible but non-spamming diagnostic (SESSION 9 wording): a degraded
     * callback channel is a property of the MACHINE (Windows 7 / VxKex
     * xaudio2_9 shim, APO quirk), not of one buffer and not of this
     * plugin version.  The line answers the natural question "why is
     * this in my log" right in the log itself: retirement is paced from
     * the engine's own queue state instead of the lost callbacks, audio
     * and emulation speed are unaffected, and no action is needed.
     * Re-armed only by a sustained healthy streak (see above). */
    if (!m_ReconcileHealedLogged)
    {
        m_ReconcileHealedLogged = true;
        DebugMessage(M64MSG_WARNING,
            "XAudio2.9: OnBufferEnd delivery degraded on this machine (engine=%u queued, plugin=%u outstanding) - "
            "retirement is paced from the engine's own queue state instead; audio and emulation speed are unaffected; "
            "typical on Windows 7 / VxKex xaudio2_9 shims, no action needed",
            (unsigned)xvs.BuffersQueued, (unsigned)ours);
    }

    /* SESSION 8 - AGE GATE: GetState on a shim can UNDER-REPORT (the
     * minidump machine shows engine=0 while its buffers are still
     * audible), so the margin alone is not proof that the oldest
     * outstanding buffer finished playing.  The FIFO is ordered by
     * submission time, so a single age check on its FRONT slot bounds
     * every slot behind it: if the OLDEST outstanding slot is younger
     * than HealMinAgeMs(), nothing in the FIFO is safely retirable yet
     * and the loop breaks on its first iteration.  A slot retired here
     * has both exceeded the margin AND outlived any conceivable
     * playback of its PCM - recycled live audio is now physically
     * impossible, whichever lie GetState told. */
    const u64 nowMs = GetTickCount64();
    const u64 minAgeMs = (u64)HealMinAgeMs();

    for (u32 i = 0; i < healCount && m_OutstandingCount > 0; i++)
    {
        u32 idx = m_OutstandingSlots[0];

        if (nowMs - m_Slots[idx].submitTickMs < minAgeMs)
            break;   /* oldest outstanding slot is too young to retire
                      * safely - everything behind it is younger still */

        for (u32 j = 0; j + 1 < m_OutstandingCount; j++)
            m_OutstandingSlots[j] = m_OutstandingSlots[j + 1];
        m_OutstandingCount--;

        /* Same retirement accounting as a real OnBufferEnd token: credit
         * the bytes and recycle the slot.  A late real token for this slot
         * is rejected by the FIFO-membership check in DrainPendingRetires
         * (and by the generation check if the slot was resubmitted in the
         * meantime), so it can never be retired twice. */
        m_BytesRetired.fetch_add(m_Slots[idx].lastLen, std::memory_order_acq_rel);
        m_FreeSlots[m_FreeSlotCount++] = idx;
    }
}

/* ---------------------------------------------------------------------------
 * Audio thread - wall-clock paced feeder
 * ------------------------------------------------------------------------ */
void XAudio2SoundDriverModern::AudioThreadLoop()
{
    /* Wakes on OnBufferEnd (instant refill when the engine retires a
     * buffer) or on a 4 ms heartbeat, and feeds the engine from the
     * ring buffer strictly at wall-clock rate.  The only engine query
     * in the loop is the cheap NOSAMPLESPLAYED GetState of the ~16 ms
     * engine-truth reconcile below (1b) - the expensive SamplesPlayed
     * computation is never requested.  See the class header for the
     * full design rationale. */

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    const u64 qpf = xa2_qpc_freq();
    u64 lastQpc = xa2_qpc_now();
    u64 startTick = GetTickCount64();      /* idle-guard reference (see below) */
    bool stuckWarned = false;
    u64 lastHealTickMs = 0;                /* engine-truth reconcile cadence */
    u32 dryStreak = 0;                     /* consecutive dry-ring wakes   */
    u32 paddedChunks = 0;                  /* diagnostics: chunks that had to be padded */
    u32 lastEngineQueued = 0xFFFFFFFFu;    /* diagnostics: last engine queue depth */
    u64 lastPadReportMs = GetTickCount64();/* diagnostics: report cadence  */

    while (bStopAudioThread == false)
    {
        /* Heartbeat / wake-up: 4 ms normally, 1 ms when the user asked
         * for the lowest possible yielding (DISALLOW_SLEEP_XA2).
         * SESSION 9: the event belongs to the callback shard. */
        DWORD timeout = (Configuration::getDisallowSleepXA2() == false) ? 4 : 1;
        if (m_CB != NULL && m_CB->wakeEvent != NULL)
            WaitForSingleObject(m_CB->wakeEvent, timeout);
        else
            Sleep(timeout);

        if (bStopAudioThread == true)
            break;

        /* Idle guard: if the feeder never becomes operational (e.g.
         * cacheSize permanently 0 because BackendFPS/SamplesPerSecond
         * never resolved), say so once instead of silently idling -
         * separating a configuration problem from a device-level one
         * must not require a debugger.  SESSION 9: deviceLost lives in
         * the shard now. */
        bool deviceLost = (m_CB != NULL) ? m_CB->deviceLost.load(std::memory_order_relaxed) : false;
        if (m_Source == NULL || deviceLost || cacheSize == 0 || cacheSize > kMaxSegment)
        {
            if (!stuckWarned && (GetTickCount64() - startTick) >= 2000)
            {
                stuckWarned = true;
                DebugMessage(M64MSG_WARNING,
                    "XAudio2.9: audio thread is alive but idle (source=%s, deviceLost=%s, cacheSize=%u) - "
                    "no audio is being submitted to the engine at all; check BACKEND_FPS and the game's DAC rate",
                    (m_Source != NULL) ? "ok" : "NULL", deviceLost ? "true" : "false", (unsigned)cacheSize);
            }
            continue;
        }

        /* --- 1. Retire everything the engine reported since the last
         * wake (OnBufferEnd pushed the tokens lock-free).  This also
         * wakes instantly via the event, so a healthy engine is fed
         * within microseconds of freeing a slot. -------------------- */
        DrainPendingRetires();

        /* --- 1a. CRACKLE FIX: re-anchor the wall clock after being
         * blocked (RecreateSourceVoice() holds m_SubmitMutex for the
         * whole DACRATE-change transition, including up to a 100 ms
         * flush wait).  Without this, the very next 'due' calculation
         * below would include that blocked interval, manufacture a
         * large one-shot pacing credit, and let the burst-guard slam
         * several chunks into the brand-new voice before GetState()
         * can register them - HealAccountingWithEngine() then mistakes the
         * still-playing burst for buffers that lost their OnBufferEnd
         * and frees their slots while the engine is still reading from
         * them (a slot's memory gets overwritten mid-playback: an
         * audible crackle at every DACRATE change).  Skip exactly one
         * due-time accrual so the blocked interval is never charged. */
        if (m_ResyncPacingClock.exchange(false, std::memory_order_acq_rel))
        {
            lastQpc = xa2_qpc_now();
            continue;
        }

        /* --- 1b. SESSION 7 (v1.0.15, restored): continuous engine-truth
         * reconcile, ~16 ms cadence.  OnBufferEnd (handled above) remains
         * the primary, lowest-latency retirement signal; this heal only
         * closes the gap a (partially) dead callback channel would
         * otherwise leave.  Without it m_BytesRetired freezes, the
         * in-flight cap closes the submission gates, the ring overfills
         * and the A/V sync throttle drags the whole emulation below real
         * time - the exact slowdown this backend's wall-clock pacing was
         * designed to prevent (see the file header, point 7).
         *
         * The heal itself is strictly conservative: it retires only the
         * oldest outstanding slots beyond GetState().BuffersQueued + 2,
         * so a slot whose buffer the engine still counts as queued - or
         * whose OnBufferEnd token is merely still in flight - is never
         * recycled.  It uses the cheap NOSAMPLESPLAYED GetState variant
         * (the same call the proven XAudio 2.7 feeder makes ~1000x/sec;
         * the expensive SamplesPlayed computation is never requested).
         * On a healthy machine ours <= queued+2 essentially always and
         * this block is a no-op.  There is deliberately NO voice
         * recreation here: a recreate discards live audio and never
         * restored the callback channel anyway (the v1.0.13/1.0.14
         * regression proved that 27 times in a single session log). */
        {
            u64 nowMs = GetTickCount64();
            if (lastHealTickMs == 0 || nowMs - lastHealTickMs >= 16)
            {
                lastHealTickMs = nowMs;
                HealAccountingWithEngine();
            }
        }

        /* --- 2. How much audio does the wall clock owe? --------------- */
        u64 nowQpc = xa2_qpc_now();
        u64 elapsedTicks = nowQpc - lastQpc;
        lastQpc = nowQpc;
        if (elapsedTicks > qpf / 4)              /* >250 ms: a stall     */
            elapsedTicks = qpf / 4;

        u32 freq = (iCurFrequency > 0) ? (u32)iCurFrequency : 44100;

        u32 speedFactor = 100;
        {
            /* Fast-forward / slow-motion scales the drain rate. */
            PluginLockGuard lck(m_mutex);
            speedFactor = (m_SpeedFactor != 0) ? m_SpeedFactor : 100;
        }

        /* due = elapsed * rate * 4 * speed / 100 (bytes).  u64 math:
         * 2.5e6 ticks * 192000 Hz * 4 = 1.9e12 - fits comfortably. */
        u64 due = (u64)elapsedTicks * (u64)freq * 4ull * (u64)speedFactor
                  / qpf / 100ull;
        m_PacingCredit += (s64)due;

        /* --- 3. Feed the engine ---------------------------------------- */
        u64 inflightLimit = (u64)freq * 4ull * (u64)kMaxInflightMs / 1000ull;
        /* SESSION 8: the chunk-count floor rose from 4 to 6 so the
         * age-gated reconcile's bounded extra lag (up to 2 chunks of age
         * + the 2-slot margin) can never close the submission gates,
         * at any BACKEND_FPS.  For the default 150 ms time budget this
         * only matters for large-chunk configurations (a small-chunk
         * session never comes near either bound). */
        if (inflightLimit < (u64)cacheSize * 6)
            inflightLimit = (u64)cacheSize * 6;  /* always >= 6 chunks   */

        /* Comfort queue: keep ~3 chunks inside the engine so scheduler
         * jitter can never underrun the voice.  Topping it up may drive
         * the pacing credit slightly negative (the clock repays it); in
         * equilibrium submissions still happen at exactly 1x real time. */
        const u64 queueFloor = (u64)cacheSize * 3ull;

        u64 submittedNow = m_BytesSubmitted.load(std::memory_order_acquire);
        u64 retiredNow   = m_BytesRetired.load(std::memory_order_acquire);
        u64 inflight = (submittedNow >= retiredNow) ? (submittedNow - retiredNow) : 0;

        /* SESSION 7: dry-ring guard.  LoadAiBuffer pads a short pull
         * with the held last sample - right for bridging scheduler
         * jitter, but when the ring is genuinely dry (emulation
         * paused, true silence) a whole chunk of held-sample padding
         * is one constant 4-byte sample repeated: an audible DC buzz,
         * while the feeder would still account for padding it never
         * really played.  After 3 consecutive
         * dry wakes stop feeding until real data returns - the device
         * runs dry, which is real silence, and the engine's comfort
         * queue covers the device-side transition. */
        u32 ringAvail = 0;
        {
            {
                PluginLockGuard lck(m_mutex);
                ringAvail = m_BufferRemaining;
            }
            /* A partially-filled ring is normal: AI DMA callbacks do not
             * have to align to our BackendFps chunk boundary.  Treating
             * "less than one whole chunk" as dry can therefore stop the
             * XAudio2.9 feeder for several wake-ups while the engine's
             * comfort queue drains, producing a real output gap/click.
             * LoadAiBuffer() already has the deliberate last-sample hold for
             * a short pull, so only a genuinely empty ring should trip this
             * anti-buzz guard. */
            if (ringAvail == 0)
                dryStreak++;
            else
                dryStreak = 0;
        }
        const bool ringDry = (dryStreak >= 3);

        if (ringDry)
        {
            /* Forgive the wall-clock credit accrued while the game was
             * silent: the clock cannot demand audio the game never
             * produced.  Keeping a large credit would later either be
             * spent as a burst of padding or, once it creeps past the
             * in-flight limit, discard real audio from the ring. */
            if (m_PacingCredit > (s64)cacheSize)
                m_PacingCredit = (s64)cacheSize;
        }

        /* Engine truth: how many buffers the voice really holds right now
         * (the classic XAudio2 2.7 driver, which is clean on the same
         * machine, makes exactly this decision on every wake).  The plugin's
         * own in-flight accounting can lag the engine by several chunks
         * whenever OnBufferEnd is late or missing (the shim case).  A user
         * log from such a machine showed 7-10 buffers sitting in the voice
         * and 6-7 held-sample padded chunks per report window while the
         * 2.7 driver was clean, so the refill decision now follows the
         * engine's own queue.  If the snapshot is unavailable the previous
         * credit/accounting gates are used unchanged. */
        u32 engineQueued = 0xFFFFFFFFu;
        {
            PluginLockGuard lck(m_SubmitMutex);
            if (m_Source != NULL)
            {
                M_XAUDIO2_VOICE_STATE xvs;
                memset(&xvs, 0, sizeof(xvs));
                m_Source->GetState(&xvs, M_XAUDIO2_VOICE_NOSAMPLESPLAYED);
                engineQueued = xvs.BuffersQueued;
            }
        }
        const bool engineTruthKnown = (engineQueued != 0xFFFFFFFFu);
        const u32  kComfortChunks = 3;   /* same depth as the 2.7 driver */
        u32 queuedNow = engineTruthKnown ? engineQueued : 0;
        lastEngineQueued = engineQueued;

        u32 chunksThisWake = 0;
        while (bStopAudioThread == false)
        {
            if (ringDry)
                break;   /* genuinely dry ring: no held-sample padding */

            bool creditOk = (m_PacingCredit >= (s64)cacheSize) &&
                            (inflight + cacheSize <= inflightLimit);
            bool primeOk  = (inflight + cacheSize <= queueFloor);

            bool feed;
            if (engineTruthKnown)
            {
                /* Pull model: refill only while the engine itself holds
                 * fewer than kComfortChunks buffers.  The wall clock acts
                 * as a rate limiter (a chunk is taken when one is due) and
                 * the engine queue dropping below 2 chunks is urgent
                 * enough to refill regardless of the clock, so a device
                 * that runs a hair faster than the QPC can never starve.
                 * The in-flight limit stays as a hard upper bound in case
                 * a GetState snapshot ever under-reports. */
                bool room = (inflight + cacheSize <= inflightLimit);
                feed = room && (queuedNow < kComfortChunks) &&
                       (creditOk || (queuedNow + 1 < kComfortChunks));
            }
            else
            {
                feed = creditOk || primeOk;
            }

            if (!feed)
                break;
            if (++chunksThisWake > 8)            /* burst guard          */
                break;

            if (SubmitChunk(cacheSize))
            {
                m_PacingCredit -= (s64)cacheSize;   /* may go negative   */
                inflight += cacheSize;
                queuedNow++;

                /* Diagnostics only: a chunk pulled from a ring that held
                 * less than a whole chunk was completed with the held last
                 * sample (LoadAiBuffer's underrun hold) - a source-side gap
                 * the engine cannot repair. */
                if (ringAvail >= cacheSize)
                    ringAvail -= cacheSize;
                else
                {
                    ringAvail = 0;
                    paddedChunks++;
                }
            }
            else
            {
                /* Engine refused (voice stopped?) or every slot is
                 * still queued: stop feeding, the drop path below keeps
                 * the wall-clock pacing alive. */
                break;
            }
        }

        /* A device that runs slightly faster than the QPC makes the credit
         * go (slowly) negative through the urgent refills; bound the debt
         * so a later stall cannot take unboundedly long to be noticed. */
        if (m_PacingCredit < -(s64)inflightLimit)
            m_PacingCredit = -(s64)inflightLimit;

        /* --- 4. Engine fell behind: drop instead of stalling ---------- *
         * The wall clock says data is due but the engine cannot take it
         * (in-flight pegged at its limit, or it stopped accepting
         * buffers).  Discard the excess from the head of the ring buffer
         * so the A/V sync throttle in AI_LenChanged() keeps the emulator
         * at full speed - the game must never wait for a slow backend
         * (this is exactly what the Project64 integration does).  The
         * audio output degrades instead: the engine keeps playing what
         * it already has, with periodic gaps. */
        if (m_PacingCredit > (s64)(inflightLimit + (u64)cacheSize))
        {
            u32 drop = (u32)(m_PacingCredit - (s64)inflightLimit) & ~3u;
            if (drop >= cacheSize)
            {
                /* SESSION 7: charge the pacing credit in full (that is
                 * the pacing decision) but count only the bytes that
                 * REALLY left the ring - a drop request against a
                 * short/empty ring discards less than asked, and the
                 * old accounting reported "dropped 21 KB/s" even while
                 * the emulator sat paused with an empty ring. */
                ConsumeBuffered(drop);
                m_PacingCredit -= (s64)drop;

                if (!m_LagWarned)
                {
                    m_LagWarned = true;
                    DebugMessage(M64MSG_WARNING,
                        "XAudio2.9: engine consumes slower than real time - dropping excess audio to keep emulation at full speed");
                }
            }
        }

        /* Diagnostics: at most one line per 5 s.  It separates a source-side
         * gap (the emulator did not deliver audio in time: below-real-time
         * emulation, a pause, a loading screen) from an XAudio2-side
         * problem when a click is heard. */
        if (paddedChunks != 0)
        {
            u64 reportNowMs = GetTickCount64();
            if (reportNowMs - lastPadReportMs >= 5000)
            {
                DebugMessage(M64MSG_INFO,
                    "XAudio2.9: %u audio chunk(s) were padded with the last sample: the audio ring held less than "
                    "one chunk while the engine queue (now %u buffer(s)) had to be refilled "
                    "(emulation below real time, pause or loading)",
                    (unsigned)paddedChunks, (unsigned)lastEngineQueued);
                paddedChunks = 0;
                lastPadReportMs = reportNowMs;
            }
        }
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
}

DWORD WINAPI XAudio2SoundDriverModern::AudioThreadProc(LPVOID lpParameter)
{
    /* x64-safe cast: LPVOID is pointer-sized. */
    XAudio2SoundDriverModern* driver = (XAudio2SoundDriverModern*)(uintptr_t)lpParameter;
    driver->AudioThreadLoop();
    return 0;
}

void XAudio2SoundDriverModern::StartAudioThread()
{
    if (hAudioThread == NULL && dllInitialized == true)
    {
        /* Allocate the submission slot pool on first use.  The pool size
         * is derived from the current chunk size and the in-flight cap
         * (SESSION 8: 6-chunk floor, same as the feeder's gate), so the
         * engine can always sit kMaxInflightMs behind without a slot
         * ever being recycled early. */
        if (m_Slots == NULL)
        {
            u32 freq = (iCurFrequency > 0) ? (u32)iCurFrequency : 44100;
            u64 inflightLimit = (u64)freq * 4ull * (u64)kMaxInflightMs / 1000ull;
            u64 chunk = (cacheSize > 0) ? (u64)cacheSize : 4096ull;
            if (inflightLimit < chunk * 6)
                inflightLimit = chunk * 6;
            u32 slots = (u32)(inflightLimit / chunk) + 4;
            if (slots < 8)  slots = 8;
            if (slots > kMaxSlotPool) slots = kMaxSlotPool;

            m_Slots = new(std::nothrow) BufferSlot[slots];
            if (m_Slots == NULL)
            {
                DebugMessage(M64MSG_ERROR, "XAudio2.9: slot pool allocation failed (%u slots) - running silent real-time drain", (unsigned)slots);
                StartFallbackDrain();
                return;
            }
            m_SlotCount = slots;
            {
                PluginLockGuard lck(m_SubmitMutex);
                for (u32 i = 0; i < m_SlotCount; i++)
                {
                    m_Slots[i].generation.store(1, std::memory_order_relaxed);
                    m_Slots[i].lastLen = 0;
                    m_Slots[i].submitTickMs = 0;
                }
                ResetSlotsLocked();   /* also clears m_OutstandingSlots */
            }
            /* SESSION 9: the MPMC token ring lives in the callback shard
             * and was initialised with it in Setup() - nothing to reset
             * here. */

            m_BytesSubmitted.store(0, std::memory_order_relaxed);
            m_BytesRetired.store(0, std::memory_order_relaxed);
            m_LagWarned = false;
            m_PacingCredit = 0;
            m_ReconcileHealedLogged = false;
            m_HealthyHealStreak = 0;
        }

        bStopAudioThread = false;
        hAudioThread = CreateThread(NULL, 0, AudioThreadProc, this, 0, NULL);
        if (hAudioThread == NULL)
            DebugMessage(M64MSG_ERROR, "XAudio2: CreateThread failed (%lu)", GetLastError());
    }
}

void XAudio2SoundDriverModern::StopAudioThread()
{
    if (hAudioThread != NULL)
    {
        bStopAudioThread = true;
        if (m_CB != NULL && m_CB->wakeEvent != NULL)
            SetEvent(m_CB->wakeEvent);            /* wake the feeder now  */
        DWORD result = WaitForSingleObject(hAudioThread, 5000);
        if (result != WAIT_OBJECT_0)
        {
            TerminateThread(hAudioThread, 0);
            DebugMessage(M64MSG_WARNING, "XAudio2: audio thread had to be terminated");
        }
        CloseHandle(hAudioThread);
    }
    hAudioThread = NULL;
    bStopAudioThread = false;
}

/* ---------------------------------------------------------------------------
 * Start / stop
 * ------------------------------------------------------------------------ */
void XAudio2SoundDriverModern::StopAudio()
{
    StopAudioThread();

    if (m_Source != NULL)
    {
        /* SESSION 5: the feeder is already stopped above, so nothing can
         * race a submission into this flush - but the flush itself is
         * still applied asynchronously by the engine, and Microsoft's
         * docs are explicit that a voice is only "reset" once
         * GetState().BuffersQueued == 0.  Waiting for that condition
         * here (bounded, engine quanta are ~10 ms) guarantees the next
         * StartAudio()'s fresh submissions cannot be swallowed by a
         * still-pending flush.  m_SubmitMutex is held for the wait +
         * reset for uniformity with RecreateSourceVoice(); nobody can
         * contend it at this point (the only other taker was the feeder
         * thread, and StopAudioThread() already joined it). */
        PluginLockGuard lck(m_SubmitMutex);

        m_Source->Stop();
        m_Source->FlushSourceBuffers();

        if (WaitVoiceQueueEmpty(100) == false)
        {
            M_XAUDIO2_VOICE_STATE xvs;
            memset(&xvs, 0, sizeof(xvs));
            m_Source->GetState(&xvs, M_XAUDIO2_VOICE_NOSAMPLESPLAYED);
            DebugMessage(M64MSG_WARNING,
                "XAudio2.9: voice queue did not drain within 100 ms after the stop/flush (BuffersQueued=%u) - "
                "stopping anyway; if audio stays silent after resume this points at the device/APO level",
                (unsigned)xvs.BuffersQueued);
        }

        m_BytesRetired.store(m_BytesSubmitted.load(std::memory_order_acquire),
                             std::memory_order_release);
        ResetSlotsLocked();
        m_PacingCredit = 0;
    }
}

void XAudio2SoundDriverModern::StartAudio()
{
    if (Setup() == FALSE)
    {
        /* See SetFrequency(): dead backend -> silent real-time drain so
         * the A/V sync throttle keeps the game at full speed. */
        StartFallbackDrain();
        return;
    }

    StopFallbackDrain();

    if (m_Source != NULL)
        m_Source->Start();

    StartAudioThread();
}

/* ---------------------------------------------------------------------------
 * Volume: XAudio2 SetVolume - 0.0 silent, 1.0 full
 *
 * VOLUME: plain unity at 100% (0% silent, 50% half amplitude).  A
 * previous session baked a x1.4 boost into the voice level to chase the
 * original AziAudio-Plus's perceived loudness on a specific reporter's
 * machine.  "The engine saturates the mix at the mastering stage" is
 * just clipping under another name: pushing the voice level above unity
 * hard-limits the mastering output on any louder passage, audible as
 * crackle/distortion - confirmed by ear.  A correct, undistorted unity
 * gain is worth more than matching a boost that was itself a defect in
 * whatever it was chasing.  Removed, matching WASAPI's SetVolume above
 * for the same reason.
 * ------------------------------------------------------------------------ */
void XAudio2SoundDriverModern::SetVolume(u32 volume)
{
    SoundDriver::SetVolume(volume);

    float xaVolume = (float)GetVolume() / 100.0f;
    if (m_Source != NULL)
        m_Source->SetVolume(xaVolume);
}

#endif /* ENABLE_BACKEND_XAUDIO2_MODERN */
