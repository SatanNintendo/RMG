/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* SoundDriver.h - buffered audio sink base class                            *
*                                                                           *
* Architecture (Mupen64Plus native):                                        *
*   - AI_LenChanged() is called by the core when the emulated AI DMA        *
*     "plays" data.  The data in RDRAM must be consumed IMMEDIATELY:        *
*     the core restores the AI registers right after the call and the       *
*     game is free to overwrite that memory.  The old zilmar-spec flow      *
*     (storing RDRAM pointers and deferring the copy) is unsafe here.       *
*   - The core emulates the AI FIFO, DMA durations, the delayed carry and   *
*     raises the AI interrupt itself.  The plugin therefore never touches   *
*     AI_STATUS / MI_INTR / CheckInterrupts - doing so would corrupt the    *
*     core's interrupt state (double interrupts, hangs, crackling).         *
*   - A/V synchronisation is done by throttling the calling thread when     *
*     the ring buffer is fuller than the target level (same model as       *
*     mupen64plus-audio-sdl's synchronize_audio).                           *
*   - SpeedFactor (emulator fast-forward) is handled by consuming more      *
*     input data per output buffer, which keeps audio in sync with the     *
*     rest of the emulation at non-100% speeds.                             *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/
#pragma once

#include <atomic>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

#include "common.h"
#include "AudioSpec.h"
#include "SoundDriverInterface.h"
#include "PluginMutex.h"

class SoundDriver :
    public SoundDriverInterface
{
public:
    /* ---- Audio-spec entry points (called by the emulator core) ---- */
    void AI_SetFrequency(u32 Frequency) override;
    void AI_LenChanged(const u8 *start, u32 length) override;
    void AI_Startup() override;
    void AI_Shutdown() override;
    void AI_ResetAudio() override;
    void AI_SetSpeedFactor(u32 speed) override;

    void SetVolume(u32 volume) override;
    virtual ~SoundDriver();

    /* ---- Pull interface used by the backend implementations ---- */

    /* Copies up to 'length' bytes of ready audio into 'start'.
     * Returns the number of bytes actually produced.  On underrun the
     * output is padded by holding the last sample (prevents clicking).
     * Thread-safe: called from the driver's audio thread. */
    u32 LoadAiBuffer(u8 *start, u32 length);

    /* WASAPI variant: consumes N64-rate input and writes float samples,
     * resampling with a linear interpolator. 'ratio' is
     * n64_frequency / device_frequency. */
    u32 LoadAiBufferResample(u8 *start, u32 frames, float ratio);

    /* Volume, 0..100, 100 = unity */
    u32 GetVolume() const { return m_Volume; }

    /* Discards up to 'bytes' bytes from the head of the ring buffer
     * (honours the speed factor).  Used by the NoSound driver and by
     * the wall-clock paced backends to keep the sync throttle alive
     * without stalling on a slow device.  Returns the number of bytes
     * actually discarded (the request is clamped to the ring's current
     * level). */
    u32 ConsumeBuffered(u32 bytes);

protected:
    SoundDriver();

    /* Backend hooks */
    virtual Boolean Initialize() = 0;
    virtual void DeInitialize() = 0;
    virtual void SetFrequency(u32 Frequency) = 0;
    virtual void StartAudio() = 0;
    virtual void StopAudio() = 0;

    /* Fallback drain (silent real-time consumer) - used by AI_Startup()
     * and by backends whose real pipeline failed to start (e.g. XAudio
     * 2.9 on Windows 7).  Never call these from a derived destructor. */
    void StartFallbackDrain();
    void StopFallbackDrain();
    void DrainLoop();

    /* State */
    bool m_audioIsInitialized;
    bool m_isValid;

    /* Serialises ring-buffer access between the core thread (push) and
     * the backend audio thread (pull).  PluginMutex is a CRITICAL_SECTION
     * wrapper on Windows (see PluginMutex.h - keeps the MinGW build small)
     * and std::mutex elsewhere. */
    PluginMutex m_mutex;

    /* Ring buffer holding stereo 16-bit samples in host byte order,
     * already converted to the host L/R channel order.
     * One second at 44.1kHz stereo 16-bit (as in the original driver). */
    static const u32 MAX_SIZE = 44100 * 2 * 2;
    u8  m_Buffer[MAX_SIZE];
    u32 m_CurrentReadLoc;
    u32 m_CurrentWriteLoc;
    u32 m_BufferRemaining;

    /* Target fullness level used by the A/V sync throttle (bytes).
     * Computed from BUFFER_LEVEL / BUFFER_FPS configuration. */
    u32 m_SyncTargetBytes;

    /* Current N64 sample rate */
    u32 m_SamplesPerSecond;

    /* Emulator speed factor (100 = normal) */
    u32 m_SpeedFactor;

    /* Last sample written to the output, used for underrun hold */
    u32 m_LastSample;

    /* WASAPI resampler state.  This is deliberately per-driver (not static)
     * because the ring buffer and the audio lifecycle are per driver too.
     * The state is reset whenever the ring is reset / the DAC rate changes,
     * so a new stream can never inherit a fractional phase from an old one. */
    f32 m_ResampleLastLeft;
    f32 m_ResampleLastRight;
    f64 m_ResamplePhase;

    /* Volume 0..100 (100 = unity) */
    u32 m_Volume;

    /* Number of bytes dropped because the ring buffer overflowed
     * (diagnostics only, reset on frequency change). */
    u64 m_DroppedBytes;

    /* Stall guard: consecutive 1-second full-stall events in the A/V
     * sync throttle.  After 3 strikes the throttle is disabled for the
     * session (dead backend -> full-speed emulation, silent audio). */
    u32 m_SyncStallStrikes;
    bool m_SyncDisabled;

    /* Fallback drain thread state */
    volatile bool m_FallbackRunning;
#ifdef _WIN32
    HANDLE m_hDrainThread;
#else
    pthread_t m_hDrainThread;
#endif

private:
    /* Push 'length' bytes from RDRAM into the ring buffer (with the N64
     * R/L -> host L/R conversion).  Must be called with m_mutex held. */
    void PushSamples(const u8 *start, u32 length);

    /* Sleep helper shared by the sync throttle */
    void ThrottleSleepMs(u32 ms);

    /* Current effective sync target (scaled by speed factor) */
    u32 EffectiveSyncTarget() const;
};
