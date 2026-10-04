/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* XAudio2ModernApi.h - minimal XAudio 2.9 (xaudio2_9.dll) declarations      *
*                                                                           *
* This header declares the XAudio2 2.8/2.9 object model with prefixed names *
* (M_*) so it can coexist in the same plugin with the classic XAudio 2.7    *
* headers bundled under 3rdParty/directx/include.                           *
*                                                                           *
* The engine is created through GetProcAddress("XAudio2Create") on          *
* xaudio2_9.dll (Windows 10/11). No import library and no COM class         *
* registration is required, and the 2.7-era CLSID (which needs the old      *
* DirectX redist) is not used at all.                                       *
*                                                                           *
* Interface layouts follow the XAudio 2.9 vtable order exactly:             *
*   IXAudio2:        RegisterForCallbacks, UnregisterForCallbacks,          *
*                    CreateSourceVoice, CreateSubmixVoice,                  *
*                    CreateMasteringVoice, StartEngine, StopEngine,         *
*                    CommitChanges, GetPerformanceData,                     *
*                    SetDebugConfiguration                                  *
*   IXAudio2Voice:   GetVoiceDetails, SetOutputVoices, SetEffectChain,      *
*                    EnableEffect, DisableEffect, GetEffectState,           *
*                    SetEffectParameters, GetEffectParameters,              *
*                    SetFilterParameters, GetFilterParameters,              *
*                    SetOutputFilterParameters, GetOutputFilterParameters,  *
*                    SetVolume, GetVolume, SetChannelVolumes,               *
*                    GetChannelVolumes, SetOutputMatrix, GetOutputMatrix,   *
*                    DestroyVoice                                           *
*   IXAudio2SourceVoice: Start, Stop, SubmitSourceBuffer,                   *
*                    FlushSourceBuffers, Discontinuity, ExitLoop,           *
*                    GetState, SetFrequencyRatio, GetFrequencyRatio,        *
*                    SetSourceSampleRate                                     *
*                                                                           *
*  FIX (frontend crash, verified via minidump analysis): IXAudio2Voice      *
*  does NOT derive from IUnknown in the real XAudio2 API - voices are not   *
*  ref-counted, DestroyVoice() replaces Release().  A previous revision     *
*  of this header declared QueryInterface/AddRef/Release on the voice       *
*  base interface, shifting every voice method 3 slots past its true        *
*  vtable position.  SetSourceSampleRate landed at slot 31, three           *
*  entries PAST the end of the real 29-entry source voice vtable, and       *
*  the call jumped to garbage memory, crashing the whole frontend (call     *
*  through vtable+0xF8 while the vtable ends at slot 28 / offset 0xE0).     *
*  The base now begins directly with GetVoiceDetails (slot 0), exactly      *
*  like the genuine xaudio2.h from the Windows SDK / mingw-w64.             *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/
#ifndef _XAUDIO2_MODERN_API_H_
#define _XAUDIO2_MODERN_API_H_

#pragma once

#include <windows.h>

/* WAVEFORMATEX: mmsystem.h comes with windows.h unless LEAN_AND_MEAN
   is defined; mmreg.h provides it otherwise. Both MSVC and MinGW safe. */
#ifndef _WAVEFORMATEX_
#include <mmreg.h>
#endif

#ifdef __cplusplus

/* ---------------------------------------------------------------------------
 * Constants (XAudio 2.8/2.9 values)
 * ------------------------------------------------------------------------ */
#define M_XAUDIO2_COMMIT_NOW           0u
#define M_XAUDIO2_END_OF_STREAM        0u      /* buffer flag: last buffer  */
#define M_XAUDIO2_NO_LOOP_REGION       0u
#define M_XAUDIO2_DEFAULT_FREQ_RATIO   2.0f
#define M_XAUDIO2_DEFAULT_CHANNELS     0u      /* mastering: match device   */
#define M_XAUDIO2_DEFAULT_SAMPLERATE   0u      /* mastering: match device   */

/* FIX: the previous revision of this header claimed "0" was the
   documented XAUDIO2_DEFAULT_PROCESSOR value for XAudio 2.8/2.9 and
   tried it first, falling back to 0xFFFFFFFF only on failure.  That is
   backwards.  Checked against the genuine mingw-w64 xaudio2.h *and*
   the real combined DirectX SDK header already bundled in this project
   under 3rdParty/directx/include/xaudio2.h (both define the enum
   identically):
       XAUDIO2_ANY_PROCESSOR     = 0xffffffff
       XAUDIO2_DEFAULT_PROCESSOR = XAUDIO2_ANY_PROCESSOR
   "0" is not a valid processor bitmask at all (no processor bit set).
   XAudio2Create() on real Windows 10/11 appears to tolerate it in
   practice (2.8+ dropped per-processor worker threads, so the engine
   likely ignores this argument entirely), which is almost certainly
   why this was never caught before - but it is still wrong, and it is
   cheap to do it the documented way.  The driver now tries the real
   documented default first and keeps "0" only as a defensive second
   attempt for oddball shims. */
#define M_XAUDIO2_ANY_PROCESSOR          0xffffffffu
#define M_XAUDIO2_DEFAULT_PROCESSOR      M_XAUDIO2_ANY_PROCESSOR
#define M_XAUDIO2_LEGACY_ZERO_PROCESSOR  0x00000000u

typedef UINT32 M_XAUDIO2_PROCESSOR;

/* CreateMasteringVoice Flags.  XAudio 2.9 (Windows 10+) opts every
   mastering voice created with DeviceId == NULL into a "virtual audio
   client": XAudio2 silently swaps the underlying WASAPI render device
   out from under the voice (e.g. on default-device changes) instead of
   surfacing a critical error.

   VERDICT FROM THE SLOWDOWN INVESTIGATION: this flag was the prime
   suspect for the "only XAudio 2.9 misbehaves" symptom and was tested
   both ways on the affected machine (CreateMasteringVoice with Flags=0
   and with M_XAUDIO2_NO_VIRTUAL_AUDIO_CLIENT) - the behaviour was
   IDENTICAL, so it is NOT the cause of anything observed so far.  The
   flag is kept defined here for reference/future experiments, but the
   driver now calls CreateMasteringVoice with Flags=0 (the documented
   default).  The real fix for the slowdown was the wall-clock pacing
   redesign of XAudio2SoundDriverModern (see that file's header). */
#define M_XAUDIO2_NO_VIRTUAL_AUDIO_CLIENT   0x10000u

/* IXAudio2SourceVoice::GetState Flags.  XAUDIO2_VOICE_NOSAMPLESPLAYED
   (value 0x0100, verified against the genuine Windows 10 SDK
   xaudio2.h - NOT the 0x0008 that was briefly assumed during the
   investigation) skips the SamplesPlayed computation, which the
   XAudio2 documentation says makes the call ~3x cheaper.  Used by the
   driver's periodic diagnostics where only BuffersQueued is needed. */
#define M_XAUDIO2_VOICE_NOSAMPLESPLAYED     0x0100u

/* audio stream category for CreateMasteringVoice (2.9 parameter) */
typedef enum M_AUDIO_STREAM_CATEGORY
{
    M_AudioCategory_Other             = 0,
    M_AudioCategory_ForegroundOnlyMedia,
    M_AudioCategory_BackgroundCapableMedia,
    M_AudioCategory_Communications,
    M_AudioCategory_Alerts,
    M_AudioCategory_SoundEffects,
    M_AudioCategory_GameEffects,
    M_AudioCategory_GameMedia,
    M_AudioCategory_GameChat,
    M_AudioCategory_Speech,
    M_AudioCategory_Media,
    M_AudioCategory_Movie,
    M_AudioCategory_MediaStream
} M_AUDIO_STREAM_CATEGORY;

/* ---------------------------------------------------------------------------
 * Structures
 *
 * No #pragma pack here: both structures must keep NATURAL alignment so
 * that pContext sits at offset 40 in M_XAUDIO2_BUFFER and SamplesPlayed at
 * offset 16 in M_XAUDIO2_VOICE_STATE - exactly where the real XAudio2
 * implementation reads/writes them.  (A packed copy of these structs
 * previously made the engine read pContext out of the middle of
 * LoopCount and write SamplesPlayed 4 bytes into the caller's stack.)
 * ------------------------------------------------------------------------ */

typedef struct M_XAUDIO2_BUFFER
{
    UINT32          Flags;              /* M_XAUDIO2_END_OF_STREAM or 0     */
    UINT32          AudioBytes;         /* size of pAudioData in bytes      */
    const BYTE     *pAudioData;         /* PCM data (must stay valid until  */
                                        /* OnBufferEnd)                     */
    UINT32          PlayBegin;
    UINT32          PlayLength;
    UINT32          LoopBegin;
    UINT32          LoopLength;
    UINT32          LoopCount;
    void           *pContext;          /* natural alignment: offset 40 */
} M_XAUDIO2_BUFFER;

typedef struct M_XAUDIO2_VOICE_STATE
{
    void           *pCurrentBufferContext;
    UINT32          BuffersQueued;             /* offset  8 */
    UINT64          SamplesPlayed;             /* offset 16 */
} M_XAUDIO2_VOICE_STATE;

/* ---------------------------------------------------------------------------
 * Interfaces
 * ------------------------------------------------------------------------ */

struct M_IXAudio2;
struct M_IXAudio2Voice;
struct M_IXAudio2SourceVoice;
struct M_IXAudio2MasteringVoice;
struct M_IXAudio2VoiceCallback;

/* FIX (XAudio 2.9 "no sound/crash", verified against the genuine mingw-w64
 * and DirectX SDK xaudio2.h): the XAudio2 callback interfaces are NOT
 * IUnknown-derived.  Both the SDK and mingw-w64 declare them with
 * DECLARE_INTERFACE (no base), so the vtable begins DIRECTLY with
 * OnVoiceProcessingPassStart (voice) / OnProcessingPassStart (engine).
 * A previous revision of this header declared QueryInterface/AddRef/
 * Release first, shifting every callback three slots: the engine calls
 * vtable slot 0 (OnVoiceProcessingPassStart, i.e. every ~10 ms processing
 * pass) and landed in QueryInterface - which wrote NULL through a garbage
 * ppvObject register argument and crashed the frontend on XAudio's own
 * processing thread.  The genuine IXAudio2VoiceCallback vtable has EXACTLY
 * 7 entries, IXAudio2EngineCallback exactly 3.  Callbacks are not
 * ref-counted: the client must simply keep the object alive while the
 * voice exists (see DestroyVoice below). */
struct M_IXAudio2VoiceCallback
{
    virtual void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32 BytesRequired) = 0;
    virtual void STDMETHODCALLTYPE OnVoiceProcessingPassEnd(void) = 0;
    virtual void STDMETHODCALLTYPE OnStreamEnd(void) = 0;
    virtual void STDMETHODCALLTYPE OnBufferStart(void *pBufferContext) = 0;
    virtual void STDMETHODCALLTYPE OnBufferEnd(void *pBufferContext) = 0;
    virtual void STDMETHODCALLTYPE OnLoopEnd(void *pBufferContext) = 0;
    virtual void STDMETHODCALLTYPE OnVoiceError(void *pBufferContext, HRESULT Error) = 0;
};

struct M_IXAudio2EngineCallback
{
    virtual void STDMETHODCALLTYPE OnProcessingPassStart(void) = 0;
    virtual void STDMETHODCALLTYPE OnProcessingPassEnd(void) = 0;
    virtual void STDMETHODCALLTYPE OnCriticalError(HRESULT Error) = 0;
};

/* WARNING: no IUnknown on this base!  XAudio2 voices are not ref-counted
 * (DestroyVoice() replaces Release()); the vtable begins directly with
 * GetVoiceDetails at slot 0, exactly as in the genuine xaudio2.h.  See
 * the note in the file header for the crash this used to cause. */
struct M_IXAudio2Voice
{
    virtual void STDMETHODCALLTYPE GetVoiceDetails(void *pVoiceDetails) = 0;             /* XAUDIO2_VOICE_DETAILS* */
    virtual HRESULT STDMETHODCALLTYPE SetOutputVoices(const void *pSendList) = 0;        /* XAUDIO2_VOICE_SENDS*   */
    virtual HRESULT STDMETHODCALLTYPE SetEffectChain(const void *pEffectChain) = 0;      /* XAUDIO2_EFFECT_CHAIN*  */
    virtual HRESULT STDMETHODCALLTYPE EnableEffect(UINT32 EffectIndex, UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual HRESULT STDMETHODCALLTYPE DisableEffect(UINT32 EffectIndex, UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual void STDMETHODCALLTYPE GetEffectState(UINT32 EffectIndex, BOOL *pEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEffectParameters(UINT32 EffectIndex, const void *pParameters, UINT32 ParametersByteSize, UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetEffectParameters(UINT32 EffectIndex, void *pParameters, UINT32 ParametersByteSize) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFilterParameters(const void *pParameters, UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual void STDMETHODCALLTYPE GetFilterParameters(void *pParameters) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOutputFilterParameters(M_IXAudio2Voice *pDestinationVoice, const void *pParameters, UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual void STDMETHODCALLTYPE GetOutputFilterParameters(M_IXAudio2Voice *pDestinationVoice, void *pParameters) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetVolume(float Volume, UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual void STDMETHODCALLTYPE GetVolume(float *pVolume) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetChannelVolumes(UINT32 Channels, const float *pVolumes, UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual void STDMETHODCALLTYPE GetChannelVolumes(UINT32 Channels, float *pVolumes) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOutputMatrix(M_IXAudio2Voice *pDestinationVoice, UINT32 SourceChannels, UINT32 DestinationChannels, const float *pLevelMatrix, UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual void STDMETHODCALLTYPE GetOutputMatrix(M_IXAudio2Voice *pDestinationVoice, UINT32 SourceChannels, UINT32 DestinationChannels, float *pLevelMatrix) = 0;
    virtual void STDMETHODCALLTYPE DestroyVoice(void) = 0;
};

struct M_IXAudio2SourceVoice : public M_IXAudio2Voice
{
    virtual HRESULT STDMETHODCALLTYPE Start(UINT32 Flags = 0, UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop(UINT32 Flags = 0, UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual HRESULT STDMETHODCALLTYPE SubmitSourceBuffer(const M_XAUDIO2_BUFFER *pBuffer, const void *pBufferWMA = NULL) = 0;
    virtual HRESULT STDMETHODCALLTYPE FlushSourceBuffers(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE Discontinuity(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE ExitLoop(UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual void STDMETHODCALLTYPE GetState(M_XAUDIO2_VOICE_STATE *pVoiceState, UINT32 Flags = 0) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFrequencyRatio(float Ratio, UINT32 OperationSet = M_XAUDIO2_COMMIT_NOW) = 0;
    virtual void STDMETHODCALLTYPE GetFrequencyRatio(float *pRatio) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSourceSampleRate(UINT32 NewSourceSampleRate) = 0;
};

struct M_IXAudio2MasteringVoice : public M_IXAudio2Voice
{
    virtual HRESULT STDMETHODCALLTYPE GetChannelMask(DWORD *pChannelMask) = 0;
};

struct M_IXAudio2
{
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(const IID *riid, void **ppvObject) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef(void) = 0;
    virtual ULONG STDMETHODCALLTYPE Release(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE RegisterForCallbacks(M_IXAudio2EngineCallback *pCallback) = 0;
    virtual void STDMETHODCALLTYPE UnregisterForCallbacks(M_IXAudio2EngineCallback *pCallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateSourceVoice(
        M_IXAudio2SourceVoice **ppSourceVoice,
        const WAVEFORMATEX *pSourceFormat,
        UINT32 Flags = 0,
        float MaxFrequencyRatio = M_XAUDIO2_DEFAULT_FREQ_RATIO,
        M_IXAudio2VoiceCallback *pCallback = NULL,
        const void *pSendList = NULL,
        const void *pEffectChain = NULL) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateSubmixVoice(
        void **ppSubmixVoice,
        UINT32 InputChannels,
        UINT32 InputSampleRate,
        UINT32 Flags = 0,
        UINT32 ProcessingStage = 0,
        const void *pSendList = NULL,
        const void *pEffectChain = NULL) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateMasteringVoice(
        M_IXAudio2MasteringVoice **ppMasteringVoice,
        UINT32 InputChannels = M_XAUDIO2_DEFAULT_CHANNELS,
        UINT32 InputSampleRate = M_XAUDIO2_DEFAULT_SAMPLERATE,
        UINT32 Flags = 0,
        LPCWSTR DeviceId = NULL,
        const void *pEffectChain = NULL,
        M_AUDIO_STREAM_CATEGORY category = M_AudioCategory_GameEffects) = 0;
    virtual HRESULT STDMETHODCALLTYPE StartEngine(void) = 0;
    virtual void STDMETHODCALLTYPE StopEngine(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE CommitChanges(UINT32 OperationSet) = 0;
    virtual void STDMETHODCALLTYPE GetPerformanceData(void *pPerfData) = 0;
    virtual void STDMETHODCALLTYPE SetDebugConfiguration(const void *pDebugConfiguration, void *pReserved = NULL) = 0;
};

/* DLL entry point (resolved dynamically from xaudio2_9.dll)
 * ------------------------------------------------------------------------ */
typedef HRESULT (WINAPI *M_PFN_XAudio2Create)(M_IXAudio2 **ppXAudio2, UINT32 Flags, M_XAUDIO2_PROCESSOR XAudio2Processor);

/* ---------------------------------------------------------------------------
 * ABI guards: these layouts MUST match the real XAudio2 structures exactly.
 * A regression here corrupts every buffer submission / state query.
 * ------------------------------------------------------------------------ */
static_assert(offsetof(M_XAUDIO2_BUFFER, pAudioData) == 8,  "XAUDIO2_BUFFER ABI");
static_assert(offsetof(M_XAUDIO2_BUFFER, pContext)   == 40, "XAUDIO2_BUFFER ABI");
static_assert(sizeof(M_XAUDIO2_BUFFER)               == 48, "XAUDIO2_BUFFER ABI");
static_assert(offsetof(M_XAUDIO2_VOICE_STATE, BuffersQueued) == 8,  "XAUDIO2_VOICE_STATE ABI");
static_assert(offsetof(M_XAUDIO2_VOICE_STATE, SamplesPlayed) == 16, "XAUDIO2_VOICE_STATE ABI");
static_assert(sizeof(M_XAUDIO2_VOICE_STATE)           == 24, "XAUDIO2_VOICE_STATE ABI");

/* Vtable slot map (verified against the crash dump fix and objdump):
 *   M_IXAudio2Voice         slots 0..18   (GetVoiceDetails .. DestroyVoice)
 *   M_IXAudio2SourceVoice   slots 19..28  (Start .. SetSourceSampleRate)
 *   M_IXAudio2MasteringVoice slot  19     (GetChannelMask)
 *   M_IXAudio2              slots 0..12   (IUnknown + engine methods)
 *   M_IXAudio2VoiceCallback slots 0..6    (OnVoiceProcessingPassStart ..
 *                                          OnVoiceError - NO IUnknown!)
 *   M_IXAudio2EngineCallback slots 0..2   (OnProcessingPassStart ..
 *                                          OnCriticalError - NO IUnknown!)
 * NOTE: sizeof() cannot count vtable entries (a polymorphic class without
 * data members is always pointer-sized); if methods are added/removed the
 * disassembly of the call sites (call *0xNN(%rax)) is the authoritative
 * check - SetSourceSampleRate must be called at 0xE0, Start at 0x98,
 * SubmitSourceBuffer at 0xA8, GetState at 0xC8, DestroyVoice at 0x90. */

#endif /* __cplusplus */

#endif /* _XAUDIO2_MODERN_API_H_ */
