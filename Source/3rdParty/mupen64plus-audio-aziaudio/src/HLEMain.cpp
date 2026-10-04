/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin for Project64 Compatible N64 Emulators          *
* http://www.apollo64.com/                                                  *
* Copyright (C) 2000-2019 Azimer. All rights reserved.                      *
*                                                                           *
* HLEStart - the AziAudio HLE audio task dispatcher.                      *
*                                                                           *
* Called through ProcessAList() when the RSP plugin forwards an audio      *
* task list to this plugin.  Detects the audio microcode variant from     *
* the ucode data in RDRAM and dispatches the command list through the      *
* matching ABI command table.                                              *
*                                                                           *
* License:                                                                  *
* GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html                        *
*                                                                           *
****************************************************************************/

/* memset() and memcpy() */
#include <string.h>

#include "audiohle.h"

/* HLE memory globals - filled by InitiateAudio() */
u8 * DMEM;
u8 * IMEM;
u8 * DRAM;

/* Current audio command (dispatched through the ABI table) */
u32 t9, k0;

/* Variables needed for ABI HLE */
u8 BufferSpace[0x10000];
short hleMixerWorkArea[256];
u32 SEGMENTS[0x10];             // 0x0320
u16 AudioInBuffer;              // 0x0000(T8)
u16 AudioOutBuffer;             // 0x0002(T8)
u16 AudioCount;                 // 0x0004(T8)
u16 AudioAuxA;                  // 0x000A(T8)
u16 AudioAuxC;                  // 0x000C(T8)
u16 AudioAuxE;                  // 0x000E(T8)
u32 loopval;                    // 0x0010(T8) // Value set by A_SETLOOP : Possible conflict with SETVOLUME???
bool isMKABI = false;
bool isZeldaABI = false;

s32 acc[32][N];
s16 acc_clamped[N];

u32 UCData, UDataLen;

extern "C"
{
    // MusyX HLE provided by Mupen64Plus authored by Bobby Smiles
    void ProcessMusyX_v1();
    void ProcessMusyX_v2();
}

/* Audio UCode command tables - defined in the ABI*.cpp translation units.
 *
 *     ABI 1 : Mario64, WaveRace USA, Golden Eye 007, Quest64, SF Rush
 *             60% of all games use this.  Distributed 3rd Party ABI
 *             (ABI1GE: GoldenEye variant)
 *
 *     ABI 2 : WaveRace JAP, MarioKart 64, Mario64 JAP RumbleEdition,
 *             Yoshi Story, Pokemon Games, Zelda64, Zelda MoM (miyamoto)
 *             Most NCL or NOA games (Most commands)
 *
 *     ABI 3 : DK64, Perfect Dark, Banjo Kazooi, Banjo Tooie
 *             All RARE games except Golden Eye 007
 */
extern p_func ABI1[NUM_ABI_COMMANDS];
extern p_func ABI1GE[NUM_ABI_COMMANDS];
extern p_func ABI2[NUM_ABI_COMMANDS];
extern p_func ABI3[NUM_ABI_COMMANDS];

/* The active command table */
p_func ABI[NUM_ABI_COMMANDS];

void SPNOOP() {
}

void HLEStart() {
    u32 List = ((u32*)DMEM)[0xFF0 / 4], ListLen = ((u32*)DMEM)[0xFF4 / 4];
    u32 *HLEPtr = (u32 *)(DRAM + List);

    UCData = ((u32*)DMEM)[0xFD8 / 4];
    UDataLen = ((u32*)DMEM)[0xFDC / 4];

    loopval = 0;
    memset(SEGMENTS, 0, 0x10 * 4);
    isMKABI = false;
    isZeldaABI = false;

    u8 * UData = DRAM + UCData;

    // Detect uCode
    if (((u32*)UData)[0] != 0x1) {
        switch (*(u32*)(UData + (0x10)))
        {
            case 0x00000001: // MusyX v1
                // RogueSquadron, ResidentEvil2, PolarisSnoCross,
                // TheWorldIsNotEnough, RugratsInParis, NBAShowTime,
                // HydroThunder, Tarzan, GauntletLegend, Rush2049
                ProcessMusyX_v1();
                return;
            default:
                return;

            case 0x0000127c:  break; // naudio (many games)
            case 0x00001280:  break; // BanjoKazooie
            case 0x1c58126c:  break; // DonkeyKong
            case 0x1ae8143c:  break; // BanjoTooie, JetForceGemini, MickeySpeedWayUSA, PerfectDark
            case 0x1ab0140c:  break; // ConkerBadFurDay
        }
        memcpy(ABI, ABI3, NUM_ABI_COMMANDS * sizeof(p_func));
    }
    else
    {
        if (*(u32*)(UData + (0x30)) == 0xF0000F00)
        { // Should be common in ABI 1
            switch (*(u32*)(UData + (0x28)))
            {
                case 0x1e24138c:
                    memcpy(ABI, ABI1, NUM_ABI_COMMANDS * sizeof(p_func));
                    break;
                case 0x1dc8138c: // GoldenEye
                    memcpy(ABI, ABI1GE, NUM_ABI_COMMANDS * sizeof(p_func));
                    break;
                case 0x1e3c1390: // BlastCorp, DiddyKongRacing
                    memcpy(ABI, ABI1GE, NUM_ABI_COMMANDS * sizeof(p_func));
                    break;
                default: return;
            }
        }
        else
        {
            switch (*(u32*)(UData + (0x10))) // ABI2 and MusyX
            {
                case 0x00010010: // MusyX v2 (IndianaJones, BattleForNaboo)
                    ProcessMusyX_v2();
                    return;
                default:
                    return;

                case 0x11181350:  break; // MarioKart, WaveRace (E)
                case 0x111812e0:  break; // StarFox (J)
                case 0x110412ac:  break; // WaveRace (J RevB)
                case 0x110412cc:  break; // StarFox/LylatWars (except J)
                case 0x1cd01250:  break; // FZeroX
                case 0x1f08122c:  break; // YoshisStory
                case 0x1f38122c:  break; // 1080 Snowboarding
                case 0x1f681230:  break; // Zelda OoT / Zelda MM (J, J RevA)
                case 0x1f801250:  break; // Zelda MM (except J, J RevA, E Beta), PokemonStadium 2
                case 0x109411f8:  break; // Zelda MM (E Beta)
                case 0x1eac11b8:  break; // AnimalCrossing
            }
            memcpy(ABI, ABI2, NUM_ABI_COMMANDS * sizeof(p_func));
        }
    }

    ListLen = ListLen >> 2;

    for (u32 x = 0; x < ListLen; x += 2) {
        unsigned char command;

        k0 = HLEPtr[x + 0];
        t9 = HLEPtr[x + 1];
        command = (unsigned char)((k0 >> 24) & 0xFF);

        ABI[command]();
    }
}
