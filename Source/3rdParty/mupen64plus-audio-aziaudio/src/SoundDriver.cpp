/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* SoundDriver.cpp - buffered audio sink base class implementation           *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#include "SoundDriver.h"
#include "SoundDriverInterface.h"
#include "Configuration.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

/* ---------------------------------------------------------------------------
 * Construction / destruction
 * ------------------------------------------------------------------------ */
SoundDriver::SoundDriver()
{
    m_audioIsInitialized = false;
    m_isValid = false;

    m_CurrentReadLoc = 0;
    m_CurrentWriteLoc = 0;
    m_BufferRemaining = 0;

    m_SamplesPerSecond = 44100;     /* matches the core's lazy default */
    m_SyncTargetBytes = 16384;      /* ~93 ms at 44.1 kHz stereo        */
    m_SpeedFactor = 100;
    m_LastSample = 0;
    m_ResampleLastLeft = 0.0f;
    m_ResampleLastRight = 0.0f;
    m_ResamplePhase = 0.0;
    m_Volume = 100;
    m_DroppedBytes = 0;
    m_SyncStallStrikes = 0;
    m_SyncDisabled = false;
    m_FallbackRunning = false;
#ifdef _WIN32
    m_hDrainThread = NULL;
#else
    m_hDrainThread = 0;
#endif
}

SoundDriver::~SoundDriver()
{
    /* ROM-CLOSE CRASH FIX (confirmed by minidump analysis, all backends).
     *
     * The previous revision called AI_Shutdown() here.  That was fatal:
     * during base-class destruction the vptr is already switched to the
     * SoundDriver base vtable, whose pure-virtual slots (Initialize,
     * DeInitialize, StartAudio, StopAudio, SetFrequency) are ZERO in this
     * MinGW build (the weak-undefined __cxa_pure_virtual aliases resolve
     * to null).  AI_Shutdown() -> StopAudio() therefore dispatched to
     * `call *0x20(%rax)` with slot value 0 - a call to NULL - and crashed
     * the emulator with an access violation inside `delete snd` in
     * RomClosed()/PluginShutdown()/RomOpen().  The minidump shows the
     * fault exactly there: return address RomClosed+0x66 (the instruction
     * after `call *0x8(%rax)`, the deleting-destructor call).
     *
     * Teardown ownership instead:
     *   - every entry point (RomOpen / RomClosed / PluginShutdown) calls
     *     AI_Shutdown() BEFORE deleting the driver, and
     *   - every derived destructor calls its own guarded DeInitialize()
     *     (safe: the vptr still points at the derived vtable there).
     *
     * NO virtual calls are allowed below this point. */
}

/* ---------------------------------------------------------------------------
 * Thread sleep helper
 * ------------------------------------------------------------------------ */
void SoundDriver::ThrottleSleepMs(u32 ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = (long)ms * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/* ---------------------------------------------------------------------------
 * AI_SetFrequency - the core programmed a new DAC rate
 * ------------------------------------------------------------------------ */
void SoundDriver::AI_SetFrequency(u32 Frequency)
{
    if (Frequency == 0 || Frequency > 192000)
    {
        DebugMessage(M64MSG_WARNING, "AI_SetFrequency: ignoring invalid frequency %u", Frequency);
        return;
    }

    m_SamplesPerSecond = Frequency;

    /* Reset the ring buffer before switching the device over so that no
     * stale audio at the old rate is played back. */
    {
        PluginLockGuard lck(m_mutex);
        m_CurrentReadLoc = 0;
        m_CurrentWriteLoc = 0;
        m_BufferRemaining = 0;
        m_LastSample = 0;
        m_ResampleLastLeft = 0.0f;
        m_ResampleLastRight = 0.0f;
        m_ResamplePhase = 0.0;
        m_DroppedBytes = 0;
    }

    SetFrequency(Frequency);

    /* Recompute the A/V sync target:
     *   BUFFER_LEVEL frames of BUFFER_FPS (e.g. 3 frames @ 45 fps
     *   -> ~67 ms of buffered audio).  This mirrors the original
     *   AziAudio buffering model and the PRIMARY_BUFFER_TARGET of
     *   mupen64plus-audio-sdl. */
    u32 fps   = Configuration::getBufferFPS();
    u32 level = Configuration::getBufferLevel();
    if (fps == 0)   fps = 60;
    if (level == 0) level = 2;

    u64 target = (u64)Frequency * 4ull * level / fps;
    if (target > MAX_SIZE)
        target = MAX_SIZE;
    if (target < 4096)
        target = 4096;

    m_SyncTargetBytes = (u32)target;
}

/* ---------------------------------------------------------------------------
 * AI_LenChanged - the core "played" 'length' bytes of AI DMA data
 * ------------------------------------------------------------------------ */
void SoundDriver::AI_LenChanged(const u8 *start, u32 length)
{
    if (length == 0 || start == NULL)
        return;

    /* Push the data into the ring buffer.  The copy must happen now:
     * the caller (Mupen64Plus audio_plugin_compat) restores the AI
     * registers as soon as we return and the emulated game may reuse
     * that RDRAM immediately. */
    {
        PluginLockGuard lck(m_mutex);
        PushSamples(start, length);
    }

    /* A/V synchronisation: when the ring buffer is fuller than the
     * target level the emulator is running ahead of the sound card -
     * sleep here (on the emulator's CPU thread) to let playback catch
     * up, exactly like sdl_synchronize_audio() does.  The tolerance is
     * ~10 ms so that the throttle does not trigger on jitter alone.
     *
     * STALL GUARD (XAudio 2.9-on-Win7 bug and any dead backend): if the
     * backend never consumes (e.g. xaudio2_9.dll shimmed by VxKex on
     * Windows 7, WASAPI render thread died, no sound device) the ring
     * buffer stays permanently full and this throttle would slow the
     * game down to a crawl - one forced 1-second wait per DMA.  After
     * three consecutive full-stall seconds with no drain we disable the
     * throttle for the rest of the session: the game runs at full speed
     * and audio is dropped silently.  A merely slow backend resets the
     * strike counter every time it catches up. */
    if (Configuration::getSyncAudio() == true && !m_SyncDisabled)
    {
        const u32 tolerance = (m_SamplesPerSecond / 100) * 4;    /* 10 ms  */
        u32 limit = 0;
        u32 spins = 0;

        for (;;)
        {
            {
                PluginLockGuard lck(m_mutex);
                limit = m_BufferRemaining;
            }

            if (limit <= EffectiveSyncTarget() + tolerance)
            {
                m_SyncStallStrikes = 0;
                break;
            }

            /* Safety valve: never block the emulator for more than
             * ~1 second total inside one call (e.g. paused sound device
             * or debugger break) - drop the excess instead. */
            if (++spins > 1000)
            {
                {
                    PluginLockGuard lck(m_mutex);
                    u32 excess = m_BufferRemaining > EffectiveSyncTarget() + tolerance
                        ? m_BufferRemaining - (EffectiveSyncTarget() + tolerance) : 0;
                    if (excess > 0)
                    {
                        excess &= ~3u;   /* keep stereo alignment */
                        m_CurrentReadLoc = (m_CurrentReadLoc + excess) % MAX_SIZE;
                        m_BufferRemaining -= excess;
                        m_DroppedBytes += excess;
                    }
                }

                if (++m_SyncStallStrikes >= 3)
                {
                    m_SyncDisabled = true;
                    DebugMessage(M64MSG_ERROR,
                        "audio backend is not consuming - A/V sync disabled for this session "
                        "(game runs at full speed, sound output is dropped)");
                }
                break;
            }

            ThrottleSleepMs(1);
        }
    }
}

/* ---------------------------------------------------------------------------
 * PushSamples - copy RDRAM AI DMA data into the ring buffer
 *
 * The N64 AI DMA 32-bit word contains [R][L] sample pairs (left in the
 * high half of the big-endian word).  The Mupen64Plus core stores RDRAM
 * words in host byte order, so on a little-endian host a 16-bit load
 * from ptr+0 yields the right channel and ptr+2 the left channel, both
 * with correct sample values.  Writing them swapped into the ring
 * buffer produces the [L,R] interleaved order a PC sound device
 * expects.  (Identical to the original AziAudio conversion, which
 * works for the same reason on Project64.)
 * ------------------------------------------------------------------------ */
void SoundDriver::PushSamples(const u8 *start, u32 length)
{
    /* Only full stereo sample frames; the core always pushes multiples
     * of 8 bytes anyway. */
    length &= ~3u;

    if (length > MAX_SIZE)
    {
        /* Pathological size (single DMA > 1s of audio): keep the tail. */
        start += length - MAX_SIZE;
        length = MAX_SIZE;
    }

    /* Make room if the buffer would overflow - drop the oldest data so
     * that playback stays continuous (sync throttling normally prevents
     * this from ever happening). */
    if (length > MAX_SIZE - m_BufferRemaining)
    {
        u32 need = length - (MAX_SIZE - m_BufferRemaining);
        need &= ~3u;
        m_CurrentReadLoc = (m_CurrentReadLoc + need) % MAX_SIZE;
        m_BufferRemaining -= (m_BufferRemaining > need) ? need : m_BufferRemaining;
        m_DroppedBytes += need;
        DebugMessage(M64MSG_WARNING, "AI_LenChanged: ring buffer overflow, dropped %u oldest bytes", need);
    }

    u32 first = (MAX_SIZE - m_CurrentWriteLoc < length)
        ? (MAX_SIZE - m_CurrentWriteLoc)
        : length;
    u32 second = length - first;

    const u8 *src = start;
    u8 *dst = m_Buffer + m_CurrentWriteLoc;

    /* N64 [R,L] -> host [L,R] with 16-bit pair swap, unrolled in
     * 4-byte units (one stereo sample frame). */
    for (u32 i = 0; i < first; i += 4)
    {
        dst[i + 0] = src[i + 2];
        dst[i + 1] = src[i + 3];
        dst[i + 2] = src[i + 0];
        dst[i + 3] = src[i + 1];
    }

    if (second > 0)
    {
        dst = m_Buffer;
        src = start + first;
        for (u32 i = 0; i < second; i += 4)
        {
            dst[i + 0] = src[i + 2];
            dst[i + 1] = src[i + 3];
            dst[i + 2] = src[i + 0];
            dst[i + 3] = src[i + 1];
        }
    }

    m_CurrentWriteLoc = (m_CurrentWriteLoc + length) % MAX_SIZE;
    m_BufferRemaining += length;

    if (m_BufferRemaining >= 4)
    {
        const u8 *last = m_Buffer + ((m_CurrentWriteLoc + MAX_SIZE - 4) % MAX_SIZE);
        m_LastSample = *(const u32 *)last;
    }
}

/* ---------------------------------------------------------------------------
 * Effective sync target (scaled by the speed factor)
 * ------------------------------------------------------------------------ */
u32 SoundDriver::EffectiveSyncTarget() const
{
    u32 target = m_SyncTargetBytes;

    if (m_SpeedFactor != 100)
    {
        u64 scaled = (u64)target * m_SpeedFactor / 100ull;
        if (scaled > MAX_SIZE)
            scaled = MAX_SIZE;
        target = (u32)scaled;
    }

    return target;
}

/* ---------------------------------------------------------------------------
 * LoadAiBuffer - backend pull with underrun hold
 * ------------------------------------------------------------------------ */
u32 SoundDriver::LoadAiBuffer(u8 *start, u32 length)
{
    if (start == NULL || length == 0)
        return 0;

    length &= ~3u;
    if (length == 0)
        return 0;

    /* Fast-forward consumes proportionally more input than the device
     * plays, keeping the ring buffer bounded at higher speeds. */
    u32 want = length;
    if (m_SpeedFactor != 100)
    {
        u64 scaled = (u64)length * m_SpeedFactor / 100ull;
        if (scaled < length) scaled = length;
        want = (u32)scaled;
    }

    u32 produced = 0;

    {
        PluginLockGuard lck(m_mutex);

        u32 avail = (m_BufferRemaining < want) ? m_BufferRemaining : want;
        avail &= ~3u;

        while (avail > 0 && produced < length)
        {
            u32 chunk = (MAX_SIZE - m_CurrentReadLoc < avail) ? (MAX_SIZE - m_CurrentReadLoc) : avail;
            if (chunk > (length - produced))
                chunk = length - produced;
            chunk &= ~3u;
            if (chunk == 0)
                break;

            memcpy(start + produced, m_Buffer + m_CurrentReadLoc, chunk);

            m_CurrentReadLoc = (m_CurrentReadLoc + chunk) % MAX_SIZE;
            m_BufferRemaining -= chunk;
            avail -= chunk;
            produced += chunk;
        }

        /* Remember the newest sample we handed out for the underrun hold */
        if (produced >= 4)
            m_LastSample = *(const u32 *)(start + produced - 4);
    }

    /* Underrun: hold the last sample so the output stays quiet instead
     * of clicking (classic AziAudio behaviour). */
    while (produced < length)
    {
        *(u32 *)(start + produced) = m_LastSample;
        produced += 4;
    }

    return produced;
}

/* ---------------------------------------------------------------------------
 * LoadAiBufferResample - WASAPI float output with streaming linear resampling
 * ------------------------------------------------------------------------ */
u32 SoundDriver::LoadAiBufferResample(u8 *start, u32 frames, float ratio)
{
    /* 'frames' output sample frames, 32-bit float stereo each. */
    if (start == NULL || frames == 0)
        return 0;

    if (ratio <= 0.0f)
        ratio = 1.0f;

    f32 *outp = (f32 *)start;
    u32 produced = 0;
    const f64 step = (f64)ratio;
    const f32 gain = (f32)m_Volume / 100.0f;

    /* The old implementation used
     *     current + phase * (current - previous)
     * which is extrapolation, not interpolation.  On a normal 44.1 -> 48 kHz
     * conversion that can overshoot badly on high-frequency content; the
     * following clamp then hard-clips those excursions.  Besides changing
     * the timbre, that injects unnecessary distortion into a path which is
     * supposed to be a transparent presentation of the N64 PCM stream.
     *
     * Keep the fractional source position between calls, but interpolate
     * between the CURRENT and NEXT ring-buffer frames.  Both the phase and
     * the last output sample are per-driver state, so reopening/resetting a
     * stream cannot inherit stale state from another driver instance. */
    PluginLockGuard lck(m_mutex);

    while (produced < frames)
    {
        if (m_BufferRemaining < 4)
            break;

        /* First move the source cursor over whole input samples requested by
         * the accumulated phase.  When the ring has only one input frame
         * left, wait for the next frame rather than advancing into unknown
         * data.  A real underrun is handled by holding the last sample below. */
        while (m_ResamplePhase >= 1.0)
        {
            if (m_BufferRemaining < 8)
                break;
            m_ResamplePhase -= 1.0;
            m_CurrentReadLoc = (m_CurrentReadLoc + 4) % MAX_SIZE;
            m_BufferRemaining -= 4;
        }

        if (m_BufferRemaining < 8)
        {
            /* We have one real input frame but not yet its neighbour.  Hold
             * that frame without advancing the fractional phase.  This is a
             * bounded underrun hold and avoids inventing a slope from stale
             * data; the next call can resume interpolation as soon as another
             * DMA frame arrives. */
            const s16 *cur = (const s16 *)(m_Buffer + m_CurrentReadLoc);
            const f32 l = (f32)cur[0] / 32768.0f;
            const f32 r = (f32)cur[1] / 32768.0f;
            outp[0] = l * gain;
            outp[1] = r * gain;
            m_ResampleLastLeft = l;
            m_ResampleLastRight = r;
            outp += 2;
            produced++;
            continue;
        }

        const s16 *cur = (const s16 *)(m_Buffer + m_CurrentReadLoc);
        const u32 nextLoc = (m_CurrentReadLoc + 4) % MAX_SIZE;
        const s16 *next = (const s16 *)(m_Buffer + nextLoc);

        const f32 cl = (f32)cur[0] / 32768.0f;
        const f32 cr = (f32)cur[1] / 32768.0f;
        const f32 nl = (f32)next[0] / 32768.0f;
        const f32 nr = (f32)next[1] / 32768.0f;
        const f32 phase = (f32)m_ResamplePhase;

        f32 l = (cl + phase * (nl - cl)) * gain;
        f32 r = (cr + phase * (nr - cr)) * gain;
        if (l > 1.0f) l = 1.0f; else if (l < -1.0f) l = -1.0f;
        if (r > 1.0f) r = 1.0f; else if (r < -1.0f) r = -1.0f;

        outp[0] = l;
        outp[1] = r;
        m_ResampleLastLeft = l / (gain != 0.0f ? gain : 1.0f);
        m_ResampleLastRight = r / (gain != 0.0f ? gain : 1.0f);
        outp += 2;
        produced++;

        m_ResamplePhase += step;
    }

    /* Underrun: hold the most recent real input sample (scaled by gain).
     * This preserves the longstanding no-click behaviour of the base ring
     * buffer and is preferable to emitting uninitialised / discontinuous
     * data when a DMA arrives late. */
    while (produced < frames)
    {
        outp[0] = m_ResampleLastLeft * gain;
        outp[1] = m_ResampleLastRight * gain;
        outp += 2;
        produced++;
    }

    return frames;
}

/* ---------------------------------------------------------------------------
 * ConsumeBuffered - discard data (NoSound driver)
 * ------------------------------------------------------------------------ */
u32 SoundDriver::ConsumeBuffered(u32 bytes)
{
    if (bytes == 0)
        return 0;

    if (m_SpeedFactor != 100)
    {
        u64 scaled = (u64)bytes * m_SpeedFactor / 100ull;
        if (scaled > MAX_SIZE) scaled = MAX_SIZE;
        if (scaled < bytes) scaled = bytes;
        bytes = (u32)scaled;
    }

    bytes &= ~3u;

    PluginLockGuard lck(m_mutex);
    if (bytes > m_BufferRemaining)
        bytes = m_BufferRemaining;
    bytes &= ~3u;
    m_CurrentReadLoc = (m_CurrentReadLoc + bytes) % MAX_SIZE;
    m_BufferRemaining -= bytes;

    /* Report what really left the ring: a request against a short or
     * empty ring discards less than was asked for. */
    return bytes;
}

/* ---------------------------------------------------------------------------
 * Startup / shutdown / reset
 * ------------------------------------------------------------------------ */
void SoundDriver::AI_Startup()
{
    Boolean backendOk = Initialize();

    {
        PluginLockGuard lck(m_mutex);
        m_CurrentReadLoc = 0;
        m_CurrentWriteLoc = 0;
        m_BufferRemaining = 0;
        m_LastSample = 0;
        m_ResampleLastLeft = 0.0f;
        m_ResampleLastRight = 0.0f;
        m_ResamplePhase = 0.0;
        m_DroppedBytes = 0;
        m_SpeedFactor = 100;
        m_SyncStallStrikes = 0;
        m_SyncDisabled = false;
    }

    m_audioIsInitialized = true;

    if (backendOk == TRUE)
    {
        StartAudio();
    }
    else
    {
        /* The backend could not start (e.g. DirectSound with no device).
         * Run a real-time fallback drain so the A/V sync throttle keeps
         * tracking wall-clock time and the emulator runs at full speed
         * instead of crawling behind a buffer nobody consumes. */
        DebugMessage(M64MSG_ERROR, "sound backend failed to initialize - running silent (real-time drain)");
        StartFallbackDrain();
    }

    SetVolume(m_Volume);
}

void SoundDriver::AI_Shutdown()
{
    /* 1. Stop feeding data to the driver's audio thread */
    StopAudio();
    /* 2. Tear down the hardware device (backends wait for their thread) */
    DeInitialize();
    /* 2b. Stop the fallback drain thread if one was started */
    StopFallbackDrain();
    /* 3. Reset the ring buffer so no stale audio is replayed later */
    {
        PluginLockGuard lck(m_mutex);
        m_BufferRemaining = 0;
        m_CurrentReadLoc = 0;
        m_CurrentWriteLoc = 0;
        m_LastSample = 0;
        m_ResampleLastLeft = 0.0f;
        m_ResampleLastRight = 0.0f;
        m_ResamplePhase = 0.0;
    }
    m_audioIsInitialized = false;
}

void SoundDriver::AI_ResetAudio()
{
    AI_Shutdown();
    AI_Startup();
}

/* ---------------------------------------------------------------------------
 * Speed factor / volume
 * ------------------------------------------------------------------------ */
void SoundDriver::AI_SetSpeedFactor(u32 speed)
{
    if (speed < 10) speed = 10;
    if (speed > 1000) speed = 1000;

    PluginLockGuard lck(m_mutex);
    m_SpeedFactor = speed;
}

void SoundDriver::SetVolume(u32 volume)
{
    if (volume > 100)
        volume = 100;
    m_Volume = volume;
}

/* ---------------------------------------------------------------------------
 * Fallback drain thread - keeps the A/V sync throttle alive when the real
 * audio backend could not start (XAudio 2.9 on Windows 7 / VxKex, missing
 * DirectSound device, WASAPI render failure, ...).  Consumes the ring
 * buffer at exactly real-time rate, exactly like NoSoundDriver does, so
 * the emulator runs at full speed while output is silent.
 *
 * Start/Stop are only ever called from the emulator/frontend thread under
 * l_DriverMutex (AI_Startup / AI_Shutdown / backend Setup failure paths),
 * so no extra locking is required against the entry points.
 * --------------------------------------------------------------------------- */
static u32 sd_tick_ms(void)
{
#ifdef _WIN32
    return (u32)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u32)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
#endif
}

static void sd_sleep_ms(u32 ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = (long)ms * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

void SoundDriver::DrainLoop()
{
    u32 lastTick = sd_tick_ms();

    while (m_FallbackRunning)
    {
        sd_sleep_ms(5);

        u32 now = sd_tick_ms();
        u32 elapsed = now - lastTick;
        if (elapsed == 0)
            continue;
        if (elapsed > 500)
            elapsed = 500;              /* catch-up guard */

        lastTick = now;

        u32 freq = m_SamplesPerSecond;
        if (freq == 0)
            freq = 44100;

        u64 bytes = (u64)freq * 4ull * elapsed / 1000ull;
        if (bytes > MAX_SIZE)
            bytes = MAX_SIZE;
        ConsumeBuffered((u32)bytes);
    }
}

#ifdef _WIN32
static DWORD WINAPI SoundDriverDrainThreadProc(LPVOID lpParameter)
{
    /* Accessor trick (same as NoSoundDriver): DrainLoop() is protected,
     * but the thread proc must be a plain static function. */
    class SoundDrainAccess : public SoundDriver
    {
    public:
        void Run() { DrainLoop(); }
    };
    SoundDrainAccess *driver = (SoundDrainAccess *)lpParameter;
    driver->Run();
    return 0;
}
#else
static void* SoundDriverDrainThreadProc(void *param)
{
    class SoundDrainAccess : public SoundDriver
    {
    public:
        void Run() { DrainLoop(); }
    };
    SoundDrainAccess *driver = (SoundDrainAccess *)param;
    driver->Run();
    return NULL;
}
#endif

void SoundDriver::StartFallbackDrain()
{
    if (m_FallbackRunning)
        return;

    m_FallbackRunning = true;

#ifdef _WIN32
    m_hDrainThread = CreateThread(NULL, 0, SoundDriverDrainThreadProc, this, 0, NULL);
    if (m_hDrainThread == NULL)
        m_FallbackRunning = false;
#else
    if (pthread_create(&m_hDrainThread, NULL, SoundDriverDrainThreadProc, this) != 0)
    {
        m_hDrainThread = 0;
        m_FallbackRunning = false;
    }
#endif
}

void SoundDriver::StopFallbackDrain()
{
    if (!m_FallbackRunning)
        return;

    m_FallbackRunning = false;

#ifdef _WIN32
    if (m_hDrainThread != NULL)
    {
        DWORD result = WaitForSingleObject(m_hDrainThread, 2000);
        if (result != WAIT_OBJECT_0)
        {
            TerminateThread(m_hDrainThread, 0);
            DebugMessage(M64MSG_WARNING, "fallback drain thread had to be terminated");
        }
        CloseHandle(m_hDrainThread);
        m_hDrainThread = NULL;
    }
#else
    if (m_hDrainThread != 0)
    {
        pthread_join(m_hDrainThread, NULL);
        m_hDrainThread = 0;
    }
#endif
}
