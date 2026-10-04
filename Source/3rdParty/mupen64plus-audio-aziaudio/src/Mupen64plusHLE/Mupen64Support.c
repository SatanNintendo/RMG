/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* Mupen64plusHLE support - adapted for Mupen64Plus build                    *
*                                                                           *
* The MusyX microcode handlers come from the mupen64plus-hle audio          *
* project (Copyright Bobby Smiles, GPL).                                    *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
*****************************************************************************/
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "common.h"

#include "hle_external.h"
#include "hle_internal.h"

/* HLE memory globals defined in main.cpp (filled by InitiateAudio) */
extern unsigned char *DRAM;
extern unsigned char *DMEM;
extern unsigned char *IMEM;

void HleWarnMessage(void* user_defined, const char *message, ...)
{
        va_list args;
        va_start(args, message);
        va_end(args);

        (void)user_defined;
        (void)message;
}

void HleVerboseMessage(void* user_defined, const char *message, ...)
{
        va_list args;
        va_start(args, message);
        va_end(args);

        (void)user_defined;
        (void)message;
}

/* Stub implementations for HLE external functions
 * (the audio plugin owns the alist dispatch, not the RSP plugin) */
void HleCheckInterrupts(void* user_defined) { (void)user_defined; }
void HleProcessDlistList(void* user_defined) { (void)user_defined; }
void HleProcessAlistList(void* user_defined) { (void)user_defined; }
void HleProcessRdpList(void* user_defined) { (void)user_defined; }
void HleShowCFB(void* user_defined) { (void)user_defined; }
void HleErrorMessage(void* user_defined, const char *message, ...) { (void)user_defined; (void)message; }

static struct hle_t _hle;

void SetupMusyX(void)
{
        struct hle_t *hle = &_hle;
        memset(hle, 0, sizeof(*hle));

        hle->dram = DRAM;
        hle->dmem = DMEM;
        hle->imem = IMEM;
}

void ProcessMusyX_v1(void)
{
        SetupMusyX();
        musyx_v1_task(&_hle);
}

void ProcessMusyX_v2(void)
{
        SetupMusyX();
        musyx_v2_task(&_hle);
}
