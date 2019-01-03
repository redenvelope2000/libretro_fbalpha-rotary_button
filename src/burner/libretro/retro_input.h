#ifndef __RETRO_INPUT__
#define __RETRO_INPUT__

#include <vector>
#include "burner.h"

#define RETROPAD_CLASSIC	RETRO_DEVICE_ANALOG
#define RETROPAD_MODERN		RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#define RETROMOUSE_BALL		RETRO_DEVICE_MOUSE
#define RETROMOUSE_FULL		RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1)

#define JOY_NEG 0
#define JOY_POS 1
#define MAX_KEYBINDS 0x5000
#define RETRO_DEVICE_ID_JOYPAD_EMPTY 255


extern retro_input_state_t input_cb;
extern UINT8 diag_input_hold_frame_delay;
extern int diag_input_combo_start_frame;
extern uint8_t keybinds[MAX_KEYBINDS][5];
extern uint8_t axibinds[5][8][3];
extern std::vector<retro_input_descriptor> normal_input_descriptors;
extern bool input_initialized;
extern UINT16 switch_ncode;
extern unsigned fba_devices[5];

INT32 GameInpAutoOne(struct GameInp* pgi, char* szi, char *szn);
bool apply_macro_from_variables();
bool poll_diag_input();
bool init_input();

#endif
