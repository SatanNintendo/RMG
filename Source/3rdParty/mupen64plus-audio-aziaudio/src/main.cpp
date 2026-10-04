/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin for Mupen64Plus                                 *
* http://www.apollo64.com/                                                  *
* Copyright (C) 2000-2026 Azimer. All rights reserved.                      *
*                                                                           *
* Mupen64Plus plugin interface implementation.  Ported from the zilmar      *
* audio specification (Project64) to the Mupen64Plus audio plugin           *
* specification.                                                            *
*                                                                           *
* Core architecture notes:                                                  *
*  - The Mupen64Plus core owns the complete AI state machine (DMA FIFO,    *
*    timings, AI interrupt).  This plugin is an audio SINK: it receives    *
*    "already played" samples in AiLenChanged() and streams them to a      *
*    sound backend.  It never manipulates AI_STATUS / MI_INTR or calls     *
*    CheckInterrupts (the core's dummy in the compat layer makes the old   *
*    zilmar-style register poking useless at best and harmful at worst).   *
*  - AiLenChanged() is invoked through the core's plugin-compat backend    *
*    with AI_DRAM_ADDR_REG / AI_LEN_REG temporarily programmed - the data  *
*    must be copied out of RDRAM IMMEDIATELY (registers are restored and   *
*    the game may reuse the memory as soon as we return).                  *
*  - The plugin also carries the full AziAudio HLE audio engine.  It is    *
*    used when the RSP plugin (mupen64plus-rsp-hle) is configured with     *
*    "AudioListToAudioPlugin = True": the RSP then forwards every audio    *
*    task list to ProcessAList() instead of handling it itself.            *
*                                                                           *
* License:                                                                  *
* GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html                        *
*                                                                           *
****************************************************************************/

#include "common.h"
#include "AudioSpec.h"

#include "SoundDriverInterface.h"
#include "SoundDriverFactory.h"
#include "Configuration.h"
#include "audiohle.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "PluginMutex.h"

#ifdef _WIN32
#include <windows.h>
#define M64P_SYM(lib, name) GetProcAddress((HMODULE)(lib), (name))
#include "ConfigDialog.h"
#else
#include <dlfcn.h>
#define M64P_SYM(lib, name) dlsym((void*)(lib), (name))
#endif

/* ========================================================================
 * Mupen64Plus Core function pointers (resolved in PluginStartup)
 * ======================================================================== */
ptr_ConfigOpenSection      ConfigOpenSection = NULL;
ptr_ConfigDeleteSection    ConfigDeleteSection = NULL;
ptr_ConfigSetParameter     ConfigSetParameter = NULL;
ptr_ConfigGetParameter     ConfigGetParameter = NULL;
ptr_ConfigSetDefaultInt    ConfigSetDefaultInt = NULL;
ptr_ConfigSetDefaultFloat  ConfigSetDefaultFloat = NULL;
ptr_ConfigSetDefaultBool   ConfigSetDefaultBool = NULL;
ptr_ConfigSetDefaultString ConfigSetDefaultString = NULL;
ptr_ConfigGetParamInt      ConfigGetParamInt = NULL;
ptr_ConfigGetParamFloat    ConfigGetParamFloat = NULL;
ptr_ConfigGetParamBool     ConfigGetParamBool = NULL;
ptr_ConfigGetParamString   ConfigGetParamString = NULL;
ptr_ConfigSaveSection      ConfigSaveSection = NULL;

/* Plugin state */
static int l_PluginInit = 0;
m64p_handle l_ConfigAudio = NULL;

/* Debug callback into the core */
static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;

/* Global audio state */
static SoundDriverInterface *snd = NULL;
static PluginMutex l_DriverMutex;         /* guards create/destroy vs. use  */

AUDIO_INFO AudioInfo;                   /* filled by InitiateAudio()      */
/* (declared extern "C" in AudioSpec.h / m64p_plugin.h) */

/* HLE memory globals - DEFINED in HLEMain.cpp, filled by InitiateAudio()
   (declared extern in audiohle.h) */

/* Last DAC rate seen (deduplicates AiDacrateChanged calls) */
static u32 Dacrate = 0;

/* Volume control (Mupen64Plus Volume API) */
static int VolPercent = 80;
static int VolDelta = 5;
static int VolIsMuted = 0;

/* ------------------------------------------------------------------------
 * Windows multimedia timer resolution (from the original AziAudio WIP12:
 * makes the 1 ms A/V-sync sleeps accurate instead of ~15.6 ms)
 * --------------------------------------------------------------------- */
#ifdef _WIN32
typedef LONG(NTAPI* pSetTimerResolution)(ULONG RequestedResolution, BOOLEAN Set, PULONG ActualResolution);
typedef LONG(NTAPI* pQueryTimerResolution)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);
static pSetTimerResolution   l_pNtSetTimerResolution = NULL;
static ULONG                 l_TimerResApplied = 0;    /* 0 = not applied */

static void SetTimerResolution(void)
{
    pQueryTimerResolution queryFunction;
    ULONG minResolution, maxResolution, actualResolution;
    const HINSTANCE hLibrary = LoadLibraryA("NTDLL.dll");

    if (hLibrary == NULL)
        return;

    queryFunction = (pQueryTimerResolution)GetProcAddress(hLibrary, "NtQueryTimerResolution");
    if (queryFunction == NULL)
    {
        /* NTDLL is always mapped into the process; releasing our local
           reference cannot unload it (original WIP12.2 fix). */
        FreeLibrary(hLibrary);
        return;
    }

    queryFunction(&minResolution, &maxResolution, &actualResolution);
    DebugMessage(M64MSG_VERBOSE, "Win32 timer resolution: min=%u max=%u actual=%u",
                 minResolution, maxResolution, actualResolution);

    l_pNtSetTimerResolution = (pSetTimerResolution)GetProcAddress(hLibrary, "NtSetTimerResolution");
    if (l_pNtSetTimerResolution != NULL)
    {
        /* maxResolution is the FINEST resolution (in 100 ns units).
           Requesting it gives the most precise Sleep() granularity. */
        if (l_pNtSetTimerResolution(maxResolution, TRUE, &actualResolution) == 0)
            l_TimerResApplied = maxResolution;
    }

    FreeLibrary(hLibrary);
}

static void RestoreTimerResolution(void)
{
    if (l_TimerResApplied != 0 && l_pNtSetTimerResolution != NULL)
    {
        ULONG actual = 0;
        l_pNtSetTimerResolution(l_TimerResApplied, FALSE, &actual);
        l_TimerResApplied = 0;
    }
    l_pNtSetTimerResolution = NULL;
}
#endif /* _WIN32 */

/* ------------------------------------------------------------------------
 * Debug message routing (to the core's log)
 * --------------------------------------------------------------------- */
void DebugMessage(int level, const char *message, ...)
{
    char msgbuf[1024];
    va_list args;

    if (l_DebugCallback == NULL)
        return;

    va_start(args, message);
    vsnprintf(msgbuf, sizeof(msgbuf), message, args);
    (*l_DebugCallback)(l_DebugCallContext, level, msgbuf);
    va_end(args);
}

/* ------------------------------------------------------------------------
 * safe_strcpy (used by several translation units)
 * --------------------------------------------------------------------- */
int safe_strcpy(char* dst, size_t limit, const char* src)
{
    if (dst == NULL || src == NULL)
        return 22; /* EINVAL */

    size_t bytes = strlen(src) + 1;
    int failure = 0;

    if (bytes > limit)
    {
        bytes = limit;
        failure = 34; /* ERANGE */
    }

    memcpy(dst, src, bytes);
    dst[limit - 1] = '\0';
    return failure;
}

/* ------------------------------------------------------------------------
 * Volume helpers
 * --------------------------------------------------------------------- */
static void VolumeCommit(void)
{
    int levelToCommit = VolIsMuted ? 0 : VolPercent;

    PluginLockGuard lock(l_DriverMutex);
    if (snd != NULL)
        snd->SetVolume((u32)levelToCommit);
}

/* ------------------------------------------------------------------------
 * AziVolumeApply - set the live volume state (0..100 + mute) and push it
 * to the running driver immediately.  Called by the settings dialog's
 * OK/Apply buttons so the volume slider takes effect without a restart.
 * With no game running it just updates the state (the next RomOpen picks
 * it up from VOLUME_DEFAULT anyway).
 *
 * v1.0.12 SESSION 8: previously the dialog only wrote VOLUME_DEFAULT to
 * the config; the live value (VolPercent) was read once at PluginStartup,
 * so the slider never reached the driver and even a game restart kept the
 * startup-time volume.
 * --------------------------------------------------------------------- */
void AziVolumeApply(int level, int muted)
{
    if (!l_PluginInit)
        return;

    if (level < 0)
        level = 0;
    if (level > 100)
        level = 100;

    VolPercent = level;
    VolIsMuted = (muted != 0) ? 1 : 0;

    VolumeCommit();

    DebugMessage(M64MSG_INFO, "Volume: %d%% applied%s",
                 VolIsMuted ? 0 : VolPercent,
                 VolIsMuted ? " (muted)" : "");
}

/* ========================================================================
 * Mupen64Plus Common Plugin Functions
 * ======================================================================== */

#ifdef _WIN32
/* ------------------------------------------------------------------------
 * SESSION 9 (v1.0.17): pin OUR OWN DLL into the process for its whole
 * lifetime (GetModuleHandleExW with GET_MODULE_HANDLE_EX_FLAG_PIN -
 * FreeLibrary from the frontend then never unmaps us).
 *
 * WHY: the XAudio 2.9 backend (and, on the Windows 7 / VxKex machine,
 * the xaudio2_9 shim's ~40-thread worker pool) dispatches audio
 * callbacks asynchronously.  RomClosed / RomOpen / PluginShutdown delete
 * the sound driver object right after AI_Shutdown(), and a frontend that
 * SWITCHES audio plugins in its settings UI calls FreeLibrary on us while
 * a late callback may still be executing our code - executing unmapped
 * code is an immediate silent process exit, exactly the "emulator
 * sometimes closes without any error while working with the plugin and
 * its settings" report.  Pinned, the ~320 KB module simply stays mapped
 * until process exit; a later LoadLibrary of the same file returns the
 * same base address (DLL_PROCESS_ATTACH is not re-run; PluginStartup
 * re-initialises every static it needs, so repeat sessions are correct).
 *
 * Called from PluginStartup - the first exported entry point any frontend
 * calls, guaranteed before any sound driver (and therefore any engine
 * callback) can exist.  Idempotent by nature.
 * --------------------------------------------------------------------- */
static void PinSelfInProcess(void)
{
    HMODULE hSelf = NULL;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (LPCWSTR)(uintptr_t)&PluginStartup,
                           &hSelf) != FALSE)
    {
        DebugMessage(M64MSG_INFO,
            "AziAudio-Plus: plugin DLL pinned for the process lifetime (late audio-callback safety)");
    }
    else
    {
        DebugMessage(M64MSG_WARNING,
            "AziAudio-Plus: could not pin the plugin DLL (%lu) - switching audio plugins while a game runs is less safe",
            GetLastError());
    }
}
#endif /* _WIN32 */

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    ptr_CoreGetAPIVersions CoreAPIVersionFunc;
    int ConfigAPIVersion, DebugAPIVersion, VidextAPIVersion;
    float fConfigParamsVersion = 0.0f;

    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    /* First thing is to set the callback function for debug info */
    l_DebugCallback = DebugCallback;
    l_DebugCallContext = Context;

#ifdef _WIN32
    /* SESSION 9: pin ourselves before anything else can create async
     * callbacks that point into this module. */
    PinSelfInProcess();
#endif

    /* Attach and call the CoreGetAPIVersions function, check Config API
       version for compatibility */
    CoreAPIVersionFunc = (ptr_CoreGetAPIVersions) M64P_SYM(CoreLibHandle, "CoreGetAPIVersions");
    if (CoreAPIVersionFunc == NULL)
    {
        DebugMessage(M64MSG_ERROR, "Core emulator broken; no CoreAPIVersionFunc() function found.");
        return M64ERR_INCOMPATIBLE;
    }

    (*CoreAPIVersionFunc)(&ConfigAPIVersion, &DebugAPIVersion, &VidextAPIVersion, NULL);
    if ((ConfigAPIVersion & 0xffff0000) != (CONFIG_API_VERSION & 0xffff0000))
    {
        DebugMessage(M64MSG_ERROR, "Emulator core Config API (v%i.%i.%i) incompatible with plugin (v%i.%i.%i)",
                (ConfigAPIVersion >> 16) & 0xffff, (ConfigAPIVersion >> 8) & 0xff, ConfigAPIVersion & 0xff,
                (CONFIG_API_VERSION >> 16) & 0xffff, (CONFIG_API_VERSION >> 8) & 0xff, CONFIG_API_VERSION & 0xff);
        return M64ERR_INCOMPATIBLE;
    }

    /* Get the core config function pointers from the library handle */
    ConfigOpenSection = (ptr_ConfigOpenSection) M64P_SYM(CoreLibHandle, "ConfigOpenSection");
    ConfigDeleteSection = (ptr_ConfigDeleteSection) M64P_SYM(CoreLibHandle, "ConfigDeleteSection");
    ConfigSetParameter = (ptr_ConfigSetParameter) M64P_SYM(CoreLibHandle, "ConfigSetParameter");
    ConfigGetParameter = (ptr_ConfigGetParameter) M64P_SYM(CoreLibHandle, "ConfigGetParameter");
    ConfigSetDefaultInt = (ptr_ConfigSetDefaultInt) M64P_SYM(CoreLibHandle, "ConfigSetDefaultInt");
    ConfigSetDefaultFloat = (ptr_ConfigSetDefaultFloat) M64P_SYM(CoreLibHandle, "ConfigSetDefaultFloat");
    ConfigSetDefaultBool = (ptr_ConfigSetDefaultBool) M64P_SYM(CoreLibHandle, "ConfigSetDefaultBool");
    ConfigSetDefaultString = (ptr_ConfigSetDefaultString) M64P_SYM(CoreLibHandle, "ConfigSetDefaultString");
    ConfigGetParamInt = (ptr_ConfigGetParamInt) M64P_SYM(CoreLibHandle, "ConfigGetParamInt");
    ConfigGetParamFloat = (ptr_ConfigGetParamFloat) M64P_SYM(CoreLibHandle, "ConfigGetParamFloat");
    ConfigGetParamBool = (ptr_ConfigGetParamBool) M64P_SYM(CoreLibHandle, "ConfigGetParamBool");
    ConfigGetParamString = (ptr_ConfigGetParamString) M64P_SYM(CoreLibHandle, "ConfigGetParamString");
    ConfigSaveSection = (ptr_ConfigSaveSection) M64P_SYM(CoreLibHandle, "ConfigSaveSection");

    if (!ConfigOpenSection || !ConfigDeleteSection || !ConfigSetParameter || !ConfigGetParameter ||
        !ConfigSetDefaultInt || !ConfigSetDefaultFloat || !ConfigSetDefaultBool || !ConfigSetDefaultString ||
        !ConfigGetParamInt   || !ConfigGetParamFloat   || !ConfigGetParamBool   || !ConfigGetParamString)
        return M64ERR_INCOMPATIBLE;

    /* Get a configuration section handle */
    if (ConfigOpenSection("Audio-AziAudio", &l_ConfigAudio) != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "Couldn't open config section 'Audio-AziAudio'");
        return M64ERR_INPUT_NOT_FOUND;
    }

    /* Check the section version number */
    if (ConfigGetParameter(l_ConfigAudio, "Version", M64TYPE_FLOAT, &fConfigParamsVersion, sizeof(float)) != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_WARNING, "No version number in 'Audio-AziAudio' config section. Setting defaults.");
        ConfigDeleteSection("Audio-AziAudio");
        ConfigOpenSection("Audio-AziAudio", &l_ConfigAudio);
    }
    else if (((int) fConfigParamsVersion) != ((int) CONFIG_PARAM_VERSION))
    {
        DebugMessage(M64MSG_WARNING, "Incompatible version %.2f in 'Audio-AziAudio' config section. Setting defaults.", fConfigParamsVersion);
        ConfigDeleteSection("Audio-AziAudio");
        ConfigOpenSection("Audio-AziAudio", &l_ConfigAudio);
    }

    /* Set the default values for this plugin */
    ConfigSetDefaultFloat(l_ConfigAudio, "Version",           CONFIG_PARAM_VERSION, "AziAudio-Plus Mupen64Plus config parameter version number");
    ConfigSetDefaultString(l_ConfigAudio, "DRIVER",           DRIVER_DEFAULT_STR,   "Sound driver: default, nosound, sdl2"
#ifdef _WIN32
    ", ds8, xa2, xa2legacy, xa2modern, wasapi, waveout"
#endif
    );
    /* VOLUME 1.0.13: 100 = full gain, matching the original AziAudio-Plus
       default (its inverted 0=full/100=silent scale shipped at 0).  The
       previous default of 80 made every backend start 1.9 dB (linear
       paths) to 20 dB (DirectSound's dB scale) quieter than the plugin
       this project was ported from. */
    ConfigSetDefaultInt(l_ConfigAudio,   "VOLUME_DEFAULT",    100,                  "Default volume when a game is started (0-100)");
    ConfigSetDefaultInt(l_ConfigAudio,   "VOLUME_ADJUST",     5,                   "Volume change step for VolumeUp/VolumeDown (percent)");
    ConfigSetDefaultBool(l_ConfigAudio,  "SYNC_AUDIO",        1,                   "Synchronize audio with the emulator (A/V sync)");
    ConfigSetDefaultBool(l_ConfigAudio,  "FORCE_SYNC",        0,                   "Extra-strict synchronization for the legacy DirectSound path");
    ConfigSetDefaultInt(l_ConfigAudio,   "BUFFER_LEVEL",      3,                   "A/V sync buffer target, in frames of BUFFER_FPS (1-9)");
    ConfigSetDefaultInt(l_ConfigAudio,   "BUFFER_FPS",        45,                  "Frame rate used for the buffer level computation (15-120)");
    ConfigSetDefaultInt(l_ConfigAudio,   "BACKEND_FPS",       90,                  "Backend buffer submission granularity (15-120)");
    ConfigSetDefaultInt(l_ConfigAudio,   "DEFAULT_FREQUENCY", 44100,               "Frequency used until the game programs the DAC rate (matches the core default)");
    ConfigSetDefaultBool(l_ConfigAudio,  "TIMER_RESOLUTION",  1,                   "Windows: request the finest system timer resolution for precise sync sleeps");
    ConfigSetDefaultBool(l_ConfigAudio,  "DISALLOW_SLEEP_XA2", 0,                  "XAudio2: disallow yielding in the audio thread (lower latency, higher CPU)");
    ConfigSetDefaultBool(l_ConfigAudio,  "DISALLOW_SLEEP_DS8", 0,                  "DirectSound: disallow yielding in the audio thread (lower latency, higher CPU)");

    /* Probe and register available sound drivers FIRST - the config
       loader resolves "default" through the factory's priority list. */
    SoundDriverFactory::Initialize();

    /* Load the settings (also validates and clamps) */
    Configuration::LoadSettings();

    /* Log available drivers */
    {
        SoundDriverType drivers[20];
        int numDrivers = SoundDriverFactory::EnumDrivers(drivers, 20);
        DebugMessage(M64MSG_INFO, "AziAudio-Plus: %d sound driver(s) available:", numDrivers);
        for (int i = 0; i < numDrivers; i++)
            DebugMessage(M64MSG_INFO, "  [%s] %s",
                Configuration::DriverTypeToString(drivers[i]),
                SoundDriverFactory::GetDriverDescription(drivers[i]));
    }

    /* Read volume settings */
    VolPercent = ConfigGetParamInt(l_ConfigAudio, "VOLUME_DEFAULT");
    VolDelta = ConfigGetParamInt(l_ConfigAudio, "VOLUME_ADJUST");
    if (VolPercent < 0) VolPercent = 0;
    if (VolPercent > 100) VolPercent = 100;
    if (VolDelta <= 0) VolDelta = 5;

#ifdef _WIN32
    if (Configuration::getResTimer() == true)
        SetTimerResolution();
#endif

    l_PluginInit = 1;
    DebugMessage(M64MSG_INFO, "%s initialized.", PLUGIN_VERSION);
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

    /* Destroy a still-running sound driver (e.g. the frontend did not
       close the ROM).  The pointer is nulled FIRST so a concurrent
       callback can never observe a deleted object. */
    {
        PluginLockGuard lock(l_DriverMutex);
        if (snd != NULL)
        {
            SoundDriverInterface *tmp = snd;
            snd = NULL;
            tmp->AI_Shutdown();
            delete tmp;
        }
    }

#ifdef _WIN32
    RestoreTimerResolution();
#endif

    /* Reset state */
    l_DebugCallback = NULL;
    l_DebugCallContext = NULL;
    l_ConfigAudio = NULL;
    DMEM = NULL;
    IMEM = NULL;
    DRAM = NULL;
    Dacrate = 0;
    l_PluginInit = 0;

    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion,
                                        int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_AUDIO;

    if (PluginVersion != NULL)
        *PluginVersion = AZI_AUDIO_PLUGIN_VERSION;

    if (APIVersion != NULL)
        *APIVersion = AUDIO_PLUGIN_API_VERSION;

    if (PluginNamePtr != NULL)
        *PluginNamePtr = "AziAudio-Plus HLE Audio (Mupen64Plus)";

    if (Capabilities != NULL)
        *Capabilities = 0;

    return M64ERR_SUCCESS;
}

/* ------------------------------------------------------------------------
 * PluginConfig - configuration dialog (optional function, called by
 * frontends such as RMG when the user opens the plugin settings)
 * --------------------------------------------------------------------- */
EXPORT m64p_error CALL PluginConfig(void *parent)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

#ifdef _WIN32
    /* 'parent' may be a Win32 HWND (console frontends), a Qt QWidget*
       (RMG) or NULL.  Only use it as an owner when it really is a
       window; otherwise fall back to a parentless dialog. */
    HWND hParent = NULL;
    if (parent != NULL && IsWindow((HWND)parent))
        hParent = (HWND)parent;
    ShowConfigDialog(g_hDllInst, hParent);
#else
    DebugMessage(M64MSG_INFO, "PluginConfig: settings are in the Mupen64Plus config file, section [Audio-AziAudio]");
#endif

    return M64ERR_SUCCESS;
}

/* ========================================================================
 * Mupen64Plus Audio Plugin Functions
 * ======================================================================== */

/* ------------------------------------------------------------------------
 * InitiateAudio - the core hands over the memory map
 * --------------------------------------------------------------------- */
EXPORT int CALL InitiateAudio(AUDIO_INFO Audio_Info)
{
    if (!l_PluginInit)
        return 0;

    AudioInfo = Audio_Info;

    DMEM = Audio_Info.DMEM;
    IMEM = Audio_Info.IMEM;
    DRAM = Audio_Info.RDRAM;

    if (DRAM == NULL || DMEM == NULL || IMEM == NULL)
    {
        DebugMessage(M64MSG_ERROR, "InitiateAudio: core passed NULL memory pointers");
        return 0;
    }

    /* Pick up settings changes made between runs */
    Configuration::LoadSettings();
    VolPercent = ConfigGetParamInt(l_ConfigAudio, "VOLUME_DEFAULT");
    if (VolPercent < 0) VolPercent = 0;
    if (VolPercent > 100) VolPercent = 100;

    DebugMessage(M64MSG_VERBOSE, "InitiateAudio: memory map stored (RDRAM=%p DMEM=%p IMEM=%p)",
                 (void*)DRAM, (void*)DMEM, (void*)IMEM);
    return 1;
}

/* ------------------------------------------------------------------------
 * RomOpen - create and start the sound backend
 * --------------------------------------------------------------------- */
EXPORT int CALL RomOpen(void)
{
    if (!l_PluginInit)
        return 0;

    Configuration::RomRunning = true;
    Configuration::LoadSettings();

    /* v1.0.12 SESSION 8: pick up the configured default volume at every
     * game start (the parameter is documented as "Default volume when a
     * game is started").  VolPercent used to be read only once at
     * PluginStartup, so slider changes made in the settings dialog never
     * reached the driver without a full frontend restart. */
    {
        int cfgVol = (int)Configuration::getVolume();
        if (cfgVol < 0) cfgVol = 0;
        if (cfgVol > 100) cfgVol = 100;
        VolPercent = cfgVol;
        VolIsMuted = 0;
        DebugMessage(M64MSG_INFO, "RomOpen: default volume %d%% (VOLUME_DEFAULT)",
                     VolPercent);
    }

    {
        PluginLockGuard lock(l_DriverMutex);

        /* Fresh driver on every ROM start (the native Mupen64Plus
           lifecycle: RomOpen creates the backend, RomClosed releases
           it, exactly like mupen64plus-audio-sdl). */
        if (snd != NULL)
        {
            snd->AI_Shutdown();
            delete snd;
            snd = NULL;
        }

        SoundDriverType selectedDriver = Configuration::getDriver();
        DebugMessage(M64MSG_INFO, "RomOpen: creating sound driver '%s'",
                     Configuration::DriverTypeToString(selectedDriver));

        snd = SoundDriverFactory::CreateSoundDriver(selectedDriver);
        if (snd == NULL)
        {
            DebugMessage(M64MSG_ERROR, "RomOpen: failed to create sound driver, falling back to NoSound");
            snd = SoundDriverFactory::CreateSoundDriver(SND_DRIVER_NOSOUND);
            if (snd == NULL)
                return 0;
        }

        snd->AI_Startup();

        /* Apply the configured default frequency until the game
           programs the DAC rate (matches the core's lazy default). */
        int defaultFreq = ConfigGetParamInt(l_ConfigAudio, "DEFAULT_FREQUENCY");
        if (defaultFreq < 8000) defaultFreq = 8000;
        if (defaultFreq > 96000) defaultFreq = 96000;
        snd->AI_SetFrequency((u32)defaultFreq);

        Dacrate = 0;
    }

    snd->SetVolume((u32)(VolIsMuted ? 0 : VolPercent));

    return 1;
}

/* ------------------------------------------------------------------------
 * RomClosed - stop and release the sound backend
 * --------------------------------------------------------------------- */
EXPORT void CALL RomClosed(void)
{
    if (!l_PluginInit)
        return;

    Configuration::RomRunning = false;

    PluginLockGuard lock(l_DriverMutex);
    if (snd != NULL)
    {
        SoundDriverInterface *tmp = snd;
        snd = NULL;             /* null FIRST so concurrent AI calls see it */
        tmp->AI_Shutdown();
        delete tmp;
    }
    Dacrate = 0;                /* force re-detection on the next ROM     */
}

/* ------------------------------------------------------------------------
 * AiDacrateChanged - the game programmed a new DAC rate
 *
 * The core calls this (through its plugin-compat backend) whenever the
 * audio format changes; it temporarily programs AI_DACRATE_REG to a
 * value matching the requested frequency for the given system type.
 * --------------------------------------------------------------------- */
static unsigned int vi_clock_from_system_type(int system_type)
{
    switch (system_type)
    {
    default:
        DebugMessage(M64MSG_WARNING, "Invalid system_type %d. Assuming NTSC", system_type);
        /* fallback */
    case SYSTEM_NTSC: return 48681812;
    case SYSTEM_PAL:  return 49656530;
    case SYSTEM_MPAL: return 48628316;
    }
}

static unsigned int dacrate2freq(unsigned int vi_clock, uint32_t dacrate)
{
    return vi_clock / (dacrate + 1);
}

EXPORT void CALL AiDacrateChanged(int SystemType)
{
    if (!l_PluginInit || AudioInfo.AI_DACRATE_REG == NULL)
        return;

    u32 dacrate = *AudioInfo.AI_DACRATE_REG & 0x00003FFFu;
    if (dacrate == 0)
        return;

    /* Deduplicate: the core's compat wrapper rewrites AI_DACRATE_REG to
       the value corresponding to the requested frequency, so equal
       frequencies produce equal dacrate values here. */
    if (Dacrate == dacrate)
        return;
    Dacrate = dacrate;

    unsigned int video_clock = vi_clock_from_system_type(SystemType);
    u32 Frequency = dacrate2freq(video_clock, dacrate);

    /* Standardise to the common rates (as the original AziAudio does) -
       the sound cards handle these best and it avoids long-term drift
       between the emulated and real clocks. */
    if ((Frequency > 7000) && (Frequency < 9000))
        Frequency = 8000;
    else if ((Frequency > 10000) && (Frequency < 12000))
        Frequency = 11025;
    else if ((Frequency > 18000) && (Frequency < 20000))
        Frequency = 19000;
    else if ((Frequency > 21000) && (Frequency < 23000))
        Frequency = 22050;
    else if ((Frequency > 31000) && (Frequency < 33000))
        Frequency = 32000;
    else if ((Frequency > 43000) && (Frequency < 45000))
        Frequency = 44100;
    else if ((Frequency > 47000) && (Frequency < 49000))
        Frequency = 48000;
    else
        DebugMessage(M64MSG_VERBOSE, "AiDacrateChanged: unstandardised frequency %u Hz", Frequency);

    DebugMessage(M64MSG_INFO, "AiDacrateChanged: frequency = %u Hz (system type %d)", Frequency, SystemType);

    PluginLockGuard lock(l_DriverMutex);
    if (snd != NULL)
        snd->AI_SetFrequency(Frequency);
}

/* ------------------------------------------------------------------------
 * AiLenChanged - the emulated AI DMA played 'length' bytes
 *
 * IMPORTANT: unlike the zilmar spec there is NO delayed-carry handling
 * here - the core already applied it when it computed the buffer
 * address.  The AI registers are valid only for the duration of this
 * call (the core restores them right after), and the sample data must
 * be copied out of RDRAM immediately.
 * --------------------------------------------------------------------- */
EXPORT void CALL AiLenChanged(void)
{
    if (!l_PluginInit || AudioInfo.AI_DRAM_ADDR_REG == NULL || AudioInfo.AI_LEN_REG == NULL)
        return;

    u32 address = (*AudioInfo.AI_DRAM_ADDR_REG & 0x00FFFFF8u);
    u32 length = (*AudioInfo.AI_LEN_REG & 0x3FFF8u);

    if (length == 0)
        return;

    PluginLockGuard lock(l_DriverMutex);
    if (snd == NULL || AudioInfo.RDRAM == NULL)
        return;

    snd->AI_LenChanged(AudioInfo.RDRAM + address, length);
}

/* ------------------------------------------------------------------------
 * ProcessAList - AziAudio's own HLE audio processing
 *
 * Called by the RSP plugin (mupen64plus-rsp-hle) for audio tasks when
 * its "AudioListToAudioPlugin" setting is enabled.  The task list and
 * buffers are read directly from DMEM / RDRAM via the pointers handed
 * over in InitiateAudio().
 * --------------------------------------------------------------------- */
EXPORT void CALL ProcessAList(void)
{
    if (!l_PluginInit || DMEM == NULL || DRAM == NULL)
        return;

    HLEStart();
}

/* ------------------------------------------------------------------------
 * SetSpeedFactor - emulator fast-forward / slow-motion
 * --------------------------------------------------------------------- */
EXPORT void CALL SetSpeedFactor(int percentage)
{
    if (!l_PluginInit)
        return;

    if (percentage < 10) percentage = 10;
    if (percentage > 1000) percentage = 1000;

    DebugMessage(M64MSG_VERBOSE, "SetSpeedFactor: %d%%", percentage);

    PluginLockGuard lock(l_DriverMutex);
    if (snd != NULL)
        snd->AI_SetSpeedFactor((u32)percentage);
}

/* ========================================================================
 * Volume Control (Mupen64Plus API)
 * ======================================================================== */

EXPORT void CALL VolumeMute(void)
{
    if (!l_PluginInit)
        return;

    VolIsMuted = !VolIsMuted;
    VolumeCommit();
}

EXPORT void CALL VolumeUp(void)
{
    if (!l_PluginInit)
        return;

    VolPercent += VolDelta;
    if (VolPercent > 100)
        VolPercent = 100;
    VolIsMuted = 0;
    VolumeCommit();
}

EXPORT void CALL VolumeDown(void)
{
    if (!l_PluginInit)
        return;

    VolPercent -= VolDelta;
    if (VolPercent < 0)
        VolPercent = 0;
    VolIsMuted = 0;
    VolumeCommit();
}

EXPORT int CALL VolumeGetLevel(void)
{
    return VolIsMuted ? 0 : VolPercent;
}

EXPORT void CALL VolumeSetLevel(int level)
{
    if (!l_PluginInit)
        return;

    VolIsMuted = 0;
    VolPercent = level;
    if (VolPercent < 0)
        VolPercent = 0;
    else if (VolPercent > 100)
        VolPercent = 100;
    VolumeCommit();
}

EXPORT const char * CALL VolumeGetString(void)
{
    static char VolumeString[32];

    if (VolIsMuted)
        strcpy(VolumeString, "Mute");
    else
        sprintf(VolumeString, "%i%%", VolPercent);

    return VolumeString;
}

/* ========================================================================
 * DLL entry point (Windows) - keeps the instance handle for the
 * configuration dialog
 * ======================================================================== */
#ifdef _WIN32
HINSTANCE g_hDllInst = NULL;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        g_hDllInst = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}
#endif
