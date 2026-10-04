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

#include "audiohle.h"

void UNKNOWN () {
}

p_func ABI2[NUM_ABI_COMMANDS] = {
    SPNOOP    ,ADPCM2    ,CLEARBUFF2,UNKNOWN   ,
    ADDMIXER  ,RESAMPLE2 ,UNKNOWN   ,SEGMENT2  ,
    SETBUFF2  ,DUPLICATE2,DMEMMOVE2 ,LOADADPCM2,
    MIXER2   ,INTERLEAVE2,HILOGAIN  ,SETLOOP2  ,
    SPNOOP    ,INTERL2   ,ENVSETUP1 ,ENVMIXER2 ,
    LOADBUFF2 ,SAVEBUFF2 ,ENVSETUP2 ,SPNOOP    ,
    HILOGAIN  ,SPNOOP    ,DUPLICATE2,UNKNOWN   ,
    SPNOOP    ,SPNOOP    ,SPNOOP    ,SPNOOP    ,
};

/* NOTES:

  FILTER/SEGMENT - Still needs to be finished up... add FILTER?
  UNKNOWWN #27   - Is this worth doing?  Looks like a pain in the ass just for WaveRace64
*/
