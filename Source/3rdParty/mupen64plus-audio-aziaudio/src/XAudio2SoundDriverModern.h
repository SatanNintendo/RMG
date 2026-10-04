/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* XAudio2SoundDriverModern - true XAudio 2.9 backend (xaudio2_9.dll)        *
*                                                                           *
* Loads xaudio2_9.dll dynamically (Windows 10/11) and resolves              *
* XAudio2Create via GetProcAddress.  The classic XAudio 2.7 COM class is    *
* NOT used, so this driver works on any clean modern Windows install.      *
*                                                                           *
* ---------------------------------------------------------------------------
* WALL-CLOCK PACING REDESIGN (fixes "sound works but the whole game runs    *
* very slow", seen with xaudio2_9.dll on some machines)                     *
* ---------------------------------------------------------------------------
* The old pull loop kept 3 buffers queued and pulled a new cacheSize        *
* chunk from the ring buffer only when IXAudio2SourceVoice::GetState()      *
* reported that the engine had retired one.  That ties the rate at which    *
* the ring buffer drains to the rate at which the XAudio 2.9 engine         *
* consumes its internal queue.  SoundDriver::AI_LenChanged() throttles the  *
* ENTIRE EMULATION to the ring-buffer drain rate - so if the engine's       *
* queue advances slower than real time (observed on real machines with      *
* xaudio2_9.dll: driver/APO quirks, engine-thread starvation caused by      *
* our own TIME CRITICAL thread hammering the synchronised GetState() call  *
* ~1000x/sec, etc.), the whole game - video included - slows down to the    *
* engine's pace.  The XAudio 2.7, WASAPI, DirectSound and waveOut backends  *
* drain in real time on the same machines, which is why only this backend   *
* showed the problem.  (This also answers "why does it work in Project64": *
* the zilmar-spec WIP12 plugin never couples emulation speed to the         *
* backend's consumption rate - a slow backend just drops audio there.)      *
*                                                                           *
* The redesigned loop measures nothing about the engine while feeding:      *
*                                                                           *
*   1. WALL-CLOCK PACING: the ring buffer is drained at exactly real time  *
*      (QueryPerformanceCounter), regardless of how fast the engine         *
*      consumes.  The A/V sync throttle in AI_LenChanged() then paces the   *
*      emulator to the wall clock, never to the engine.                     *
*                                                                           *
*   2. EVENT-DRIVEN FEEDING: OnBufferEnd() (with the buffer's slot token    *
*      passed through M_XAUDIO2_BUFFER::pContext) wakes the feeder thread   *
*      the moment the engine retires a buffer, so a healthy engine is fed   *
*      instantly.  SESSION 7: a cheap NOSAMPLESPLAYED GetState IS polled    *
*      once per ~16 ms for the continuous reconciliation (point 7) - the    *
*      same cadence the XAudio 2.7 feeder (GetState-driven, proven on      *
*      the affected machine) has always used; the expensive SamplesPlayed  *
*      computation (documented as ~3x the cost of a NOSAMPLESPLAYED        *
*      state query) is still never requested.                               *
*                                                                           *
*   7. SESSION 7 - CONTINUOUS ENGINE-TRUTH RECONCILIATION: the old          *
*      emergency-only GetState cross-check ("OnBufferEnd silent for 48 ms  *
*      AND no submission in the last 64 ms") could never fire while the    *
*      feeder was actively feeding - its own no-recent-submission safety   *
*      gate excluded exactly the state in which a partially dead            *
*      OnBufferEnd channel accumulates phantom in-flight debt.  The debt   *
*      froze at the whole 150 ms budget, capped submissions at ~75% of     *
*      real time (audible gaps, the ring overfilling to ~150-180 ms), and  *
*      with the A/V sync throttle enabled dragged the ENTIRE emulation to  *
*      ~56% speed ("everything slows down, audio and video together").    *
*      The reconcile now runs every ~16 ms, snapshots the engine state     *
*      under m_SubmitMutex, and heals only beyond BuffersQueued + 2, so    *
*      it can never recycle a slot the engine still holds.  A dry-ring     *
*      guard additionally stops feeding held-sample padding (an audible    *
*      DC buzz while paused) after ~3 dry wakes, and the drop accounting   *
*      counts only bytes that really left the ring.                         *
*                                                                           *
*   SESSION 7 RESTORED (v1.0.15): the 1.0.13 "diagnostics cleanup"         *
*   accidentally rewrote this reconcile from the continuous healing form   *
*   into a ~500 ms watchdog that RECREATED the source voice instead of     *
*   healing the accounting.  On a machine with a dead OnBufferEnd channel  *
*   that reintroduced exactly the phantom-debt cascade documented above,   *
*   plus a recreation storm that discarded live audio every cycle (user    *
*   log: 27x "sustained queue/accounting mismatch (engine=0 queued,        *
*   plugin=9 outstanding) - recreating source voice", jerky audio and a    *
*   slow game).  The reconcile is continuous and heal-only again, and      *
*   voice recreation is exclusively the DACRATE-change path (point 6).     *
*                                                                           *
*   SESSION 8 (v1.0.16) - two fixes from a minidump forensics round        *
*   (Windows 7 + VxKex machine: xaudio2_9.dll is a VxKex SHIM wrapping    *
*   the real XAudio 2.7, with its own ~40-thread worker pool):             *
*                                                                           *
*   (a) CRASH AFTER "ANY MANIPULATION WITH THE PLUGIN": the dump shows     *
*       heap free-list corruption (RtlAllocateHeap decoding a garbage     *
*       Flink) surfacing on the GUI thread long after the damage was      *
*       done.  Teardown released the engine and freed the slot pool       *
*       immediately, while the shim's worker pool may still dispatch a    *
*       late OnBufferEnd/OnVoiceError INTO the driver object - and the    *
*       object itself is deleted right after AI_Shutdown() returns        *
*       (RomClosed / RomOpen / PluginShutdown all do                       *
*       "tmp->AI_Shutdown(); delete tmp;").  One 8-byte token write into  *
*       freed memory is exactly a corrupted free-list link.  Teardown is  *
*       now a hardened, ordered shutdown: detach both callbacks' owner    *
*       pointers FIRST (a late callback becomes a no-op), Stop ->         *
*       Flush -> bounded queue-drain wait -> DestroyVoice (never          *
*       mid-playback), StopEngine, a 30 ms quiescence grace for          *
*       in-flight pool dispatches, only then Release, close the event    *
*       and free the slot pool.  The shim DLL itself is now PINNED for   *
*       the whole process (loaded once, never FreeLibrary'd): with a     *
*       40-thread pool inside the shim, unloading its code while a       *
*       worker is still executing it would be a guaranteed crash.        *
*                                                                           *
*   (b) SLIGHT CRACKLE + LOST ONBUFFEREND TOKENS: the token ring was a    *
*       single-producer SPSC queue, but the shim dispatches callbacks     *
*       from a WORKER POOL - concurrent OnBufferEnd calls both claimed    *
*       the same tail slot and one token was silently lost per race.      *
*       That is the "partially dead callback channel" this machine has    *
*       shown since v1.0.10, and every lost token adds phantom in-flight  *
*       debt that the heal then retires GUESSING from GetState - which    *
*       on the shim can under-report, so a slot was sometimes recycled    *
*       while its buffer was still audible (PCM overwritten               *
*       mid-playback = crackle).  The ring is now a Vyukov-style          *
*       bounded MPMC queue (correct for any number of producer            *
*       threads), and the heal is AGE-GATED: a slot must have been        *
*       outstanding for at least max(40 ms, 2x the chunk duration)        *
*       before the reconcile may retire it, so a buffer that is still     *
*       physically playing can never be recycled no matter what           *
*       GetState claims.  On a healthy machine both changes are no-ops:   *
*       single-producer MPMC behaves exactly like SPSC, and real          *
*       retirement still arrives via OnBufferEnd within microseconds.     *
*                                                                           *
*   SESSION 9 (v1.0.17) - "emulator sometimes silently closes while       *
*   working with the plugin and its settings" (residual after v1.0.16):   *
*                                                                           *
*   (a) CALLBACK-LIFETIME ISOLATION: v1.0.16 detached the callbacks'      *
*       owner pointer, but the callback OBJECTS were members of the      *
*       driver object, which RomClosed/RomOpen/PluginShutdown delete     *
*       immediately after AI_Shutdown().  A shim worker preempted        *
*       between loading the callback pointer and entering our code       *
*       re-entered after the 30 ms grace and dispatched through a        *
*       vtable pointer in freed memory - "sometimes", surfacing as       *
*       heap corruption and a WER-less silent exit on this Windows 7     *
*       machine.  Everything a callback can touch now lives in a         *
*       dedicated heap XACallbackShard (token ring, wake event,          *
*       device-lost/error flags) next to the two heap-allocated          *
*       interface objects, and the whole bundle is deliberately          *
*       NEVER FREED while the process lives (~3 KB + one event           *
*       handle per engine creation, reclaimed at process exit).         *
*       The shutdown path now contains ZERO timing assumptions.         *
*                                                                           *
*   (b) DLL SELF-PIN: the frontend's FreeLibrary on the plugin DLL       *
*       (plugin switching in the settings UI) can no longer unmap our    *
*       code while a late shim worker is still executing it - the        *
*       plugin pins itself for the process lifetime in PluginStartup    *
*       (see main.cpp).  Together with (a) this makes ANY late           *
*       callback provably safe.                                           *
*                                                                           *
*   (c) The degraded-channel WARNING is now streak-gated: it fires at    *
*       most once per degraded period instead of re-arming on every      *
*       single surviving retirement (a partially-alive channel could     *
*       re-fire it within milliseconds), and its text states plainly     *
*       that this is a machine property (Windows 7 / VxKex               *
*       xaudio2_9 shim), that retirement is paced from engine truth,    *
*       and that audio and emulation speed are unaffected.               *
*                                                                           *
*   3. BOUNDED IN-FLIGHT BACKLOG: at most kMaxInflightMs of audio is        *
*      allowed to sit unretired inside the engine.  If the engine falls     *
*      behind, the excess that the wall clock says is due is dropped from   *
*      the head of the ring buffer (ConsumeBuffered) instead of letting    *
*      the ring fill up - the game keeps running at full speed and the      *
*      audio plays whatever the engine managed to take.  This mirrors      *
*      what the Project64 integration does with a slow backend.  A small   *
*      3-chunk "comfort queue" is always kept inside the engine so that    *
*      scheduler jitter can never underrun the voice.                      *
*                                                                           *
*   4. SLOT GENERATION TOKENS: pContext carries (slot index, generation).   *
*      A slot is only recycled when OnBufferEnd reports the same           *
*      generation that was submitted, so buffers can never be overwritten  *
*      while still queued, and late/duplicate callbacks (e.g. after         *
*      FlushSourceBuffers) are ignored safely.                              *
*                                                                           *
*                                                                           *
*   6. SESSION 5 - VOICE RECREATION ON RATE CHANGE + GETSTATE RECONCILE:   *
*      A DACRATE change now swaps the source voice for a freshly created   *
*      one at the new rate (Stop -> FlushSourceBuffers -> wait for the     *
*      documented GetState().BuffersQueued == 0 reset condition ->         *
*      DestroyVoice -> CreateSourceVoice -> Start), instead of calling     *
*      SetSourceSampleRate().  Microsoft's FlushSourceBuffers docs state  *
*      that SetSourceSampleRate is ILLEGAL right after Stop+Flush until    *
*      the flush has completed, and that stop+flush+resubmit "resets all  *
*      of the voice's internal counters" - the undefined zone this        *
*      driver's feeder used to race submissions into (that race is        *
*      exactly how buffers could leave the queue with no callback).      *
*      The whole transition runs under m_SubmitMutex (safe: OnBufferEnd  *
*      is lock-free).  Independently, the retirement accounting no       *
*      longer trusts OnBufferEnd alone: m_OutstandingSlots keeps the     *
*      FIFO of submitted-not-retired slots, and when the callback        *
*      channel has been silent ~50 ms the audio thread reconciles       *
*      against the engine's own GetState().BuffersQueued and retires    *
*      what the engine says is gone - the same engine-truth feedback    *
*      the XAudio 2.7 backend's feeder has used all along (which is     *
*      why that backend never showed this bug on the same machine).     *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/
#pragma once

#include "common.h"

#if defined(ENABLE_BACKEND_XAUDIO2_MODERN)

#include <atomic>
#include <mutex>

#include "XAudio2ModernApi.h"
#include "SoundDriver.h"
#include "PluginMutex.h"

#include <new>

/* Size of the callback token ring (SESSION 8; unchanged by SESSION 9). */
static const u32 kXaPendingCount = 256;

/* Hard upper bound for the submission slot pool (StartAudioThread clamps
 * its computed slot count to this value).  The free-list stack and the
 * outstanding-FIFO below are fixed-capacity arrays of this size - bounded,
 * allocation-free storage that avoids std::vector/std::deque, whose growth
 * paths reference std::__throw_bad_alloc / std::__throw_length_error and
 * thereby pull the whole libstdc++ exception machinery (plus its
 * winpthreads dependencies) into the statically linked MinGW plugin.
 * See PluginMutex.h for the same reasoning applied to the mutexes. */
static const u32 kMaxSlotPool = 64;

/* ---------------------------------------------------------------------------
 * XACallbackShard (SESSION 9, v1.0.17) - callback-lifetime isolation.
 *
 * Everything an XAudio2 callback can touch - the lock-free MPMC token ring,
 * the feeder wake event, the device-lost / voice-error flags - lives in THIS
 * dedicated heap allocation, OUTSIDE the driver object, together with the
 * two callback INTERFACE objects the engine points at (they are heap-
 * allocated too, see VoiceCallbackModern / EngineCallbackModern below).
 *
 * WHY (the residual crash of v1.0.16, "emulator sometimes silently closes
 * while working with the plugin and its settings"):  v1.0.16 detached the
 * callbacks' owner pointer and added a 30 ms grace after StopEngine, but the
 * callback objects were still MEMBERS of the driver object - and RomClosed /
 * RomOpen / PluginShutdown all 'delete' the driver right after AI_Shutdown.
 * A VxKex shim worker that was preempted anywhere between loading the
 * callback pointer and entering our code re-entered AFTER the grace and
 * dispatched through a vtable pointer in FREED memory - probabilistic
 * safety, hence "sometimes".  A leaked 8-byte token write was heap
 * corruption surfacing later as a silent process exit (WER-less Win7).
 *
 * THE CONTRACT NOW: the shard and both callback objects are deliberately
 * NEVER FREED while the process lives.  Teardown sets 'retired' (callbacks
 * become cheap no-ops), tears the engine down in order, then simply drops
 * the pointers - ~3 KB and one event handle per engine creation stay
 * mapped until process exit reclaims them.  With the plugin DLL itself
 * pinned (main.cpp, SESSION 9), a late shim worker can always safely
 * execute our code and touch ONLY still-mapped memory.  There is no timing
 * assumption anywhere in the shutdown path any more.
 * --------------------------------------------------------------------------- */
struct XACallbackShard
{
    /* Vyukov-style bounded MPMC token ring (SESSION 8).  Producers: engine
     * callback thread(s) - a worker POOL on the VxKex shim.  Consumer: the
     * audio thread.  Protocol unchanged from v1.0.16. */
    u64                tokens[kXaPendingCount];
    std::atomic<u32>   seq[kXaPendingCount];    /* per-cell sequence     */
    std::atomic<u32>   enqueuePos;              /* MPMC cursor           */
    std::atomic<u32>   dequeuePos;              /* consumer-only         */

    /* Lifetime / detach flag: once true, every callback entry returns
     * immediately.  Checked once at entry - the shard memory itself is
     * never freed, so even the check races no-one. */
    std::atomic<bool>  retired;

    /* Engine-callback payloads (written by OnCriticalError/OnVoiceError,
     * polled by Setup()/the audio thread). */
    std::atomic<bool>  deviceLost;
    std::atomic<long>  lastVoiceErrorHr;

    /* Auto-reset event signalling the feeder.  Owned by the shard; NEVER
     * closed while the process lives (a late SetEvent on a closed handle
     * would be a handle-recycle hazard - leaving it open costs one handle
     * per engine creation, reclaimed at process exit). */
    HANDLE             wakeEvent;

    /* Resets the ring and flags; creates the event.  Returns false when
     * the event could not be created (caller fails Setup). */
    bool Init();
};

class XAudio2SoundDriverModern : public SoundDriver
{
public:
    XAudio2SoundDriverModern();
    ~XAudio2SoundDriverModern();

    /* Setup and Teardown */
    Boolean Initialize();
    void DeInitialize();

    /* Management functions */
    void StopAudio();
    void StartAudio();
    void SetFrequency(u32 Frequency);

    /* Volume: 0..100, 100 = unity */
    void SetVolume(u32 volume);

    /* Factory method */
    static SoundDriverInterface* CreateSoundDriver() { return new (std::nothrow) XAudio2SoundDriverModern(); }
    static bool ValidateDriver();

protected:
    /* Setup/teardown of the XAudio2 engine (engine, mastering voice,
     * source voice) - idempotent. */
    BOOL Setup();
    void Teardown();

    /* SESSION 9: allocate the callback shard + the two interface objects
     * (used by Setup).  Returns false only on allocation/event failure. */
    bool AllocateCallbackInfra();

    /* SESSION 9: mark the callback shard retired and drop our references -
     * the memory deliberately stays mapped for the process lifetime (see
     * XACallbackShard), so late engine/shim callbacks stay provably safe. */
    void AbandonCallbackInfra();

    void StartAudioThread();
    void StopAudioThread();

    static DWORD WINAPI AudioThreadProc(LPVOID lpParameter);
    void AudioThreadLoop();

    bool  dllInitialized;
    bool  bStopAudioThread;
    HANDLE hAudioThread;
    int   iCurFrequency;

    /* COM state (per-instance) */
    bool  bCoInitialized;

    /* XAudio2 objects */
    M_IXAudio2             *m_Engine;
    M_IXAudio2MasteringVoice *m_Master;
    M_IXAudio2SourceVoice  *m_Source;

    /* DLL handle for xaudio2_9.dll (dynamically loaded) */
    HMODULE m_hXAudioDLL;
    M_PFN_XAudio2Create m_pfnXAudio2Create;

    /* Voice callback (buffer retirement bookkeeping)
     *
     * ABI NOTE: IXAudio2VoiceCallback has NO IUnknown methods - the vtable
     * starts directly with OnVoiceProcessingPassStart.  The engine invokes
     * slot 0 on its own processing thread every pass; the previous revision
     * carried QueryInterface/AddRef/Release here, which shifted every
     * callback three slots and crashed the frontend within moments of
     * audio starting (QueryInterface writing NULL through a garbage
     * register argument).  Keep exactly the seven interface methods.
     *
     * SESSION 9: this object is HEAP-ALLOCATED together with its
     * XACallbackShard and deliberately never freed (see XACallbackShard).
     * The engine may dispatch into it at any time until Release() - and on
     * the VxKex shim even slightly after - so both the object and
     * everything OnBufferEnd touches must outlive the driver object,
     * which RomClosed/RomOpen/PluginShutdown delete immediately after
     * AI_Shutdown().
     *
     * OnBufferEnd() is the feeder's wake-up signal.  It runs on the
     * engine's own dispatch context - on the VxKex shim a WORKER POOL,
     * possibly several calls concurrently - so it must stay CHEAP AND
     * NON-BLOCKING: it only pushes the buffer's token onto the lock-free
     * MPMC ring in the shard and signals the event; all bookkeeping
     * (slot recycle, byte accounting) is done by the audio thread when it
     * drains the ring.  This also rules out any lock inversion between the
     * callback and SubmitSourceBuffer's internal engine locks. */
    class VoiceCallbackModern : public M_IXAudio2VoiceCallback
    {
    public:
        explicit VoiceCallbackModern(XACallbackShard *shard) : m_shard(shard) {}

        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32 BytesRequired) override { UNREFERENCED_PARAMETER(BytesRequired); }
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd(void) override {}
        void STDMETHODCALLTYPE OnStreamEnd(void) override {}
        void STDMETHODCALLTYPE OnBufferStart(void *pBufferContext) override { UNREFERENCED_PARAMETER(pBufferContext); }
        void STDMETHODCALLTYPE OnBufferEnd(void *pBufferContext) override;
        void STDMETHODCALLTYPE OnLoopEnd(void *pBufferContext) override { UNREFERENCED_PARAMETER(pBufferContext); }
        void STDMETHODCALLTYPE OnVoiceError(void *pBufferContext, HRESULT Error) override
        {
            /* SESSION 3 FIX: this was a pure no-op.  Per Microsoft's docs,
             * OnVoiceError fires when a critical error occurs during THIS
             * voice's processing; unlike OnCriticalError (engine-level,
             * already handled via the shard's deviceLost flag), a
             * voice-level error does not necessarily bring the whole
             * engine down - which means GetState()/SubmitSourceBuffer can
             * keep reporting "healthy" numbers (BuffersQueued==0, S_OK) on
             * a voice that silently stopped delivering OnBufferEnd
             * forever.  Ignoring OnVoiceError was the actual gap: reuse
             * the same device-lost recovery path OnCriticalError already
             * uses (Setup() tears down and rebuilds on the next call), and
             * remember the HRESULT so Setup() - not this internal XAudio2
             * thread - can log it safely.
             *
             * SESSION 9: writes go to the shard (never freed), so this is
             * safe even when dispatched after the driver object is gone. */
            UNREFERENCED_PARAMETER(pBufferContext);
            if (m_shard != NULL && !m_shard->retired.load(std::memory_order_relaxed))
            {
                m_shard->lastVoiceErrorHr.store((long)Error, std::memory_order_relaxed);
                m_shard->deviceLost.store(true, std::memory_order_relaxed);
            }
        }
    private:
        XACallbackShard *m_shard;   /* immutable after construction */
    };

    VoiceCallbackModern *m_pVoiceCallback;

    /* Engine callback - reports critical device errors so Setup() can
     * rebuild the engine (device unplugged / default device changed)
     * instead of quietly staying silent forever.  OnCriticalError() only
     * sets a flag in the shard (the callback runs on an internal XAudio2
     * thread; anything heavier, including Teardown()'s StopAudioThread() /
     * WaitForSingleObject, must not run there).  SESSION 9: heap-allocated
     * with the shard and never freed - see XACallbackShard. */
    class EngineCallbackModern : public M_IXAudio2EngineCallback
    {
    public:
        explicit EngineCallbackModern(XACallbackShard *shard) : m_shard(shard) {}

        void STDMETHODCALLTYPE OnProcessingPassStart(void) override {}
        void STDMETHODCALLTYPE OnProcessingPassEnd(void) override {}
        void STDMETHODCALLTYPE OnCriticalError(HRESULT Error) override
        {
            UNREFERENCED_PARAMETER(Error);
            if (m_shard != NULL && !m_shard->retired.load(std::memory_order_relaxed))
                m_shard->deviceLost.store(true, std::memory_order_relaxed);
        }
    private:
        XACallbackShard *m_shard;   /* immutable after construction */
    };

    EngineCallbackModern *m_pEngineCallback;

    /* Callback infrastructure (shard + both interface objects).  Allocated
     * in Setup(), deliberately ABANDONED (leaked for the process lifetime)
     * in Teardown() - see XACallbackShard for the safety contract. */
    XACallbackShard    *m_CB;

    /* -----------------------------------------------------------------
     * Wall-clock paced submission machinery (see the file header).
     * --------------------------------------------------------------- */

    /* Largest per-submission chunk the game rate / BACKEND_FPS can ever
     * produce (192 kHz / 15 fps * 4 + slack).  Slots are sized by this
     * so a DACRATE change never outgrows the pool. */
    static const u32 kMaxSegment = 192000 / 15 * 4 + 64;

    /* Maximum audio backlog allowed inside the engine before the excess
     * is dropped from the ring buffer.  In normal operation the in-flight
     * amount stays at 3 submitted chunks (~1/3 of this) - the cap only
     * engages when the engine consumes slower than real time, keeping the
     * emulator at full speed (audio degrades instead of the game). */
    static const u32 kMaxInflightMs = 150;

    /* One submission slot.  'generation' makes pContext tokens unique per
     * submission: OnBufferEnd only recycles the slot when the token's
     * generation still matches, so a buffer can never be overwritten
     * while queued, and late callbacks after a flush are ignored.
     * 'submitTickMs' (GetTickCount64 at submission, set under
     * m_SubmitMutex) feeds the SESSION 8 age gate of the reconcile. */
    struct BufferSlot
    {
        std::atomic<u32> generation;   /* bumped on every submission     */
        u32              lastLen;      /* bytes submitted in this round  */
        u64              submitTickMs; /* when this round was submitted  */
        u8               data[kMaxSegment];
    };

    /* Heap pool of submission slots + free list.  m_SubmitMutex
     * serialises every operation that touches slots or the
     * submitted/retired accounting: the audio thread's submissions, the
     * engine callback's retirements and the core thread's accounting
     * resets (SetFrequency / StopAudio).  LOCK ORDER: m_SubmitMutex is
     * always taken FIRST; SubmitChunk() calls LoadAiBuffer() (which
     * takes the base class ring mutex m_mutex) while holding it.  The
     * reverse order (m_mutex -> m_SubmitMutex) never occurs: the core
     * thread's AI_LenChanged/AI_SetFrequency take m_mutex alone, and
     * the voice-control paths take m_SubmitMutex alone (see
     * SetFrequency for the FlushSourceBuffers/OnBufferEnd re-entrancy
     * note), so no inversion and no self-deadlock is possible.
     *
     * The free list is a bounded FIFO queue (the OLDEST freed slot is
     * handed out first) and the outstanding set a bounded FIFO as well
     * (see kMaxSlotPool above); both are guarded by m_SubmitMutex.
     *
     * Why FIFO and not LIFO: a slot returned to the pool is not reused
     * until every other free slot has been used once, i.e. roughly
     * (pool size - in-flight chunks) submissions later (~150 ms at the
     * defaults).  With a LIFO stack the slot that was retired a moment
     * ago was overwritten immediately, so any retirement that happened
     * slightly BEFORE the engine had really finished reading the buffer
     * (a shimmed/partially dead OnBufferEnd channel retired by the
     * reconcile, an early or duplicate callback) turned straight into
     * recycled live PCM = an audible click.  FIFO reuse makes that
     * class of crackle physically impossible at no cost. */
    BufferSlot        *m_Slots;
    u32                m_SlotCount;
    u32                m_FreeSlots[kMaxSlotPool];      /* FIFO queue      */
    u32                m_FreeSlotCount;
    u32                m_OutstandingSlots[kMaxSlotPool]; /* submission FIFO */
    u32                m_OutstandingCount;
    PluginMutex        m_SubmitMutex;

    /* Feeder wake event + MPMC token ring now live in the callback shard
     * (m_CB, SESSION 9) - see XACallbackShard above.  OnBufferEnd signals
     * m_CB->wakeEvent and pushes tokens into the shard's ring; the audio
     * thread consumes them in DrainPendingRetires(). */

    /* Retires every token currently in the pending ring (called from
     * the audio thread, takes m_SubmitMutex once for the whole batch). */
    void DrainPendingRetires();

    /* SESSION 7 (v1.0.15, restored) - continuous engine-truth reconcile,
     * audio thread only, ~16 ms cadence.  OnBufferEnd remains the primary
     * retirement signal, but on machines where the xaudio2_9 callback
     * channel is (partially) dead - buffers leave the engine queue with
     * no OnBufferEnd ever being delivered - the plugin's accounting would
     * otherwise accumulate phantom in-flight debt: m_BytesRetired freezes,
     * the in-flight cap closes the submission gates, the ring overfills
     * and the A/V sync throttle drags the whole emulation below real time
     * (the exact slowdown the wall-clock pacing was designed to prevent).
     *
     * Every ~16 ms (the cadence the proven XAudio 2.7 feeder has always
     * used for its GetState-driven loop) the engine's own
     * GetState(XAUDIO2_VOICE_NOSAMPLESPLAYED).BuffersQueued is snapshotted
     * under m_SubmitMutex and the difference beyond the +2 safety margin
     * is HEALED: XAudio2 retires a voice's buffers strictly in submission
     * order, so the OLDEST outstanding slots beyond queued+2 are buffers
     * the engine has already fully consumed - they are retired, their
     * bytes credited to m_BytesRetired and their slots returned to the
     * free list.  The margin guarantees that a slot whose buffer the
     * engine still counts as queued - or whose OnBufferEnd token is merely
     * still in flight - can never be recycled, so live PCM is never
     * overwritten (no crackle).  A late real token for a healed slot is
     * rejected by the FIFO-membership check in DrainPendingRetires, so
     * double accounting is impossible.  On a healthy machine
     * ours <= queued+2 essentially always and this stays dormant; on a
     * callback-dead machine it becomes the retirement source, exactly how
     * the polling 2.7 feeder always worked.  The v1.0.13/1.0.14 regression
     * that replaced this with a ~500 ms "recreate the voice" watchdog only
     * discarded live audio every cycle and never restored the channel -
     * removed; voice recreation is exclusively the DACRATE-change path.
     *
     * SESSION 8 (v1.0.16) - AGE GATE: the shim's GetState may UNDER-REPORT
     * (the minidump machine reports BuffersQueued == 0 while its buffers
     * are still audible), so "beyond queued + 2" alone can retire a slot
     * whose PCM the engine is still reading - the slot is recycled, the
     * next submission overwrites the buffer mid-playback: an audible
     * crackle.  A slot is now only heal-retirable when it has ALSO been
     * outstanding for at least HealMinAgeMs() - a bound on physical
     * playback time that no under-reporting engine can violate.  The
     * extra in-flight lag (2 chunks + 40 ms, far below the 150 ms budget;
     * the budget's chunk-count floor was raised to 6 chunks so the age
     * lag can never close the submission gates at any BACKEND_FPS) is
     * imperceptible, and on a healthy machine the age gate changes
     * nothing (real tokens arrive within microseconds of playback). */
    void HealAccountingWithEngine();

    /* SESSION 8: minimum age in milliseconds before the reconcile may
     * retire a slot: max(40 ms, 2x the current chunk duration).  A chunk
     * submitted at T with the engine consuming at 1x real time is
     * finished by T + chunkDuration; 2x covers an engine running a full
     * 1x BEHIND real time (which would be loudly visible as the "engine
     * consumes slower than real time" drop warning).  Computed per call
     * from cacheSize / frequency - not a compile-time constant - so
     * extreme BACKEND_FPS settings (small frequent chunks vs. large rare
     * chunks) both stay correct.  Audio thread only. */
    u32 HealMinAgeMs() const;

    /* SESSION 5 - the full, documented voice transition for a rate
     * change: Stop -> FlushSourceBuffers -> (wait for the documented
     * BuffersQueued == 0 reset condition) -> DestroyVoice ->
     * CreateSourceVoice(new rate) -> Start -> accounting resync.
     * Takes m_SubmitMutex for the WHOLE transition (safe: OnBufferEnd
     * is lock-free) so the feeder can never race a submission into the
     * flush window.  Updates iCurFrequency only on success. */
    void RecreateSourceVoice(u32 Frequency);

    /* SESSION 5 - polls GetState().BuffersQueued until the engine
     * reports an empty queue (the documented completion condition for
     * FlushSourceBuffers) or the timeout expires.  Caller must hold
     * m_SubmitMutex (the feeder is parked, so nothing else submits). */
    bool WaitVoiceQueueEmpty(u32 timeoutMs);

    /* Pacing credit: signed byte balance of what the wall clock says is
     * due from the ring.  Positive - data that must still be submitted
     * or dropped; negative (bounded by the 3-chunk comfort queue) - an
     * advance taken while topping up the queue, which the clock repays.
     * Audio thread only. */
    s64                m_PacingCredit;

    /* CRACKLE FIX: RecreateSourceVoice() holds m_SubmitMutex for the
     * whole voice transition (Stop -> Flush -> up to 100 ms wait ->
     * DestroyVoice -> CreateSourceVoice), which blocks the audio
     * thread - it is waiting on the very same mutex inside
     * DrainPendingRetires()/HealAccountingWithEngine()/SubmitChunk().  The
     * audio thread's wall-clock reference (AudioThreadLoop()'s local
     * 'lastQpc') is NOT reset by that, so the very next iteration sees
     * an elapsed time that includes the whole blocked interval and
     * manufactures a large one-shot pacing credit.  That credit lets
     * the burst-guard (8 chunks/wake) slam several chunks into the
     * BRAND NEW voice in one go - more than GetState() can register
     * within its ~10 ms processing quantum, so the very next
     * HealAccountingWithEngine() sees "ours" far ahead of "queued" and
     * mistakes the still-playing burst for buffers that lost their
     * OnBufferEnd, freeing their slots while the engine is still
     * reading from them - i.e., a slot's memory gets overwritten by a
     * new submission while the old one is still audible: a crackle at
     * every DACRATE change.  Set whenever the pacing clock's time
     * reference needs to be re-anchored to "now" (voice recreation);
     * the audio thread resets its local lastQpc and skips exactly one
     * due-time accrual so the blocked interval is never charged. */
    std::atomic<bool>  m_ResyncPacingClock;

    /* One-shot latch for the degraded-callback-channel warning (audio
     * thread only).  SESSION 9: re-armed only after a SUSTAINED healthy
     * streak (64 consecutive clean reconcile checks, ~1 s) - a merely
     * partially-alive channel (the VxKex machine: some tokens arrive,
     * some never do) used to re-arm the latch on every real retirement
     * and re-fire the warning within milliseconds, a warning storm.
     * With the streak gate the line appears at most once per degraded
     * period, which is all a machine-property diagnostic should say. */
    bool              m_ReconcileHealedLogged;
    u32               m_HealthyHealStreak;

    /* Byte accounting (atomics so a diagnostics reader is consistent
     * without blocking the audio path):
     *   m_BytesSubmitted - incremented when a buffer is handed to the engine
     *   m_BytesRetired   - incremented when OnBufferEnd reports it done
     * in-flight = submitted - retired. */
    std::atomic<u64>   m_BytesSubmitted;
    std::atomic<u64>   m_BytesRetired;

    /* One-shot warning latch for the lag-drop path. */
    bool               m_LagWarned;

    /* Submission state (per-instance) */
    u32   cacheSize;         /* bytes per submitted buffer           */

    /* Feeds one cacheSize chunk (or 'len' bytes) to the engine.
     * Returns true when the engine accepted the buffer.  Takes
     * m_SubmitMutex (and, inside, the base ring mutex via
     * LoadAiBuffer). */
    bool SubmitChunk(u32 len);

    /* Returns every slot to the free list and invalidates outstanding
     * buffer tokens (used after FlushSourceBuffers, which invalidates
     * all queued buffers at once).  Called with m_SubmitMutex held. */
    void ResetSlotsLocked();
};

#endif /* ENABLE_BACKEND_XAUDIO2_MODERN */
