#ifndef __RETRO_INPUT__
#define __RETRO_INPUT__

#include "burner.h"

struct KeyBind
{
	unsigned id;
	unsigned port;
	unsigned device;
	int index;
	unsigned position;
};

#define RETROPAD_CLASSIC	RETRO_DEVICE_ANALOG
#define RETROPAD_MODERN		RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#define RETROMOUSE_BALL		RETRO_DEVICE_MOUSE
#define RETROMOUSE_FULL		RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1)

#define JOY_NEG 0
#define JOY_POS 1
#define MAX_KEYBINDS 255 // For whatever reason, previously this value was 0x5000 aka 20480, however 255 keybinds should be enough for 1 game
#define RETRO_DEVICE_ID_JOYPAD_EMPTY 255

void SetDiagInpHoldFrameDelay(unsigned val);
void InputMake(void);
bool GameInpApplyMacros();
void InitMacroInputDescriptors();
void SetInputDescriptors();
void InputInit();
void InputDeInit();

#endif
