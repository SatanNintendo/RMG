/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* Configuration.cpp - settings through the Mupen64Plus Core Config API      *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#include "Configuration.h"
#include "common.h"
#include "SoundDriverFactory.h"

#include <string.h>

/* Static member initialization (defaults - replaced by LoadSettings).
 * VOLUME 1.0.13: 100 = full gain, the loudness the original AziAudio-Plus
 * shipped with (its inverted 0=full scale defaulted to 0). */
unsigned long Configuration::configVolume = 100;
SoundDriverType Configuration::configDriver = SND_DRIVER_NOSOUND;
bool Configuration::configSyncAudio = true;
bool Configuration::configForceSync = false;
unsigned long Configuration::configBufferLevel = 3;
unsigned long Configuration::configBufferFPS = 45;
unsigned long Configuration::configBackendFPS = 90;
bool Configuration::configResTimer = true;
bool Configuration::configDisallowSleepXA2 = false;
bool Configuration::configDisallowSleepDS8 = false;

bool Configuration::RomRunning = false;

/* External config function pointers and handle from main.cpp */
extern ptr_ConfigGetParamInt      ConfigGetParamInt;
extern ptr_ConfigGetParamBool     ConfigGetParamBool;
extern ptr_ConfigGetParamString   ConfigGetParamString;
extern m64p_handle l_ConfigAudio;

/* ---------------------------------------------------------------------------
 * Driver type string <-> enum conversion
 * ------------------------------------------------------------------------ */
SoundDriverType Configuration::DriverTypeFromString(const char* str)
{
    if (str == NULL)
        return SoundDriverFactory::DefaultDriver();

    if (strcmp(str, DRIVER_DEFAULT_STR) == 0)
        return SoundDriverFactory::DefaultDriver();

#ifdef _WIN32
    /* Legacy keys from the original zilmar-spec plugin: the "legacy"
       driver variants were built around plugin-side AI FIFO emulation
       (owned by the Mupen64Plus core now) and were removed - map them
       to their modern counterparts. */
    if (strcmp(str, DRIVER_DS8L_STR) == 0)     { DebugMessage(M64MSG_INFO, "ds8legacy maps to ds8 (legacy variant removed)");   return SND_DRIVER_DS8; }
    if (strcmp(str, DRIVER_XA2L_STR) == 0)     { DebugMessage(M64MSG_INFO, "xa2legacy maps to xa2 (legacy variant removed)");   return SND_DRIVER_XA2; }
    if (strcmp(str, DRIVER_DS8_STR) == 0)      return SND_DRIVER_DS8;
    if (strcmp(str, DRIVER_XA2_STR) == 0)      return SND_DRIVER_XA2;
    if (strcmp(str, DRIVER_XA2M_STR) == 0)     return SND_DRIVER_XA2_MODERN;
    if (strcmp(str, DRIVER_WASAPI_STR) == 0)   return SND_DRIVER_WASAPI;
    if (strcmp(str, DRIVER_WAVEOUT_STR) == 0)  return SND_DRIVER_WAVEOUT;
#endif
    if (strcmp(str, DRIVER_SDL2_STR) == 0)     return SND_DRIVER_SDL2;
    if (strcmp(str, DRIVER_NOSOUND_STR) == 0)  return SND_DRIVER_NOSOUND;

    /* Unknown driver string - fall back to the best available driver */
    return SoundDriverFactory::DefaultDriver();
}

const char* Configuration::DriverTypeToString(SoundDriverType type)
{
    switch (type)
    {
    case SND_DRIVER_NOSOUND:    return DRIVER_NOSOUND_STR;
#ifdef _WIN32
    case SND_DRIVER_DS8:        return DRIVER_DS8_STR;
    case SND_DRIVER_XA2:        return DRIVER_XA2_STR;
    case SND_DRIVER_XA2_MODERN: return DRIVER_XA2M_STR;
    case SND_DRIVER_WASAPI:     return DRIVER_WASAPI_STR;
    case SND_DRIVER_WAVEOUT:    return DRIVER_WAVEOUT_STR;
#endif
    case SND_DRIVER_SDL2:       return DRIVER_SDL2_STR;
    default:                    return DRIVER_DEFAULT_STR;
    }
}

/* ---------------------------------------------------------------------------
 * Defaults / loading
 * ------------------------------------------------------------------------ */
void Configuration::LoadDefaults()
{
    configVolume = 100;
    configSyncAudio = true;
    configForceSync = false;
    configBufferLevel = 3;
    configBufferFPS = 45;
    configBackendFPS = 90;
    configResTimer = true;
    configDisallowSleepXA2 = false;
    configDisallowSleepDS8 = false;

    /* Default driver depends on the platform and on driver availability:
       Windows prefers the modern XAudio2 (xaudio2_9.dll, Win10/11), other
       platforms use SDL2.  The factory probes what actually works. */
#ifdef _WIN32
    configDriver = SoundDriverFactory::DriverExists(SND_DRIVER_XA2_MODERN)
        ? SND_DRIVER_XA2_MODERN
        : SoundDriverFactory::DefaultDriver();
#else
    configDriver = SoundDriverFactory::DriverExists(SND_DRIVER_SDL2)
        ? SND_DRIVER_SDL2
        : SoundDriverFactory::DefaultDriver();
#endif
}

void Configuration::LoadSettings()
{
    if (l_ConfigAudio == NULL || ConfigGetParamInt == NULL)
    {
        LoadDefaults();
        return;
    }

    LoadDefaults();

    configSyncAudio = ConfigGetParamBool(l_ConfigAudio, "SYNC_AUDIO") ? true : false;
    configForceSync = ConfigGetParamBool(l_ConfigAudio, "FORCE_SYNC") ? true : false;
    configVolume = (unsigned long)ConfigGetParamInt(l_ConfigAudio, "VOLUME_DEFAULT");
    configBufferLevel = (unsigned long)ConfigGetParamInt(l_ConfigAudio, "BUFFER_LEVEL");
    configBufferFPS = (unsigned long)ConfigGetParamInt(l_ConfigAudio, "BUFFER_FPS");
    configBackendFPS = (unsigned long)ConfigGetParamInt(l_ConfigAudio, "BACKEND_FPS");
    configResTimer = ConfigGetParamBool(l_ConfigAudio, "TIMER_RESOLUTION") ? true : false;
    configDisallowSleepXA2 = ConfigGetParamBool(l_ConfigAudio, "DISALLOW_SLEEP_XA2") ? true : false;
    configDisallowSleepDS8 = ConfigGetParamBool(l_ConfigAudio, "DISALLOW_SLEEP_DS8") ? true : false;

    /* Read driver selection from the config string */
    const char *driverStr = ConfigGetParamString(l_ConfigAudio, "DRIVER");
    if (driverStr != NULL && strlen(driverStr) > 0)
    {
        SoundDriverType requestedDriver = DriverTypeFromString(driverStr);
        /* Verify the requested driver actually exists on this platform */
        if (SoundDriverFactory::DriverExists(requestedDriver))
            configDriver = requestedDriver;
        else
            configDriver = SoundDriverFactory::DefaultDriver();
    }
    else
        configDriver = SoundDriverFactory::DefaultDriver();

    /* Validate settings (protects against a hand-edited config file,
       mirrors the original WIP12.2 clamp fixes) */
    if (configVolume > 100) configVolume = 100;
    if (configBufferLevel < 1) configBufferLevel = 1;
    if (configBufferLevel > 9) configBufferLevel = 9;
    if (configBufferFPS < 15) configBufferFPS = 15;
    if (configBufferFPS > 120) configBufferFPS = 120;
    if (configBackendFPS < 15) configBackendFPS = 15;
    if (configBackendFPS > 120) configBackendFPS = 120;
}
