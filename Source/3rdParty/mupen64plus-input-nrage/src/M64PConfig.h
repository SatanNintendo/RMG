/*
        N-Rage`s Dinput8 Plugin -- Mupen64Plus configuration storage
    (C) 2002, 2006  Norbert Wladyka
    (C) 2026        Mupen64Plus port

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _M64PCONFIG_H_
#define _M64PCONFIG_H_

#include "commonIncludes.h"

// Reads the plugin configuration from the Mupen64Plus core config file
// (mupen64plus.cfg) into the working structures (g_pcControllers,
// g_scShortcuts, g_strEmuInfo, folder/browser globals).  On a fresh install
// (no [NRage] section yet) the embedded default profiles are loaded.
// Returns false only when the core Config API is unavailable.
bool LoadConfigFromM64P(void);

// Writes the interface's (config dialog's) idea of the configuration to the
// Mupen64Plus core config file and saves the plugin's sections.
// Returns false when the core Config API is unavailable or writing failed.
bool StoreConfigToM64P(void);

// Reads the Language setting from the [NRage] section.  Returns 0 when the
// core is not hooked or no language has been configured.
LANGID GetLanguageFromM64P(void);

#endif // _M64PCONFIG_H_
