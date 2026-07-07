/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2026 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "Version.hpp"
#include "Library.hpp"
#include "Config.hpp"

//
// Exported Functions
//

CORE_EXPORT std::string CoreGetVersion(void)
{
    // Hardcoded version string.
    //
    // The previous implementation returned the CORE_VERSION macro, which was
    // resolved at build time from `git describe --tags --always` (or the
    // VERSION file, or the -DRMG_VERSION CMake option). On builds without
    // proper git tags — or when the CMake cache was stale — this produced
    // a commit hash or garbage in the window title and About dialog.
    //
    // Per the fork maintainer's request, the version is now hardcoded to
    // "0.9.1" so the displayed version is always clean and deterministic,
    // regardless of build environment state.
    return std::string("0.9.1");
}
