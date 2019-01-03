#ifndef __RETRO_CDEMU__
#define __RETRO_CDEMU__

#include "burner.h"
#include <audio/audio_mixer.h>
#include <audio/conversion/float_to_s16.h>

#ifndef INT16_MAX
#define INT16_MAX    0x7fff
#endif

#ifndef INT16_MIN
#define INT16_MIN    (-INT16_MAX - 1)
#endif

#define CLAMP_I16(x)    (x > INT16_MAX ? INT16_MAX : x < INT16_MIN ? INT16_MIN : x)

TCHAR* GetIsoPath();
INT32 CDEmuInit();
INT32 CDEmuExit();
INT32 CDEmuStop();
INT32 CDEmuPlay(UINT8 M, UINT8 S, UINT8 F);
INT32 CDEmuLoadSector(INT32 LBA, char* pBuffer);
UINT8* CDEmuReadTOC(INT32 track);
UINT8* CDEmuReadQChannel();
INT32 CDEmuGetSoundBuffer(INT16* buffer, INT32 samples);
void wav_exit();

extern CDEmuStatusValue CDEmuStatus;
extern TCHAR CDEmuImage[MAX_PATH];

#endif
