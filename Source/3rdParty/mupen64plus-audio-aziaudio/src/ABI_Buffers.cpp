/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin for Project64 Compatible N64 Emulators          *
* http://www.apollo64.com/                                                  *
* Copyright (C) 2000-2019 Azimer. All rights reserved.                      *
*                                                                           *
* License:                                                                  *
* GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html                        *
*                                                                           *
****************************************************************************/

/* memset() and memcpy() */
#include <string.h>

#include "audiohle.h"

/*
 * Every offset and length handled below comes straight from the audio
 * command list, i.e. from the game.  BufferSpace is a fixed 64 KB array,
 * so a broken or hostile list could otherwise read or write past its end.
 * BufferRoom() trims a transfer so that [offset, offset + n) always stays
 * inside BufferSpace.  For every in-range request it returns n unchanged,
 * so correct command lists behave exactly as before.
 */
static inline u32 BufferRoom(u32 offset, u32 n)
{
    if (offset >= (u32)sizeof(BufferSpace))
        return 0;
    u32 room = (u32)sizeof(BufferSpace) - offset;
    return (n < room) ? n : room;
}

void CLEARBUFF() {
    u32 addr = (u32)(k0 & 0xffff);
    u32 count = (u32)(t9 & 0xffff);
    addr &= 0xFFFC;
    u32 n = BufferRoom(addr, (count + 3) & 0xFFFC);
    if (n > 0)
        memset(BufferSpace + addr, 0, n);
}

void CLEARBUFF2() {
    u16 addr = (u16)(k0 & 0xffff);
    u16 count = (u16)(t9 & 0xffff);
    if (count > 0)
    {
        u32 n = BufferRoom(addr, count);
        if (n > 0)
            memset(BufferSpace + addr, 0, n);
    }
}

void CLEARBUFF3() {
    u16 addr = (u16)(k0 & 0xffff);
    u16 count = (u16)(t9 & 0xffff);
    u32 off = (u32)addr + 0x4f0;
    u32 n = BufferRoom(off, count);
    if (n > 0)
        memset(BufferSpace + off, 0, n);
}

void DMEMMOVE() {
    u32 v0, v1;
    u32 cnt;
    if ((t9 & 0xffff) == 0)
        return;
    v0 = (k0 & 0xFFFF);
    v1 = (t9 >> 0x10);

    u32 count = ((t9 + 3) & 0xfffc);

    for (cnt = 0; cnt < count; cnt += 4) {
        BufferSpace[BES((v1 + cnt + 0) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 0) & 0xFFFF)];
        BufferSpace[BES((v1 + cnt + 1) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 1) & 0xFFFF)];
        BufferSpace[BES((v1 + cnt + 2) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 2) & 0xFFFF)];
        BufferSpace[BES((v1 + cnt + 3) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 3) & 0xFFFF)];
    }
}

void DMEMMOVE2() { // Needs accuracy verification...
    u32 v0, v1;
    u32 cnt;
    if ((t9 & 0xffff) == 0)
        return;
    v0 = (k0 & 0xFFFF);
    v1 = (t9 >> 0x10);
    //assert ((v1 & 0x3) == 0);
    //assert ((v0 & 0x3) == 0);
    u32 count = ((t9 + 3) & 0xfffc);
    //v0 = (v0) & 0xfffc;
    //v1 = (v1) & 0xfffc;

    //memcpy (dmem+v1, dmem+v0, count-1);
    for (cnt = 0; cnt < count; cnt += 4) {
        BufferSpace[BES((v1 + cnt + 0) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 0) & 0xFFFF)];
        BufferSpace[BES((v1 + cnt + 1) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 1) & 0xFFFF)];
        BufferSpace[BES((v1 + cnt + 2) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 2) & 0xFFFF)];
        BufferSpace[BES((v1 + cnt + 3) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 3) & 0xFFFF)];
    }
}

void DMEMMOVE3() { // Needs accuracy verification...
    u32 v0, v1;
    u32 cnt;
    v0 = (k0 & 0xFFFF) + 0x4f0;
    v1 = (t9 >> 0x10) + 0x4f0;
    u32 count = ((t9 + 3) & 0xfffc);

    //memcpy (dmem+v1, dmem+v0, count-1);
    for (cnt = 0; cnt < count; cnt += 4) {
        BufferSpace[BES((v1 + cnt + 0) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 0) & 0xFFFF)];
        BufferSpace[BES((v1 + cnt + 1) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 1) & 0xFFFF)];
        BufferSpace[BES((v1 + cnt + 2) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 2) & 0xFFFF)];
        BufferSpace[BES((v1 + cnt + 3) & 0xFFFF)] = BufferSpace[BES((v0 + cnt + 3) & 0xFFFF)];
    }
}

void DUPLICATE2() {
    u16 Count = (k0 >> 16) & 0xff;
    u16 In = k0 & 0xffff;
    u16 Out = (t9 >> 16);

    u16 buff[64];

    memset(buff, 0, sizeof(buff));
    u32 inLen = BufferRoom(In, 128);
    if (inLen > 0)
        memcpy(buff, BufferSpace + In, inLen);

    while (Count) {
        u32 outLen = BufferRoom(Out, 128);
        if (outLen > 0)
            memcpy(BufferSpace + Out, buff, outLen);
        Out += 128;
        Count--;
    }
}

// TODO: This comment has me wondering if there's a problem.  10+ year old comments are hard to remember. -Azimer
void LOADBUFF() { // memcpy causes static... endianess issue :(
    u32 v0;
    if (AudioCount == 0)
        return;
    v0 = (t9 & 0xfffffc);// + SEGMENTS[(t9>>24)&0xf];
    u32 dst = (u32)(AudioInBuffer & 0xFFFC);
    u32 n = BufferRoom(dst, (u32)((AudioCount + 3) & 0xFFFC));
    if (n > 0)
        memcpy(BufferSpace + dst, DRAM + v0, n);
}

void LOADBUFF2() { // Needs accuracy verification...
    u32 v0;
    u32 cnt = (((k0 >> 0xC) + 3) & 0xFFC);
    v0 = (t9 & 0xfffffc);// + SEGMENTS[(t9>>24)&0xf];
    u32 dst = (u32)(k0 & 0xfffc);
    u32 n = BufferRoom(dst, (cnt + 3) & 0xFFFC);
    if (n > 0)
        memcpy(BufferSpace + dst, DRAM + v0, n);
}

void LOADBUFF3() {
    u32 v0;
    u32 cnt = (((k0 >> 0xC) + 3) & 0xFFC);
    v0 = (t9 & 0xfffffc);
    u32 src = (k0 & 0xffc) + 0x4f0;
    u32 n = BufferRoom(src, cnt);
    if (n > 0)
        memcpy(BufferSpace + src, DRAM + v0, n);
}

// TODO: This comment has me wondering if there's a problem.  10+ year old comments are hard to remember. -Azimer
void SAVEBUFF() { // memcpy causes static... endianess issue :(
    u32 v0;
    if (AudioCount == 0)
        return;
    v0 = (t9 & 0xfffffc);// + SEGMENTS[(t9>>24)&0xf];
    u32 src = (u32)(AudioOutBuffer & 0xFFFC);
    u32 n = BufferRoom(src, (u32)((AudioCount + 3) & 0xFFFC));
    if (n > 0)
        memcpy(DRAM + v0, BufferSpace + src, n);
}

void SAVEBUFF2() { // Needs accuracy verification...
    u32 v0;
    u32 cnt = (((k0 >> 0xC) + 3) & 0xFFC);
    v0 = (t9 & 0xfffffc);// + SEGMENTS[(t9>>24)&0xf];
    u32 src = (u32)(k0 & 0xfffc);
    u32 n = BufferRoom(src, (cnt + 3) & 0xFFFC);
    if (n > 0)
        memcpy(DRAM + v0, BufferSpace + src, n);
}

void SAVEBUFF3() {
    u32 v0;
    u32 cnt = (((k0 >> 0xC) + 3) & 0xFFC);
    v0 = (t9 & 0xfffffc);
    u32 src = (k0 & 0xffc) + 0x4f0;
    u32 n = BufferRoom(src, cnt);
    if (n > 0)
        memcpy(DRAM + v0, BufferSpace + src, n);
}

void SEGMENT() { // Should work
    SEGMENTS[(t9 >> 24) & 0xf] = (t9 & 0xffffff);
}

void SEGMENT2() {
    if (isZeldaABI) {
        FILTER2();
        return;
    }
    if ((k0 & 0xffffff) == 0) {
        isMKABI = true;
        //SEGMENTS[(t9>>24)&0xf] = (t9 & 0xffffff);
    }
    else {
        isMKABI = false;
        isZeldaABI = true;
        FILTER2();
    }
}

void SETBUFF() { // Should work ;-)
    if ((k0 >> 0x10) & 0x8) { // A_AUX - Auxillary Sound Buffer Settings
        AudioAuxA = (u16)(k0 & 0xFFFF);
        AudioAuxC = (u16)((t9 >> 0x10));
        AudioAuxE = (u16)(t9 & 0xFFFF);
    }
    else {      // A_MAIN - Main Sound Buffer Settings
        AudioInBuffer = (u16)(k0 & 0xFFFF);           // 0x00
        AudioOutBuffer = (u16)((t9 >> 0x10)); // 0x02
        AudioCount = (u16)(t9 & 0xFFFF);           // 0x04
    }
}

void SETBUFF2() {
    AudioInBuffer = (u16)(k0 & 0xFFFF);           // 0x00
    AudioOutBuffer = (u16)(t9 >> 0x10);   // 0x02
    AudioCount = (u16)(t9 & 0xFFFF);           // 0x04
}

void SETLOOP() {
    loopval = (t9 & 0xffffff);// + SEGMENTS[(t9>>24)&0xf];
    //VolTrg_Left  = (s16)(loopval>>16);        // m_LeftVol
    //VolRamp_Left = (s16)(loopval);    // m_LeftVolTarget
}

void SETLOOP2() {
    loopval = t9 & 0xffffff; // No segment?
}

void SETLOOP3() {
    loopval = (t9 & 0xffffff);
}
