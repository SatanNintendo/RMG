/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-sdl-audio - sdl_backend.c                                 *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2017 Bobby Smiles                                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

#include <stdlib.h>
#include <string.h>

#include "Resamplers/resamplers.hpp"
#include "main.hpp"

#include <RMG-Core/m64p/api/m64p_types.h>

#include <RMG-Core/Settings.hpp>
#include <RMG-Core/Netplay.hpp>

/* number of bytes per sample */
#define N64_SAMPLE_BYTES 4
#define SDL_SAMPLE_BYTES 4

struct sdl_backend
{
    /* Audio Stream */
    SDL_AudioStream* stream;

    /* Primary Buffer */
    void* primary_buffer;

    /* Resampling buffer */
    void* resample_buffer;

    /* Size of buffers */
    size_t buffers_size;

    unsigned int frequency;

    unsigned int speed_factor;

    unsigned int swap_channels;

    unsigned int error;

    float volume;

    /* Smoothed (EMA filtered) estimate of how many bytes are currently
     * queued up in the SDL audio stream. Used by the drift-correction
     * logic in sdl_push_samples() below. */
    double smoothed_queued_bytes;

    /* Whether smoothed_queued_bytes has been initialized yet */
    bool queued_bytes_primed;

    /* Resampler */
    void* resampler;
    const struct resampler_interface* iresampler;
};

/* SDL_AudioFormat.format format specifier and args builder */
#define AFMT_FMTSPEC        "%c%d%s"
#define AFMT_ARGS(x) \
        ((SDL_AUDIO_ISFLOAT(x)) ? 'F' : (SDL_AUDIO_ISSIGNED(x)) ? 'S' : 'U'), \
        SDL_AUDIO_BITSIZE(x), \
        SDL_AUDIO_ISBIGENDIAN(x) ? "BE" : "LE"

static void sdl_init_audio_device(struct sdl_backend* sdl_backend)
{
    SDL_AudioSpec spec;

    sdl_backend->error = 0;

    if (SDL_WasInit(SDL_INIT_AUDIO))
    {
        DebugMessage(M64MSG_VERBOSE, "sdl_init_audio_device(): SDL Audio sub-system already initialized.");
        SDL_DestroyAudioStream(sdl_backend->stream);
    }
    else
    {
        if (!SDL_Init(SDL_INIT_AUDIO))
        {
            DebugMessage(M64MSG_ERROR, "Failed to initialize SDL audio subsystem: %s", SDL_GetError());
            sdl_backend->error = 1;
            return;
        }
    }

    DebugMessage(M64MSG_INFO, "Initializing SDL audio subsystem...");

    memset(&spec, 0, sizeof(spec));
    spec.freq     = sdl_backend->frequency;
    spec.format   = SDL_AUDIO_S16LE;
    spec.channels = 2;

    /* Open the audio device */
    sdl_backend->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (sdl_backend->stream == NULL)
    {
        DebugMessage(M64MSG_ERROR, "Couldn't open audio stream: %s", SDL_GetError());
        sdl_backend->error = 1;
        return;
    }

    DebugMessage(M64MSG_VERBOSE, "Frequency: %i", spec.freq);
    DebugMessage(M64MSG_VERBOSE, "Format: " AFMT_FMTSPEC, AFMT_ARGS(spec.format));
    DebugMessage(M64MSG_VERBOSE, "Channels: %i", spec.channels);

    /* start audio stream */
    SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(sdl_backend->stream));

    /* reset drift-correction state: a freshly (re)opened stream starts
     * empty, and any previously applied frequency ratio is gone with the
     * old stream object, so make sure our tracking matches reality */
    sdl_backend->smoothed_queued_bytes = 0.0;
    sdl_backend->queued_bytes_primed = false;
}

static void release_audio_device(struct sdl_backend* sdl_backend)
{
    if (SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_DestroyAudioStream(sdl_backend->stream);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

struct sdl_backend* init_sdl_backend(void)
{
    /* allocate memory for sdl_backend */
    struct sdl_backend* sdl_backend = (struct sdl_backend*)malloc(sizeof(*sdl_backend));
    if (sdl_backend == nullptr) {
        return nullptr;
    }

    /* reset sdl_backend */
    memset(sdl_backend, 0, sizeof(*sdl_backend));

    /* instanciate resampler */
    std::string resampler_id = CoreSettingsGetStringValue(SettingsID::Audio_Resampler);
    void* resampler = nullptr;
    const struct resampler_interface* iresampler = get_iresampler(resampler_id.c_str(), &resampler);
    if (iresampler == nullptr) {
        free(sdl_backend);
        return nullptr;
    }

    sdl_backend->frequency = CoreSettingsGetIntValue(SettingsID::Audio_DefaultFrequency);
    sdl_backend->swap_channels = CoreSettingsGetBoolValue(SettingsID::Audio_SwapChannels);
    sdl_backend->speed_factor = 100;
    sdl_backend->resampler = resampler;
    sdl_backend->iresampler = iresampler;

    sdl_init_audio_device(sdl_backend);

    return sdl_backend;
}

void sdl_apply_settings(struct sdl_backend* sdl_backend)
{
    sdl_backend->frequency = CoreSettingsGetIntValue(SettingsID::Audio_DefaultFrequency);
    sdl_backend->swap_channels = CoreSettingsGetBoolValue(SettingsID::Audio_SwapChannels);
}

void release_sdl_backend(struct sdl_backend* sdl_backend)
{
    if (sdl_backend == nullptr) {
        return;
    }

    if (sdl_backend->error == 0) {
        release_audio_device(sdl_backend);
    }

    /* release primary buffer */
    if (sdl_backend->primary_buffer != nullptr) {
        free(sdl_backend->primary_buffer);
    }

    /* release mix buffer */
    if (sdl_backend->resample_buffer != nullptr) {
        free(sdl_backend->resample_buffer);
    }

    /* release resampler */
    sdl_backend->iresampler->release(sdl_backend->resampler);

    /* release sdl backend */
    free(sdl_backend);
}

void sdl_set_frequency(struct sdl_backend* sdl_backend, unsigned int frequency)
{
    if (sdl_backend->error != 0)
        return;

    sdl_backend->frequency = frequency;
    sdl_init_audio_device(sdl_backend);
}

void sdl_push_samples(struct sdl_backend* sdl_backend, const void* src, size_t size)
{
    if (sdl_backend->error != 0)
        return;

    /* truncate to full samples */
    if (size & 0x3) {
        DebugMessage(M64MSG_VERBOSE, "sdl_push_samples: pushing non full samples: %zu bytes !", size);
    }
    size = (size / 4) * 4;

    if (size == 0)
        return;

    /*
     * --- Audio/video clock drift correction ---
     *
     * The emulated machine is paced against the *video* clock (the core's
     * speed limiter sleeps based on SDL_GetTicks() to hit the expected VI
     * rate). Audio samples are generated by the emulated CPU and pushed
     * here as a *side effect* of that video-paced loop, while the actual
     * host sound card drains the queue at its own, independent hardware
     * clock rate.
     *
     * Even when both clocks are nominally correct, no two independent
     * clocks run at *exactly* the same rate. Over time this tiny
     * difference accumulates and the queued audio will slowly drift
     * either up (we produce faster than the device consumes) or down
     * (we produce slower than the device consumes). Left unhandled, this
     * eventually leads to either a buffer overflow or an underrun -
     * both of which cause an audible discontinuity ("click"/"pop") in
     * the output. This happens in essentially every game, regardless of
     * audio buffer size or resampler choice, because it's not a
     * buffering problem - it's a synchronization problem. It also
     * explains why it's irregular and infrequent: the size of the drift
     * depends on the actual (tiny) clock mismatch on a given machine, so
     * it can take anywhere from several seconds to much longer to
     * accumulate enough to matter.
     *
     * To fix this without ever dropping or starving audio, we
     * continuously nudge SDL's audio stream frequency ratio (a tiny,
     * inaudible pitch/speed adjustment well under 1%) to keep the queued
     * amount hovering around a small target latency. This is the same
     * "dynamic rate control" technique used by other emulators (Dolphin,
     * RetroArch, etc.) to keep audio perfectly smooth without any
     * audible artifacts.
     */

    if (sdl_backend->stream != nullptr)
    {
        int queuedBytes = SDL_GetAudioStreamQueued(sdl_backend->stream);

        /* Target ~80ms of buffered audio: enough headroom to absorb
         * jitter without adding noticeable input lag. */
        const double targetBytes = (double)sdl_backend->frequency * SDL_SAMPLE_BYTES * 0.08;

        /* Safety net: if something pathological happens (savestate load,
         * pause/resume, fast-forward toggling, a stalled audio device,
         * etc.) and the queue balloons way past the target, the gentle
         * ratio correction below would take too long to catch up and we
         * would just keep falling further behind real-time. In that case
         * only, flush the stream and start clean rather than let latency
         * grow unbounded. This should not trigger during normal gameplay. */
        const double maxBytes = targetBytes * 8.0;
        if ((double)queuedBytes > maxBytes)
        {
            SDL_ClearAudioStream(sdl_backend->stream);
            SDL_SetAudioStreamFrequencyRatio(sdl_backend->stream, 1.0f);
            queuedBytes = 0;
            sdl_backend->queued_bytes_primed = false;
        }

        /* Smooth the measured queue level (it's noisy: AI DMA blocks
         * arrive in irregular bursts) before feeding it to the
         * correction below, so we react to the underlying trend rather
         * than to every individual block. */
        if (!sdl_backend->queued_bytes_primed)
        {
            sdl_backend->smoothed_queued_bytes = (double)queuedBytes;
            sdl_backend->queued_bytes_primed = true;
        }
        else
        {
            sdl_backend->smoothed_queued_bytes =
                sdl_backend->smoothed_queued_bytes * 0.99 + (double)queuedBytes * 0.01;
        }

        /* Proportional correction: queue running high -> play slightly
         * faster to drain it; queue running low -> play slightly slower
         * to let it refill. Clamped so the pitch shift always stays well
         * below the threshold of audibility. */
        double error = (sdl_backend->smoothed_queued_bytes - targetBytes) / targetBytes;
        double ratio = 1.0 + (error * 0.01);

        if (ratio < 0.98) ratio = 0.98;
        if (ratio > 1.02) ratio = 1.02;

        SDL_SetAudioStreamFrequencyRatio(sdl_backend->stream, (float)ratio);
    }

    /* resize buffers when required */
    if (size > sdl_backend->buffers_size)
    {
        sdl_backend->primary_buffer = realloc(sdl_backend->primary_buffer, size);
        sdl_backend->resample_buffer = realloc(sdl_backend->resample_buffer, size);

        sdl_backend->buffers_size = size;
    }

    /* Confusing logic but, for LittleEndian host using memcpy will result in swapped channels,
     * whereas the other branch will result in non-swapped channels.
     * For BigEndian host this logic is inverted, memcpy will result in non swapped channels
     * and the other branch will result in swapped channels.
     *
     * This is due to the fact that the core stores 32bit words in native order in RDRAM.
     * For instance N64 bytes "Lh Ll Rh Rl" will be stored as "Rl Rh Ll Lh" on LittleEndian host
     * and therefore should the non-memcpy path to get non swapped channels,
     * whereas on BigEndian host the bytes will be stored as "Lh Ll Rh Rl" and therefore
     * memcpy path results in the non-swapped channels outcome.
     */
    if (sdl_backend->swap_channels ^ (SDL_BYTEORDER == SDL_BIG_ENDIAN)) {
        memcpy(sdl_backend->primary_buffer, src, size);
    }
    else {
        size_t i;
        for (i = 0 ; i < size ; i += 4 )
        {
            memcpy((unsigned char*)sdl_backend->primary_buffer + i + 0, (const unsigned char*)src + i + 2, 2); /* Left */
            memcpy((unsigned char*)sdl_backend->primary_buffer + i + 2, (const unsigned char*)src + i + 0, 2); /* Right */
        }
    }

    /* resample audio */
    sdl_backend->iresampler->resample(sdl_backend->resampler, 
                                        sdl_backend->primary_buffer, size, 
                                        sdl_backend->frequency, 
                                        sdl_backend->resample_buffer, size,
                                        sdl_backend->frequency);

    /* push audio buffer to SDL */
    SDL_PutAudioStreamData(sdl_backend->stream, sdl_backend->resample_buffer, size);
}

void sdl_set_speed_factor(struct sdl_backend* sdl_backend, unsigned int speed_factor)
{
    if (speed_factor < 10 || speed_factor > 300)
        return;

    sdl_backend->speed_factor = speed_factor;
}

void sdl_apply_volume(struct sdl_backend* sdl_backend, float vol)
{
    if (sdl_backend->stream != nullptr)
    {
        SDL_SetAudioStreamGain(sdl_backend->stream, vol);
    }
}
