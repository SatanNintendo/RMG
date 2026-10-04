/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* SDL2 Sound Driver implementation                                          *
*                                                                           *
* License: GNU/GPLv2                                                        *
*                                                                           *
* This driver replaces all Windows-specific backends (DirectSound, XAudio2, *
* WASAPI, WaveOut) with a cross-platform SDL2 implementation.               *
*                                                                           *
****************************************************************************/

#include "SDL2SoundDriver.h"
#include "SoundDriverRegistrar.h"
#include "Configuration.h"

#include <string.h>
#include <stdlib.h>

/* Register this driver with the factory */
REGISTER_DRIVER(SDL2SoundDriver, SND_DRIVER_SDL2, "SDL2 Audio Driver", 10)

/* Default buffer sizes */
#define DEFAULT_SECONDARY_BUFFER_SIZE 1024   /* SDL hardware buffer in samples */

bool SDL2SoundDriver::ValidateDriver()
{
    /* SDL2 is always available in Mupen64Plus context */
    return true;
}

SDL2SoundDriver::SDL2SoundDriver()
{
    m_audioDevice = 0;
    memset(&m_audioSpec, 0, sizeof(m_audioSpec));
    m_mixBuffer = NULL;
    m_mixBufferSize = 0;
    m_outputFrequency = 44100;
    m_isPlaying = false;
    m_initialized = false;

    m_secondaryBufferSize = DEFAULT_SECONDARY_BUFFER_SIZE;
}

SDL2SoundDriver::~SDL2SoundDriver()
{
    DeInitialize();
}

Boolean SDL2SoundDriver::Initialize()
{
    if (m_initialized)
        return TRUE;

    /* Initialize mix buffer (for volume control) */
    m_secondaryBufferSize = DEFAULT_SECONDARY_BUFFER_SIZE * 4; /* 16-bit stereo */
    m_mixBufferSize = m_secondaryBufferSize;
    m_mixBuffer = (u8*)malloc(m_mixBufferSize);
    if (m_mixBuffer == NULL)
    {
        return FALSE;
    }
    memset(m_mixBuffer, 0, m_mixBufferSize);

    m_initialized = true;
    m_audioIsInitialized = false;
    m_isValid = true;

    return TRUE;
}

void SDL2SoundDriver::DeInitialize()
{
    /* IMPORTANT: StopAudio must be called BEFORE closing device.
       SDL_CloseAudioDevice pauses and waits, but we also lock here
       to guarantee the callback is not running when we free buffers. */
    StopAudio();

    if (m_audioDevice > 0)
    {
        /* SDL_CloseAudioDevice will wait for any in-progress callback
           to finish before returning — safe to free memory after. */
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
    }

    /* Now safe to free the mix buffer — callback is gone */
    {
        std::lock_guard<std::mutex> lck(m_mutex);
        if (m_mixBuffer != NULL)
        {
            free(m_mixBuffer);
            m_mixBuffer = NULL;
            m_mixBufferSize = 0;
        }
    }

    m_initialized = false;
    m_audioIsInitialized = false;
}

void SDL2SoundDriver::SetFrequency(u32 Frequency)
{
    if (Frequency == 0)
        return;

    /* If same frequency and already open, nothing to do */
    if (Frequency == m_outputFrequency && m_audioDevice > 0)
        return;

    /* Pause and close the existing device before reopening */
    StopAudio();

    if (m_audioDevice > 0)
    {
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
    }

    m_outputFrequency = Frequency;

    /* Open SDL audio device with new frequency */
    SDL_AudioSpec desired;
    memset(&desired, 0, sizeof(desired));
    desired.freq     = (int)Frequency;
    desired.format   = AUDIO_S16SYS;  /* 16-bit signed, native byte order */
    desired.channels = 2;             /* Stereo */
    desired.samples  = DEFAULT_SECONDARY_BUFFER_SIZE;
    desired.callback = AudioCallback;
    desired.userdata = this;

    m_audioDevice = SDL_OpenAudioDevice(NULL, 0, &desired, &m_audioSpec,
                                        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);

    if (m_audioDevice == 0)
    {
        DebugMessage(M64MSG_ERROR, "SDL_OpenAudioDevice failed: %s", SDL_GetError());
        m_audioIsInitialized = false;
        return;
    }

    m_outputFrequency   = (u32)m_audioSpec.freq;
    m_audioIsInitialized = true;

    /* NOTE: the A/V sync target is recomputed by the base class
       (AI_SetFrequency) - nothing to scale here. */

    /* Resize the mix scratch buffer to match the SDL hardware buffer */
    u32 newMixSize = (u32)m_audioSpec.samples * 4; /* samples * 2ch * 2bytes */
    {
        std::lock_guard<std::mutex> lck(m_mutex);
        if (newMixSize != m_mixBufferSize)
        {
            u8 *newBuf = (u8*)realloc(m_mixBuffer, newMixSize);
            if (newBuf != NULL)
            {
                m_mixBuffer     = newBuf;
                m_mixBufferSize = newMixSize;
            }
        }
        if (m_mixBuffer)
            memset(m_mixBuffer, 0, m_mixBufferSize);
    }

    m_secondaryBufferSize = newMixSize;

    if (m_isPlaying)
        SDL_PauseAudioDevice(m_audioDevice, 0);
}

void SDL2SoundDriver::StartAudio()
{
    if (m_audioDevice == 0)
        return;

    m_isPlaying = true;
    SDL_PauseAudioDevice(m_audioDevice, 0); /* 0 = unpause / start */
}

void SDL2SoundDriver::StopAudio()
{
    if (m_audioDevice == 0)
        return;

    m_isPlaying = false;
    SDL_PauseAudioDevice(m_audioDevice, 1); /* 1 = pause */
}

void SDL2SoundDriver::SetVolume(u32 volume)
{
    SoundDriver::SetVolume(volume);
}

/* SDL Audio Callback - called by SDL from its own thread when it needs data */
void SDLCALL SDL2SoundDriver::AudioCallback(void *userdata, u8 *stream, int len)
{
    SDL2SoundDriver *driver = (SDL2SoundDriver *)userdata;
    driver->FillAudioBuffer(stream, len);
}

void SDL2SoundDriver::FillAudioBuffer(u8 *stream, int len)
{
    if (stream == NULL || len <= 0)
        return;

    u32 ulen = (u32)len;

    if (GetVolume() < 100)
    {
        /* Volume control path: load into scratch buffer, then mix with attenuation */
        {
            std::lock_guard<std::mutex> lck(m_mutex);

            /* Grow mix buffer if the SDL callback size changed */
            if (m_mixBuffer == NULL || m_mixBufferSize < ulen)
            {
                u8 *newBuf = (u8*)realloc(m_mixBuffer, ulen);
                if (newBuf == NULL)
                {
                    memset(stream, 0, len);
                    return;
                }
                m_mixBuffer     = newBuf;
                m_mixBufferSize = ulen;
            }
        }

        /* LoadAiBuffer acquires m_mutex internally */
        LoadAiBuffer(m_mixBuffer, ulen);

        memset(stream, 0, len);
        SDL_MixAudioFormat(stream, m_mixBuffer, AUDIO_S16SYS, ulen,
                           SDL_MIX_MAXVOLUME * (int)GetVolume() / 100);
    }
    else
    {
        /* Full volume — write directly into the SDL output buffer */
        LoadAiBuffer(stream, ulen);
    }
}
