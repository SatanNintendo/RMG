/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* Sound Driver Factory implementation                                       *
*                                                                           *
* License: GNU/GPLv2                                                        *
*                                                                           *
****************************************************************************/
#include "SoundDriverFactory.h"
#include "NoSoundDriver.h"
#include "PluginMutex.h"

int SoundDriverFactory::FactoryNextSlot = 0;
SoundDriverFactory::FactoryDriversStruct SoundDriverFactory::FactoryDrivers[MAX_FACTORY_DRIVERS];
static PluginOnceFlag factoryInitOnce = PLUGIN_ONCE_FLAG_INIT;

void SoundDriverFactory::Initialize()
{
    PluginCallOnce(factoryInitOnce, []()
    {
        /* Call all driver probe functions - they will register valid drivers */
#define SOUND_DRIVER(name) name##Probe();
#include "XDrivers.h"
#undef SOUND_DRIVER
    });
}

SoundDriverInterface* SoundDriverFactory::CreateSoundDriver(SoundDriverType DriverID)
{
    SoundDriverInterface *result = NULL;

    /* Look for our driver */
    for (int x = 0; x < FactoryNextSlot; x++)
    {
        if (FactoryDrivers[x].DriverType == DriverID)
        {
            result = FactoryDrivers[x].CreateFunction();
            if (result != NULL)
                break;
        }
    }

    /* Fallback to NoSound if requested driver is not available */
    if (result == NULL)
        result = new (std::nothrow) NoSoundDriver();

    return result;
}

bool SoundDriverFactory::RegisterSoundDriver(SoundDriverType DriverType, SoundDriverCreationFunction CreateFunction, const char *Description, int Priority)
{
    if (FactoryNextSlot < MAX_FACTORY_DRIVERS)
    {
        FactoryDrivers[FactoryNextSlot].DriverType = DriverType;
        FactoryDrivers[FactoryNextSlot].CreateFunction = CreateFunction;
        FactoryDrivers[FactoryNextSlot].Priority = Priority;
        safe_strcpy(FactoryDrivers[FactoryNextSlot].Description, 99, Description);
        FactoryNextSlot++;
        return true;
    }
    return false;
}

SoundDriverType SoundDriverFactory::DefaultDriver()
{
    int highestPriority = -1;
    SoundDriverType retVal = SoundDriverType::SND_DRIVER_NOSOUND;
    for (int x = 0; x < FactoryNextSlot; x++)
    {
        if (FactoryDrivers[x].Priority > highestPriority)
        {
            retVal = FactoryDrivers[x].DriverType;
            highestPriority = FactoryDrivers[x].Priority;
        }
    }
    return retVal;
}

int SoundDriverFactory::EnumDrivers(SoundDriverType *drivers, int max_entries)
{
    int retVal = 0;
    for (int x = 0; x < FactoryNextSlot; x++)
    {
        if (x >= max_entries) break;
        drivers[x] = FactoryDrivers[x].DriverType;
        retVal++;
    }
    return retVal;
}

const char* SoundDriverFactory::GetDriverDescription(SoundDriverType driver)
{
    for (int x = 0; x < FactoryNextSlot; x++)
    {
        if (driver == FactoryDrivers[x].DriverType)
            return FactoryDrivers[x].Description;
    }
    return "Error";
}

bool SoundDriverFactory::DriverExists(SoundDriverType driver)
{
    for (int x = 0; x < FactoryNextSlot; x++)
    {
        if (driver == FactoryDrivers[x].DriverType)
            return true;
    }
    return false;
}
