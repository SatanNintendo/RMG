/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* Sound Driver Registrar                                                    *
*                                                                           *
* License: GNU/GPLv2                                                        *
*                                                                           *
****************************************************************************/
#pragma once

#include "SoundDriverFactory.h"

#define REGISTER_DRIVER(cls, type, name, lvl) \
    void cls##Probe() { \
        if (cls::ValidateDriver()) { \
            SoundDriverFactory::RegisterSoundDriver(type, cls::CreateSoundDriver, name, lvl); \
        } \
    }
