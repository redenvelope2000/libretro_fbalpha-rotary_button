#ifndef __RETRO_INPUT__
#define __RETRO_INPUT__

#include "burner.h"

#define RETROPAD_CLASSIC	RETRO_DEVICE_ANALOG
#define RETROPAD_MODERN		RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#define RETROMOUSE_BALL		RETRO_DEVICE_MOUSE
#define RETROMOUSE_FULL		RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1)

#define JOY_NEG 0
#define JOY_POS 1
#define MAX_KEYBINDS 0x5000
#define RETRO_DEVICE_ID_JOYPAD_EMPTY 255

void SetDiagInpHoldFrameDelay(unsigned val);
void InputMake(void);
bool GameInpApplyMacros();
bool poll_diag_input();
void init_macro_input_descriptors();
void set_input_descriptors();
void InputInit();
void InputDeInit();

#endif
