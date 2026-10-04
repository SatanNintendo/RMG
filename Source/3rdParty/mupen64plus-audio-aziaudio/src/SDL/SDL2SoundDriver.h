/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* SDL2 Sound Driver - replaces Windows-specific backends                    *
*                                                                           *
* License: GNU/GPLv2                                                        *
*                                                                           *
****************************************************************************/
#pragma once

#include <new>

#include "common.h"
#include "SoundDriver.h"

#include <SDL.h>
#include <SDL_audio.h>

class SDL2SoundDriver :
    public SoundDriver
{
public:
    SDL2SoundDriver();
    ~SDL2SoundDriver();

    /* SoundDriverInterface implementation */
    Boolean Initialize();
    void DeInitialize();

    void StopAudio();
    void StartAudio();
    void SetFrequency(u32 Frequency);

    void SetVolume(u32 volume);

    /* Factory method */
    static SoundDriverInterface* CreateSoundDriver() { return new (std::nothrow) SDL2SoundDriver(); }
    static bool ValidateDriver();

private:
    /* SDL2 audio device */
    SDL_AudioDeviceID m_audioDevice;
    SDL_AudioSpec m_audioSpec;

    /* Audio buffer management */
    u8 *m_mixBuffer;
    u32 m_mixBufferSize;
    u32 m_outputFrequency;
    /* Volume is stored by the SoundDriver base class */
    bool m_isPlaying;
    bool m_initialized;

    /* Secondary buffer size (SDL hardware buffer) */
    u32 m_secondaryBufferSize;

    /* SDL audio callback */
    static void SDLCALL AudioCallback(void *userdata, u8 *stream, int len);
    void FillAudioBuffer(u8 *stream, int len);
};
