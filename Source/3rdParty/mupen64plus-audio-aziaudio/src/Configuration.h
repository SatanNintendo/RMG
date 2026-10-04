/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* Configuration - settings stored through the Mupen64Plus Core Config API   *
* (config section "Audio-AziAudio")                                         *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/
#ifndef _CONFIGURATION_H_INCLUDED__
#define _CONFIGURATION_H_INCLUDED__

#include "common.h"

class Configuration
{
public:
    /* Volume, 0..100, 100 = full (Mupen64Plus Volume API semantics) */
    static unsigned long getVolume() { return configVolume; }
    static void setVolume(unsigned long value)
    {
        configVolume = value > 100 ? 100 : value;
    }

    /* Selected backend sound driver */
    static SoundDriverType getDriver() { return configDriver; }
    static void setDriver(SoundDriverType value) { configDriver = value; }

    /* A/V synchronisation (throttle the emulator when audio runs ahead) */
    static bool getSyncAudio() { return configSyncAudio; }
    static void setSyncAudio(bool value) { configSyncAudio = value; }

    /* Extra-strict sync for the legacy DirectSound path */
    static bool getForceSync() { return configForceSync; }
    static void setForceSync(bool value) { configForceSync = value; }

    /* Buffer target: BUFFER_LEVEL frames of BUFFER_FPS */
    static unsigned long getBufferLevel() { return configBufferLevel; }
    static void setBufferLevel(unsigned long value)
    {
        if (value < 1) value = 1;
        if (value > 9) value = 9;
        configBufferLevel = value;
    }

    static unsigned long getBufferFPS() { return configBufferFPS; }
    static void setBufferFPS(unsigned long value)
    {
        if (value < 15) value = 15;
        if (value > 120) value = 120;
        configBufferFPS = value;
    }

    /* Granularity of backend buffer submissions (bytes per segment) */
    static unsigned long getBackendFPS() { return configBackendFPS; }
    static void setBackendFPS(unsigned long value)
    {
        if (value < 15) value = 15;
        if (value > 120) value = 120;
        configBackendFPS = value;
    }

    /* Windows: request a high resolution multimedia timer (makes the
     * 1 ms sync sleeps accurate instead of ~15 ms) */
    static bool getResTimer() { return configResTimer; }
    static void setResTimer(bool value) { configResTimer = value; }

    /* Backend thread yielding switches (audio latency tuning) */
    static bool getDisallowSleepXA2() { return configDisallowSleepXA2; }
    static void setDisallowSleepXA2(bool value) { configDisallowSleepXA2 = value; }

    static bool getDisallowSleepDS8() { return configDisallowSleepDS8; }
    static void setDisallowSleepDS8(bool value) { configDisallowSleepDS8 = value; }

    /* Whether a ROM is currently running */
    static bool RomRunning;

    static void LoadDefaults();
    static void LoadSettings();

    static SoundDriverType DriverTypeFromString(const char *str);
    static const char *DriverTypeToString(SoundDriverType type);

private:
    static unsigned long configVolume;
    static SoundDriverType configDriver;
    static bool configSyncAudio;
    static bool configForceSync;
    static unsigned long configBufferLevel;
    static unsigned long configBufferFPS;
    static unsigned long configBackendFPS;
    static bool configResTimer;
    static bool configDisallowSleepXA2;
    static bool configDisallowSleepDS8;
};

#endif /* _CONFIGURATION_H_INCLUDED__ */
