/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* DirectSoundDriver - DirectSound 8 backend                                 *
*                                                                           *
* Pull model: a dedicated audio thread locks a segment of the circular     *
* DirectSound buffer and fills it from the SoundDriver ring buffer         *
* (LoadAiBuffer).  The plugin-side AI DMA FIFO of the original zilmar      *
* driver is gone - the Mupen64Plus core owns the AI emulation.             *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#pragma once

#ifdef _WIN32

#include "common.h"
#if defined(_WIN32) && !defined(_XBOX)
#include <mmreg.h>
#endif
#include "3rdParty/directx/include/dsound.h"
#include "PluginMutex.h"
#include <new>
#include "SoundDriver.h"
#include "SoundDriverInterface.h"

/* DirectSound streaming buffer geometry.
 *
 * NOTE: LOCK_SIZE was previously a macro expanding to a FILE-SCOPE
 * static in the .cpp - while DirectSoundDriver ALSO declared a member
 * with the identical name that SetFrequency() wrote.  Member lookup
 * hid the static inside member functions, but the free function
 * AudioThreadProc() saw the never-written static (always 0).  The
 * compiler folded (play_pos / 0) * 0 - 0 to a constant 0, the audio
 * thread's write-position never changed and the buffer was never
 * filled: total silence with DirectSound.  All audio-thread state is
 * now held as class members so no name can ever resolve two
 * different objects again. */
#define DS_SEGMENTS     4

class DirectSoundDriver :
    public SoundDriver
{
protected:
    BOOL audioIsPlaying;
    HANDLE handleAudioThread;
    DWORD  dwAudioThreadId;
    PluginMutex dsMutex;
    LPDIRECTSOUNDBUFFER  lpdsbuf;      /* secondary (streaming) buffer */
    LPDIRECTSOUNDBUFFER  lpdsb;        /* primary buffer (volume ctrl) */
    LPDIRECTSOUND8       lpds;
    bool audioIsDone;
    DWORD SampleRate;
    DWORD SegmentSize;

    /* Segment size in bytes - written by SetFrequency() BEFORE the
       streaming buffer is created, read by the audio thread. */
    DWORD m_dwLockSize;

    /* Audio-thread cursor state (class members, one per driver
       instance - see the note on LOCK_SIZE above). */
    DWORD m_dwLastPos;
    DWORD m_dwWritePos;
    DWORD m_dwPlayPos;
    DWORD m_dwNextPos;
    LPVOID m_lpvLock1;
    LPVOID m_lpvLock2;
    DWORD  m_dwLockBytes1;
    DWORD  m_dwLockBytes2;

public:
    friend DWORD WINAPI AudioThreadProc(DirectSoundDriver *ac);

    DirectSoundDriver();
    ~DirectSoundDriver() { DeInitialize(); }

    /* Setup and Teardown Functions */
    BOOL Initialize();
    void DeInitialize();

    /* Buffer Functions for the Audio Code */
    void SetFrequency(u32 Frequency);          /* N64 sample rate        */
    void SetSegmentSize(DWORD length);

    /* Management functions */
    void StopAudio();
    void StartAudio();

    void SetVolume(u32 volume);

    static SoundDriverInterface* CreateSoundDriver() { return new (std::nothrow) DirectSoundDriver(); }
    static bool ValidateDriver();
};

#endif // _WIN32
