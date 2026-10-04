/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* Sound Driver Factory                                                      *
*                                                                           *
* License: GNU/GPLv2                                                        *
*                                                                           *
****************************************************************************/
#pragma once
#include "common.h"
#include "SoundDriverInterface.h"

/* Forward declare probe functions using X-Macro */
#define SOUND_DRIVER(name) extern void name##Probe();
#include "XDrivers.h"
#undef SOUND_DRIVER

class SoundDriverFactory
{
private:
    typedef SoundDriverInterface* (*SoundDriverCreationFunction)();
    struct FactoryDriversStruct
    {
        SoundDriverType DriverType;
        SoundDriverCreationFunction CreateFunction;
        int Priority;
        char Description[100];
    };

    SoundDriverFactory() {};
    static int FactoryNextSlot;
    static const int MAX_FACTORY_DRIVERS = 20;
    static FactoryDriversStruct FactoryDrivers[MAX_FACTORY_DRIVERS];

    /* Friend declarations for probe functions */
#define SOUND_DRIVER(name) friend void name##Probe();
#include "XDrivers.h"
#undef SOUND_DRIVER

public:
    ~SoundDriverFactory() {};

    static void Initialize();
    static SoundDriverInterface* CreateSoundDriver(SoundDriverType DriverID);
    static bool RegisterSoundDriver(SoundDriverType DriverType, SoundDriverCreationFunction CreateFunction, const char *Description, int Priority);
    static SoundDriverType DefaultDriver();
    static int EnumDrivers(SoundDriverType *drivers, int max_entries);
    static const char* GetDriverDescription(SoundDriverType driver);
    static bool DriverExists(SoundDriverType driver);
};
