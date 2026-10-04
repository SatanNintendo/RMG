/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* XDrivers.h - X-Macro driver list                                          *
*                                                                           *
* NOTE: Deliberately NO "#pragma once" / include guard here!                *
* This is an X-Macro header, meant to be #included multiple times           *
* per translation unit with a different SOUND_DRIVER(name) definition       *
* each time.                                                                *
*                                                                           *
* The "legacy" DirectSound / XAudio2 variants of the original zilmar-spec   *
* plugin (ds8legacy / xa2legacy) relied on the plugin-side AI DMA FIFO      *
* emulation, which in Mupen64Plus is owned by the core.  They were          *
* architecturally impossible to port safely and have been removed;         *
* the configuration keys map to their modern counterparts (see              *
* Configuration::DriverTypeFromString).                                    *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

/* Windows-only drivers */
#ifdef _WIN32
SOUND_DRIVER(DirectSoundDriver)          /* DirectSound 8                  */
SOUND_DRIVER(XAudio2SoundDriver)         /* XAudio 2.7 (COM, redist)       */
SOUND_DRIVER(XAudio2SoundDriverModern)   /* XAudio 2.9 (xaudio2_9.dll)     */
SOUND_DRIVER(WASAPISoundDriver)          /* WASAPI shared mode             */
SOUND_DRIVER(WaveOutSoundDriver)         /* classic waveOut               */
#endif

/* Cross-platform drivers */
/* On non-Windows RMG builds the SDL2 backend is opt-in: the host frontend
   already owns SDL3 in the same process, so loading SDL2 is deliberately
   avoided by default. */
#ifndef _WIN32
#ifndef AZI_DISABLE_SDL2
SOUND_DRIVER(SDL2SoundDriver)
#endif
#endif

/* Always available */
SOUND_DRIVER(NoSoundDriver)
