#include "retro_common.h"
#include "retro_input.h"

retro_input_state_t input_cb;
UINT8 diag_input_hold_frame_delay = 0;
int diag_input_combo_start_frame = 0;
uint8_t keybinds[MAX_KEYBINDS][5];
uint8_t axibinds[5][8][3];
std::vector<retro_input_descriptor> normal_input_descriptors;
bool input_initialized = false;
UINT16 switch_ncode = 0;
unsigned fba_devices[5] = { RETROPAD_CLASSIC, RETROPAD_CLASSIC, RETROPAD_CLASSIC, RETROPAD_CLASSIC, RETROPAD_CLASSIC };

static bool bAnalogRightMappingDone[5][2][2];
static bool bButtonMapped = false;
static bool one_diag_input_pressed = false;
static bool all_diag_input_pressed = true;
static bool diag_combo_activated = false;

// Analog to analog mapping
static INT32 GameInpAnalog2RetroInpAnalog(struct GameInp* pgi, UINT32 nJoy, UINT8 nAxis, UINT32 nKey, UINT32 nIndex, char *szn, UINT8 nInput = GIT_JOYAXIS_FULL, INT32 nSliderValue = 0x8000, INT16 nSliderSpeed = 0x0E00, INT16 nSliderCenter = 10)
{
	if(bButtonMapped) return 0;
	switch (nInput)
	{
		case GIT_JOYAXIS_FULL:
		{
			pgi->nInput = GIT_JOYAXIS_FULL;
			pgi->Input.JoyAxis.nAxis = nAxis;
			pgi->Input.JoyAxis.nJoy = (UINT8)nJoy;
			axibinds[nJoy][nAxis][0] = nIndex;
			axibinds[nJoy][nAxis][1] = nKey;
			retro_input_descriptor descriptor;
			descriptor.port = nJoy;
			descriptor.device = RETRO_DEVICE_ANALOG;
			descriptor.index = nIndex;
			descriptor.id = nKey;
			descriptor.description = szn;
			normal_input_descriptors.push_back(descriptor);
			break;
		}
		case GIT_JOYSLIDER:
		{
			pgi->nInput = GIT_JOYSLIDER;
			pgi->Input.Slider.nSliderValue = nSliderValue;
			pgi->Input.Slider.nSliderSpeed = nSliderSpeed;
			pgi->Input.Slider.nSliderCenter = nSliderCenter;
			pgi->Input.Slider.JoyAxis.nAxis = nAxis;
			pgi->Input.Slider.JoyAxis.nJoy = (UINT8)nJoy;
			axibinds[nJoy][nAxis][0] = nIndex;
			axibinds[nJoy][nAxis][1] = nKey;
			retro_input_descriptor descriptor;
			descriptor.port = nJoy;
			descriptor.device = RETRO_DEVICE_ANALOG;
			descriptor.index = nIndex;
			descriptor.id = nKey;
			descriptor.description = szn;
			normal_input_descriptors.push_back(descriptor);
			break;
		}
		case GIT_MOUSEAXIS:
		{
			pgi->nInput = GIT_MOUSEAXIS;
			pgi->Input.MouseAxis.nAxis = nAxis;
			pgi->Input.MouseAxis.nMouse = (UINT8)nJoy;
			axibinds[nJoy][nAxis][0] = nIndex;
			axibinds[nJoy][nAxis][1] = nKey;
			retro_input_descriptor descriptor;
			descriptor.port = nJoy;
			descriptor.device = RETRO_DEVICE_MOUSE;
			descriptor.index = nIndex;
			descriptor.id = nKey;
			descriptor.description = szn;
			normal_input_descriptors.push_back(descriptor);
			break;
		}
		// I'm not sure the 2 following settings are needed in the libretro port
		case GIT_JOYAXIS_NEG:
		{
			pgi->nInput = GIT_JOYAXIS_NEG;
			pgi->Input.JoyAxis.nAxis = nAxis;
			pgi->Input.JoyAxis.nJoy = (UINT8)nJoy;
			axibinds[nJoy][nAxis][0] = nIndex;
			axibinds[nJoy][nAxis][1] = nKey;
			retro_input_descriptor descriptor;
			descriptor.port = nJoy;
			descriptor.device = RETRO_DEVICE_ANALOG;
			descriptor.index = nIndex;
			descriptor.id = nKey;
			descriptor.description = szn;
			normal_input_descriptors.push_back(descriptor);
			break;
		}
		case GIT_JOYAXIS_POS:
		{
			pgi->nInput = GIT_JOYAXIS_POS;
			pgi->Input.JoyAxis.nAxis = nAxis;
			pgi->Input.JoyAxis.nJoy = (UINT8)nJoy;
			axibinds[nJoy][nAxis][0] = nIndex;
			axibinds[nJoy][nAxis][1] = nKey;
			retro_input_descriptor descriptor;
			descriptor.port = nJoy;
			descriptor.device = RETRO_DEVICE_ANALOG;
			descriptor.index = nIndex;
			descriptor.id = nKey;
			descriptor.description = szn;
			normal_input_descriptors.push_back(descriptor);
			break;
		}
	}
	bButtonMapped = true;
	return 0;
}

// Digital to digital mapping
static INT32 GameInpDigital2RetroInpKey(struct GameInp* pgi, UINT32 nJoy, UINT32 nKey, char *szn, unsigned device = RETRO_DEVICE_JOYPAD)
{
	if(bButtonMapped) return 0;
	pgi->nInput = GIT_SWITCH;
	if (!input_initialized)
		pgi->Input.Switch.nCode = (UINT16)(switch_ncode++);
	keybinds[pgi->Input.Switch.nCode][0] = nKey;
	keybinds[pgi->Input.Switch.nCode][1] = nJoy;
	keybinds[pgi->Input.Switch.nCode][4] = device;
	retro_input_descriptor descriptor;
	descriptor.port = nJoy;
	descriptor.device = device;
	descriptor.index = 0;
	descriptor.id = nKey;
	descriptor.description = szn;
	normal_input_descriptors.push_back(descriptor);
	bButtonMapped = true;
	return 0;
}

// 2 digital to 1 analog mapping
// Need to be run once for each of the 2 pgi (the 2 game inputs)
// nJoy (player) and nKey (axis) needs to be the same for each of the 2 buttons
// position is either JOY_POS or JOY_NEG (the position expected on axis to trigger the button)
// szn is the descriptor text
static INT32 GameInpDigital2RetroInpAnalogRight(struct GameInp* pgi, UINT32 nJoy, UINT32 nKey, UINT32 position, char *szn)
{
	if(bButtonMapped) return 0;
	pgi->nInput = GIT_SWITCH;
	if (!input_initialized)
		pgi->Input.Switch.nCode = (UINT16)(switch_ncode++);
	keybinds[pgi->Input.Switch.nCode][0] = nKey;
	keybinds[pgi->Input.Switch.nCode][1] = nJoy;
	keybinds[pgi->Input.Switch.nCode][2] = RETRO_DEVICE_INDEX_ANALOG_RIGHT;
	keybinds[pgi->Input.Switch.nCode][3] = position;
	keybinds[pgi->Input.Switch.nCode][4] = RETRO_DEVICE_ANALOG;
	bAnalogRightMappingDone[nJoy][nKey][position] = true;
	if(bAnalogRightMappingDone[nJoy][nKey][JOY_POS] && bAnalogRightMappingDone[nJoy][nKey][JOY_NEG]) {
		retro_input_descriptor descriptor;
		descriptor.port = nJoy;
		descriptor.device = RETRO_DEVICE_ANALOG;
		descriptor.index = RETRO_DEVICE_INDEX_ANALOG_RIGHT;
		descriptor.id = nKey;
		descriptor.description = szn;
		normal_input_descriptors.push_back(descriptor);
	}
	bButtonMapped = true;
	return 0;
}

// 1 analog to 2 digital mapping
// Needs pgi, player, axis, 2 buttons retropad id and 2 descriptions
static INT32 GameInpAnalog2RetroInpDualKeys(struct GameInp* pgi, UINT32 nJoy, UINT8 nAxis, UINT32 nKeyPos, UINT32 nKeyNeg, char *sznpos, char *sznneg)
{
	if(bButtonMapped) return 0;
	pgi->nInput = GIT_JOYAXIS_FULL;
	pgi->Input.JoyAxis.nAxis = nAxis;
	pgi->Input.JoyAxis.nJoy = (UINT8)nJoy;
	axibinds[nJoy][nAxis][0] = 0xff;
	axibinds[nJoy][nAxis][1] = nKeyPos;
	axibinds[nJoy][nAxis][2] = nKeyNeg;

	retro_input_descriptor descriptor;

	descriptor.port = nJoy;
	descriptor.device = RETRO_DEVICE_JOYPAD;
	descriptor.index = 0;
	descriptor.id = nKeyPos;
	descriptor.description = sznpos;
	normal_input_descriptors.push_back(descriptor);

	descriptor.port = nJoy;
	descriptor.device = RETRO_DEVICE_JOYPAD;
	descriptor.index = 0;
	descriptor.id = nKeyNeg;
	descriptor.description = sznneg;
	normal_input_descriptors.push_back(descriptor);

	bButtonMapped = true;
	return 0;
}

// [WIP]
// All inputs which needs special handling need to go in the next function
// TODO : move analog accelerators to R2
static INT32 GameInpSpecialOne(struct GameInp* pgi, INT32 nPlayer, char* szi, char *szn, char *description)
{
	const char * parentrom	= BurnDrvGetTextA(DRV_PARENT);
	const char * drvname	= BurnDrvGetTextA(DRV_NAME);
	const char * systemname = BurnDrvGetTextA(DRV_SYSTEM);

	if (fba_devices[nPlayer] == RETROMOUSE_BALL || fba_devices[nPlayer] == RETROMOUSE_FULL) {
		if (strcmp("x-axis", szi + 3) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_MOUSE_X, RETRO_DEVICE_MOUSE, description, GIT_MOUSEAXIS);
		}
		if (strcmp("y-axis", szi + 3) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_MOUSE_Y, RETRO_DEVICE_MOUSE, description, GIT_MOUSEAXIS);
		}
		if (strcmp("mouse x-axis", szi) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_MOUSE_X, RETRO_DEVICE_MOUSE, description, GIT_MOUSEAXIS);
		}
		if (strcmp("mouse y-axis", szi) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_MOUSE_Y, RETRO_DEVICE_MOUSE, description, GIT_MOUSEAXIS);
		}
		if (fba_devices[nPlayer] == RETROMOUSE_FULL) {
			// Handle mouse button mapping (i will keep it simple...)
			if (strcmp("fire 1", szi + 3) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_MOUSE_LEFT, description, RETRO_DEVICE_MOUSE);
			}
			if (strcmp("mouse button 1", szi) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_MOUSE_LEFT, description, RETRO_DEVICE_MOUSE);
			}
			if (strcmp("fire 2", szi + 3) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_MOUSE_RIGHT, description, RETRO_DEVICE_MOUSE);
			}
			if (strcmp("mouse button 2", szi) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_MOUSE_RIGHT, description, RETRO_DEVICE_MOUSE);
			}
			if (strcmp("fire 3", szi + 3) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_MOUSE_MIDDLE, description, RETRO_DEVICE_MOUSE);
			}
			if (strcmp("fire 4", szi + 3) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_MOUSE_BUTTON_4, description, RETRO_DEVICE_MOUSE);
			}
			if (strcmp("fire 5", szi + 3) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_MOUSE_BUTTON_5, description, RETRO_DEVICE_MOUSE);
			}
		}
	}

	// Fix part of issue #102 (Crazy Fight)
	// Can't really manage to have a decent mapping on this one if you don't have a stick/pad with the following 2 rows of 3 buttons :
	// Y X R1
	// B A R2
	if ((parentrom && strcmp(parentrom, "crazyfgt") == 0) ||
		(drvname && strcmp(drvname, "crazyfgt") == 0)
	) {
		if (strcmp("top-left", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
		}
		if (strcmp("top-center", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_X, description);
		}
		if (strcmp("top-right", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
		if (strcmp("bottom-left", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("bottom-center", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
		if (strcmp("bottom-right", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R2, description);
		}
	}

	// Fix part of issue #102 (Bikkuri Card)
	// Coin 2 overwrited Coin 1, which is probably part of the issue
	// I managed to pass the payout tests once, but i don't know how
	if ((parentrom && strcmp(parentrom, "bikkuric") == 0) ||
		(drvname && strcmp(drvname, "bikkuric") == 0)
	) {
		if (strcmp("Coin 2", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_START, description);
		}
	}

	// Fix part of issue #102 (Jackal)
	if ((parentrom && strcmp(parentrom, "jackal") == 0) ||
		(drvname && strcmp(drvname, "jackal") == 0)
	) {
		if (strcmp("Rotate Left", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L, description);
		}
		if (strcmp("Rotate Right", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
	}

	// Fix part of issue #102 (Last Survivor)
	if ((parentrom && strcmp(parentrom, "lastsurv") == 0) ||
		(drvname && strcmp(drvname, "lastsurv") == 0)
	) {
		if (strcmp("Turn Left", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L, description);
		}
		if (strcmp("Turn Right", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
	}

	// Recommandation from http://neosource.1emulation.com/forums/index.php?topic=2991.0 (Power Drift)
	if ((parentrom && strcmp(parentrom, "pdrift") == 0) ||
		(drvname && strcmp(drvname, "pdrift") == 0)
	) {
		if (strcmp("Steering", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description, GIT_JOYSLIDER, 0x8000, 0x0800, 10);
		}
	}

	// Car steer a little too much with default setting + use L/R for Shift Down/Up (Super Monaco GP)
	if ((parentrom && strcmp(parentrom, "smgp") == 0) ||
		(drvname && strcmp(drvname, "smgp") == 0)
	) {
		if (strcmp("Left/Right", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description, GIT_JOYSLIDER, 0x8000, 0x0C00, 10);
		}
		if (strcmp("Shift Down", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L, description);
		}
		if (strcmp("Shift Up", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
	}

	// Fix issue #133 (Night striker)
	if ((parentrom && strcmp(parentrom, "nightstr") == 0) ||
		(drvname && strcmp(drvname, "nightstr") == 0)
	) {
		if (strcmp("Stick Y", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_INDEX_ANALOG_LEFT, description, GIT_JOYSLIDER, 0x8000, 0x0700, 0);
		}
	}
	
	// Fix part of issue #102 (Hang On Junior)
	if ((parentrom && strcmp(parentrom, "hangonjr") == 0) ||
		(drvname && strcmp(drvname, "hangonjr") == 0)
	) {
		if (strcmp("Accelerate", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
	}
	
	// Fix part of issue #102 (Out Run)
	if ((parentrom && strcmp(parentrom, "outrun") == 0) ||
		(drvname && strcmp(drvname, "outrun") == 0)
	) {
		if (strcmp("Gear", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
	}
	
	// Fix part of issue #102 (Golden Axe)
	// Use same layout as megadrive cores
	if ((parentrom && strcmp(parentrom, "goldnaxe") == 0) ||
		(drvname && strcmp(drvname, "goldnaxe") == 0)
	) {
		if (strcmp("Fire 1", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, "Magic");
		}
		if (strcmp("Fire 2", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, "Attack");
		}
		if (strcmp("Fire 3", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, "Jump");
		}
	}
	
	// Fix part of issue #102 (Major League)
	if ((parentrom && strcmp(parentrom, "mjleague") == 0) ||
		(drvname && strcmp(drvname, "mjleague") == 0)
	) {
		if (strcmp("Bat Swing", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Fire 1", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
		if (strcmp("Fire 2", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
		}
		if (strcmp("Fire 3", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_X, description);
		}
		if (strcmp("Fire 4", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
		if (strcmp("Fire 5", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L, description);
		}
	}
	
	// Fix part of issue #102 (Chase HQ)
	if ((parentrom && strcmp(parentrom, "chasehq") == 0) ||
		(drvname && strcmp(drvname, "chasehq") == 0)
	) {
		if (strcmp("Brake", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
		if (strcmp("Accelerate", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Turbo", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
		}
		if (strcmp("Gear", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
	}
	
	// Fix part of issue #102 (SDI - Strategic Defense Initiative)
	if ((parentrom && strcmp(parentrom, "sdi") == 0) ||
		(drvname && strcmp(drvname, "sdi") == 0)
	) {
		if (strcmp("Target L/R", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_RIGHT, description);
		}
		if (strcmp("Target U/D", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_INDEX_ANALOG_RIGHT, description);
		}
		if (strcmp("Fire 1", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
	}
	
	// Fix part of issue #102 (Blades of Steel)
	if ((parentrom && strcmp(parentrom, "bladestl") == 0) ||
		(drvname && strcmp(drvname, "bladestl") == 0)
	) {
		if (strcmp("Trackball X", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_RIGHT, description);
		}
		if (strcmp("Trackball Y", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_INDEX_ANALOG_RIGHT, description);
		}
		if (strcmp("Button 1", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
		if (strcmp("Button 2", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R2, description);
		}
		if (strcmp("Button 3", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L, description);
		}
	}
	
	// Fix part of issue #102 (Forgotten Worlds)
	if ((parentrom && strcmp(parentrom, "forgottn") == 0) ||
		(drvname && strcmp(drvname, "forgottn") == 0)
	) {
		if (strcmp("Turn (analog)", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_RIGHT, description);
		}
		if (strcmp("Attack", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
		if (strcmp("Turn - (digital)", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Turn + (digital)", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
	}
	
	// Chequered Flag, Konami GT, Hyper Crash
	if ((parentrom && strcmp(parentrom, "chqflag") == 0) ||
		(drvname && strcmp(drvname, "chqflag") == 0) ||
		(parentrom && strcmp(parentrom, "konamigt") == 0) ||
		(drvname && strcmp(drvname, "konamigt") == 0) ||
		(parentrom && strcmp(parentrom, "hcrash") == 0) ||
		(drvname && strcmp(drvname, "hcrash") == 0)
	) {
		if (strcmp("Wheel", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description, GIT_JOYSLIDER);
		}
	}
	
	// Pop 'n Bounce / Gapporin
	if ((parentrom && strcmp(parentrom, "popbounc") == 0) ||
		(drvname && strcmp(drvname, "popbounc") == 0)
	) {
		if (strcmp("Paddle", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Button D", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_X, description);
		}
	}
	
	// The Irritating Maze / Ultra Denryu Iraira Bou, Shoot the Bull
	if ((parentrom && strcmp(parentrom, "irrmaze") == 0) ||
		(drvname && strcmp(drvname, "irrmaze") == 0) ||
		(parentrom && strcmp(parentrom, "shootbul") == 0) ||
		(drvname && strcmp(drvname, "shootbul") == 0)
	) {
		if (strcmp("X Axis", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Y Axis", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Button A", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Button B", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
	}
	
	// Laser Ghost
	if ((parentrom && strcmp(parentrom, "lghost") == 0) ||
		(drvname && strcmp(drvname, "lghost") == 0) ||
		(parentrom && strcmp(parentrom, "loffire") == 0) ||
		(drvname && strcmp(drvname, "loffire") == 0)
	) {
		if (strcmp("X-Axis", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Y-Axis", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Fire 1", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Fire 2", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
	}
	
	// Lord of Gun, ...
	if ((parentrom && strcmp(parentrom, "lordgun") == 0) ||
		(drvname && strcmp(drvname, "lordgun") == 0) ||
		(parentrom && strcmp(parentrom, "deerhunt") == 0) ||
		(drvname && strcmp(drvname, "deerhunt") == 0) ||
		(parentrom && strcmp(parentrom, "turkhunt") == 0) ||
		(drvname && strcmp(drvname, "turkhunt") == 0) ||
		(parentrom && strcmp(parentrom, "wschamp") == 0) ||
		(drvname && strcmp(drvname, "wschamp") == 0) ||
		(parentrom && strcmp(parentrom, "trophyh") == 0) ||
		(drvname && strcmp(drvname, "trophyh") == 0) ||
		(parentrom && strcmp(parentrom, "zombraid") == 0) ||
		(drvname && strcmp(drvname, "zombraid") == 0)
	) {
		if (strcmp("Right / left", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Up / Down", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Button 1", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Button 2", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
	}
	
	// Lethal Enforcers, Beast Busters, Bang!, Zero Point 1 & 2, Operation Thunderbolt, Operation Wolf 1 & 3, Space Gun
	if ((parentrom && strcmp(parentrom, "lethalen") == 0) ||
		(drvname && strcmp(drvname, "lethalen") == 0) ||
		(parentrom && strcmp(parentrom, "bbusters") == 0) ||
		(drvname && strcmp(drvname, "bbusters") == 0) ||
		(parentrom && strcmp(parentrom, "bang") == 0) ||
		(drvname && strcmp(drvname, "bang") == 0) ||
		(parentrom && strcmp(parentrom, "zeropnt") == 0) ||
		(drvname && strcmp(drvname, "zeropnt") == 0) ||
		(parentrom && strcmp(parentrom, "zeropnt2") == 0) ||
		(drvname && strcmp(drvname, "zeropnt2") == 0) ||
		(parentrom && strcmp(parentrom, "othunder") == 0) ||
		(drvname && strcmp(drvname, "othunder") == 0) ||
		(parentrom && strcmp(parentrom, "opwolf3") == 0) ||
		(drvname && strcmp(drvname, "opwolf3") == 0) ||
		(parentrom && strcmp(parentrom, "opwolf") == 0) ||
		(drvname && strcmp(drvname, "opwolf") == 0) ||
		(parentrom && strcmp(parentrom, "spacegun") == 0) ||
		(drvname && strcmp(drvname, "spacegun") == 0)
	) {
		if (strcmp("Gun X", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Gun Y", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Button 1", description) == 0 || strcmp("Fire 1", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Button 2", description) == 0 || strcmp("Fire 2", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
		if (strcmp("Button 3", description) == 0 || strcmp("Fire 3", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
		}
	}

	// Rail Chase
	if ((parentrom && strcmp(parentrom, "rchase") == 0) ||
		(drvname && strcmp(drvname, "rchase") == 0)
	) {
		if (strcmp("Left/Right", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Up/Down", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Fire 1", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
	}
	
	// Fix part of issue #102 (VS Block Breaker)
	if ((parentrom && strcmp(parentrom, "vblokbrk") == 0) ||
		(drvname && strcmp(drvname, "vblokbrk") == 0)
	) {
		if (strcmp("Paddle", description) == 0) {
			GameInpAnalog2RetroInpDualKeys(pgi, nPlayer, 0, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L, "Paddle Up", "Paddle Down");
		}
	}
	
	// Fix part of issue #102 (Puzz Loop 2)
	if ((parentrom && strcmp(parentrom, "pzloop2") == 0) ||
		(drvname && strcmp(drvname, "pzloop2") == 0)
	) {
		if (strcmp("Paddle", description) == 0) {
			GameInpAnalog2RetroInpDualKeys(pgi, nPlayer, 0, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L, "Paddle Up", "Paddle Down");
		}
	}
	
	// Fix part of issue #102 (After burner 1 & 2)
	if ((parentrom && strcmp(parentrom, "aburner2") == 0) ||
		(drvname && strcmp(drvname, "aburner2") == 0)
	) {
		if (strcmp("Throttle", description) == 0) {
			GameInpAnalog2RetroInpDualKeys(pgi, nPlayer, 2, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L, "Speed Up", "Speed Down");
		}
	}
	
	// Fix part of issue #156 (Alien vs Predator)
	// Use a layout more similar to the cabinet, with jump in the middle
	if ((parentrom && strcmp(parentrom, "avsp") == 0) ||
		(drvname && strcmp(drvname, "avsp") == 0)
	) {
		if (strcmp("Attack", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
		}
		if (strcmp("Jump", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Shot", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
	}
	
	// Dragon Gun
	if ((parentrom && strcmp(parentrom, "dragngun") == 0) ||
		(drvname && strcmp(drvname, "dragngun") == 0)
	) {
		if (strcmp("Gun X", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Gun Y", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
	}
	
	// Beraboh Man
	if ((parentrom && strcmp(parentrom, "berabohm") == 0) ||
		(drvname && strcmp(drvname, "berabohm") == 0)
	) {
		if (strcmp("Button 1", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Button 2", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
		if (strcmp("Button 3", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, (fba_devices[nPlayer] == RETROPAD_MODERN ? RETRO_DEVICE_ID_JOYPAD_R2 : RETRO_DEVICE_ID_JOYPAD_R), description);
		}
		if (strcmp("Button 4", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
		}
		if (strcmp("Button 5", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_X, description);
		}
		if (strcmp("Button 6", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, (fba_devices[nPlayer] == RETROPAD_MODERN ? RETRO_DEVICE_ID_JOYPAD_R : RETRO_DEVICE_ID_JOYPAD_L), description);
		}
	}
	
	// Mad Planets
	if ((parentrom && strcmp(parentrom, "mplanets") == 0) ||
		(drvname && strcmp(drvname, "mplanets") == 0)
	) {
		if (strcmp("Rotate Left", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L, description);
		}
		if (strcmp("Rotate Right", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
	}
	
	// Street Fighter Zero (CPS Changer) #245
	if ((parentrom && strcmp(parentrom, "sfzch") == 0) ||
		(drvname && strcmp(drvname, "sfzch") == 0)
	) {
		if (strcmp("Pause", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_SELECT, description);
		}
	}
	
	// Toobin' (reported on discord, layout suggestion by alfrix#8029)
	if ((parentrom && strcmp(parentrom, "toobin") == 0) ||
		(drvname && strcmp(drvname, "toobin") == 0)
	) {
		if (strcmp("Throw", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Right.Paddle Forward", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
		if (strcmp("Left Paddle Forward", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L, description);
		}
		if (strcmp("Left Paddle Backward", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L2, description);
		}
		if (strcmp("Right Paddle Backward", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R2, description);
		}
	}
	
	// Vindicators (reported on discord, layout suggestion by alfrix#8029)
	if ((parentrom && strcmp(parentrom, "vindictr") == 0) ||
		(drvname && strcmp(drvname, "vindictr") == 0)
	) {
		if (strcmp("Left Stick Up", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L2, description);
		}
		if (strcmp("Left Stick Down", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L, description);
		}
		if (strcmp("Right Stick Up", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R2, description);
		}
		if (strcmp("Right Stick Down", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
		if (strcmp("-- Alt. Input --", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L3, description);
		}
	}
	
	// Vindicators Part II (reported on discord, layout suggestion by alfrix#8029)
	if ((parentrom && strcmp(parentrom, "vindctr2") == 0) ||
		(drvname && strcmp(drvname, "vindctr2") == 0)
	) {
		if (strcmp("Left Stick Up", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L2, description);
		}
		if (strcmp("Left Stick Down", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L, description);
		}
		if (strcmp("Right Stick Up", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R2, description);
		}
		if (strcmp("Right Stick Down", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
		if (strcmp("Right Stick Fire", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
		if (strcmp("Left Stick Thumb", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
		}
	}
	
	// Thunder Ceptor & Thunder Ceptor II (#33)
	if ((parentrom && strcmp(parentrom, "tceptor") == 0) ||
		(drvname && strcmp(drvname, "tceptor") == 0)
	) {
		if (strcmp("X Axis", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Y Axis", description) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		}
		if (strcmp("Accelerator", description) == 0) {
			// It would be nice to have this control working as analog on R2
			//GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 2, RETRO_DEVICE_ID_JOYPAD_R2, RETRO_DEVICE_INDEX_ANALOG_BUTTON, description);
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R2, description);
		}
	}
	
	// Hydra (reported on discord, layout suggestion by alfrix#8029)
	if ((parentrom && strcmp(parentrom, "hydra") == 0) ||
		(drvname && strcmp(drvname, "hydra") == 0)
	) {
		if (strcmp("Accelerator", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R2, description);
		}
		if (strcmp("Boost", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L2, description);
		}
		if (strcmp("Right Trigger", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
		if (strcmp("Left Trigger", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Right Thumb", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_X, description);
		}
		if (strcmp("Left Thumb", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
		}
	}

	// Assault
	if ((parentrom && strcmp(parentrom, "assault") == 0) ||
		(drvname && strcmp(drvname, "assault") == 0)
	) {
		if (strcmp("Right Stick Up", description) == 0) {
			GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_Y, JOY_NEG, "Right Stick Up / Down");
		}
		if (strcmp("Right Stick Down", description) == 0) {
			GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_Y, JOY_POS, "Right Stick Up / Down");
		}
		if (strcmp("Right Stick Left", description) == 0) {
			GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_X, JOY_NEG, "Right Stick Left / Right");
		}
		if (strcmp("Right Stick Right", description) == 0) {
			GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_X, JOY_POS, "Right Stick Left / Right");
		}
	}
	
	// Handle megadrive
	if ((systemname && strcmp(systemname, "Sega Megadrive") == 0)) {
		// Street Fighter 2 mapping (which is the only 6 button megadrive game ?)
		// Use same layout as arcade
		if ((parentrom && strcmp(parentrom, "md_sf2") == 0) ||
			(drvname && strcmp(drvname, "md_sf2") == 0)
		) {
			if (strcmp("Button A", description) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, "Weak Kick");
			}
			if (strcmp("Button B", description) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, "Medium Kick");
			}
			if (strcmp("Button C", description) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, (fba_devices[nPlayer] == RETROPAD_MODERN ? RETRO_DEVICE_ID_JOYPAD_R2 : RETRO_DEVICE_ID_JOYPAD_R), "Strong Kick");
			}
			if (strcmp("Button X", description) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, "Weak Punch");
			}
			if (strcmp("Button Y", description) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_X, "Medium Punch");
			}
			if (strcmp("Button Z", description) == 0) {
				GameInpDigital2RetroInpKey(pgi, nPlayer, (fba_devices[nPlayer] == RETROPAD_MODERN ? RETRO_DEVICE_ID_JOYPAD_R : RETRO_DEVICE_ID_JOYPAD_L), "Strong Punch");
			}
		}
		// Generic megadrive mapping
		// Use same layout as megadrive cores
		if (strcmp("Button A", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
		}
		if (strcmp("Button B", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Button C", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
	}
	
	// Handle MSX
	if ((systemname && strcmp(systemname, "MSX") == 0)) {
		if (strcmp("Button 1", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
		}
		if (strcmp("Button 2", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
		}
		if (strcmp("Key F1", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_START, description);
		}
		if (strcmp("Key F2", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_SELECT, description);
		}
		if (strcmp("Key F3", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
		}
		if (strcmp("Key F4", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_X, description);
		}
		if (strcmp("Key F5", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
		}
		if (strcmp("Key F6", description) == 0) {
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L, description);
		}
		if (strcmp("Key UP", description) == 0) {
			GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_Y, JOY_NEG, "Key UP / Key DOWN");
		}
		if (strcmp("Key DOWN", description) == 0) {
			GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_Y, JOY_POS, "Key UP / Key DOWN");
		}
		if (strcmp("Key LEFT", description) == 0) {
			GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_X, JOY_NEG, "Key LEFT / Key RIGHT");
		}
		if (strcmp("Key RIGHT", description) == 0) {
			GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_X, JOY_POS, "Key LEFT / Key RIGHT");
		}
	}

	// Fix part of issue #102 (Twin stick games)
	if ((strcmp("Up 2", description) == 0) ||
		(strcmp("Up (right)", description) == 0) ||
		(strcmp("Right Up", description) == 0)
	) {
		GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_Y, JOY_NEG, "Up / Down (Right Stick)");
	}
	if ((strcmp("Down 2", description) == 0) ||
		(strcmp("Down (right)", description) == 0) ||
		(strcmp("Right Down", description) == 0)
	) {
		GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_Y, JOY_POS, "Up / Down (Right Stick)");
	}
	if ((strcmp("Left 2", description) == 0) ||
		(strcmp("Left (right)", description) == 0) ||
		(strcmp("Right Left", description) == 0)
	) {
		GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_X, JOY_NEG, "Left / Right (Right Stick)");
	}
	if ((strcmp("Right 2", description) == 0) ||
		(strcmp("Right (right)", description) == 0) ||
		(strcmp("Right Right", description) == 0)
	) {
		GameInpDigital2RetroInpAnalogRight(pgi, nPlayer, RETRO_DEVICE_ID_ANALOG_X, JOY_POS, "Left / Right (Right Stick)");
	}
	
	// Default racing games's Steering control to the joyslider type of analog control
	// Joyslider is some sort of "wheel" emulation
	if ((BurnDrvGetGenreFlags() & GBF_RACING)) {
		if (strcmp("x-axis", szi + 3) == 0) {
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description, GIT_JOYSLIDER);
		}
	}

	return 0;
}

// Handle mapping of an input
// Will delegate to GameInpSpecialOne for cases which needs "fine tuning"
// Use GameInp2RetroInp for the actual mapping
INT32 GameInpAutoOne(struct GameInp* pgi, char* szi, char *szn)
{
	bool bPlayerInInfo = (toupper(szi[0]) == 'P' && szi[1] >= '1' && szi[1] <= '5'); // Because some of the older drivers don't use the standard input naming.
	bool bPlayerInName = (szn[0] == 'P' && szn[1] >= '1' && szn[1] <= '5');

	if (bPlayerInInfo || bPlayerInName) {
		INT32 nPlayer = -1;

		if (bPlayerInName)
			nPlayer = szn[1] - '1';
		if (bPlayerInInfo && nPlayer == -1)
			nPlayer = szi[1] - '1';

		char* szb = szi + 3;

		// "P1 XXX" - try to exclude the "P1 " from the szName
		INT32 offset_player_x = 0;
		if (strlen(szn) > 3 && szn[0] == 'P' && szn[2] == ' ')
			offset_player_x = 3;
		char* description = szn + offset_player_x;

		bButtonMapped = false;
		GameInpSpecialOne(pgi, nPlayer, szi, szn, description);
		if(bButtonMapped) return 0;

		if (strncmp("select", szb, 6) == 0)
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_SELECT, description);
		if (strncmp("coin", szb, 4) == 0)
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_SELECT, description);
		if (strncmp("start", szb, 5) == 0)
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_START, description);
		if (strncmp("up", szb, 2) == 0)
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_UP, description);
		if (strncmp("down", szb, 4) == 0)
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_DOWN, description);
		if (strncmp("left", szb, 4) == 0)
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_LEFT, description);
		if (strncmp("right", szb, 5) == 0)
			GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_RIGHT, description);
		if (strncmp("x-axis", szb, 6) == 0)
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 0, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);
		if (strncmp("y-axis", szb, 6) == 0)
			GameInpAnalog2RetroInpAnalog(pgi, nPlayer, 1, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_INDEX_ANALOG_LEFT, description);

		if (strncmp("fire ", szb, 5) == 0) {
			char *szf = szb + 5;
			INT32 nButton = strtol(szf, NULL, 0);
			// The "Modern" mapping on neogeo (which mimic mapping from remakes and further opus of the series)
			// doesn't make sense and is kinda harmful on games other than kof, fatal fury and samourai showdown
			// So we restrain it to those 3 series.
			if ((BurnDrvGetGenreFlags() & GBF_VSFIGHT) && is_neogeo_game && fba_devices[nPlayer] == RETROPAD_MODERN) {
				switch (nButton) {
					case 1:
						GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
						break;
					case 2:
						GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
						break;
					case 3:
						GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_X, description);
						break;
					case 4:
						GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
						break;
				}
			} else {
				// Handle 6 buttons fighting games with 3xPunchs and 3xKicks
				if (bStreetFighterLayout) {
					switch (nButton) {
						case 1:
							GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
							break;
						case 2:
							GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_X, description);
							break;
						case 3:
							GameInpDigital2RetroInpKey(pgi, nPlayer, (fba_devices[nPlayer] == RETROPAD_MODERN ? RETRO_DEVICE_ID_JOYPAD_R : RETRO_DEVICE_ID_JOYPAD_L), description);
							break;
						case 4:
							GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
							break;
						case 5:
							GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
							break;
						case 6:
							GameInpDigital2RetroInpKey(pgi, nPlayer, (fba_devices[nPlayer] == RETROPAD_MODERN ? RETRO_DEVICE_ID_JOYPAD_R2 : RETRO_DEVICE_ID_JOYPAD_R), description);
							break;
					}
				// Handle generic mapping of everything else
				} else {
					switch (nButton) {
						case 1:
							GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_B, description);
							break;
						case 2:
							GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_A, description);
							break;
						case 3:
							GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_Y, description);
							break;
						case 4:
							GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_X, description);
							break;
						case 5:
							GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_R, description);
							break;
						case 6:
							GameInpDigital2RetroInpKey(pgi, nPlayer, (fba_devices[nPlayer] == RETROPAD_MODERN ? RETRO_DEVICE_ID_JOYPAD_R2 : RETRO_DEVICE_ID_JOYPAD_L), description);
							break;
						case 7:
							GameInpDigital2RetroInpKey(pgi, nPlayer, (fba_devices[nPlayer] == RETROPAD_MODERN ? RETRO_DEVICE_ID_JOYPAD_L : RETRO_DEVICE_ID_JOYPAD_R2), description);
							break;
						case 8:
							GameInpDigital2RetroInpKey(pgi, nPlayer, RETRO_DEVICE_ID_JOYPAD_L2, description);
							break;
					}
				}
			}
		}
	}

	// Store the pgi that controls the reset input
	if (strcmp(szi, "reset") == 0) {
		pgi->nInput = GIT_SWITCH;
		if (!input_initialized)
			pgi->Input.Switch.nCode = (UINT16)(switch_ncode++);
		pgi_reset = pgi;
	}

	// Store the pgi that controls the diagnostic input
	if (strcmp(szi, "diag") == 0) {
		pgi->nInput = GIT_SWITCH;
		if (!input_initialized)
			pgi->Input.Switch.nCode = (UINT16)(switch_ncode++);
		pgi_diag = pgi;
	}
	return 0;
}

// Activate or deactivate macros depending of the choice the user made in core options
bool apply_macro_from_variables()
{
   bool macro_changed = false;

   struct retro_variable var = {0};

   for (int macro_idx = 0; macro_idx < macro_core_options.size(); macro_idx++)
   {
      macro_core_option *macro_option = &macro_core_options[macro_idx];

      var.key = macro_option->option_name;
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) == false)
         continue;

      for (int macro_value_idx = 0; macro_value_idx < macro_option->values.size(); macro_value_idx++)
      {
         macro_core_option_value *macro_value = &(macro_option->values[macro_value_idx]);

         if (strcasecmp(var.value, macro_value->friendly_name) != 0)
            continue;

         unsigned old_retro_device_id = keybinds[macro_option->pgi->Macro.Switch.nCode][0];

         if (macro_value->retro_device_id == old_retro_device_id)
         {
#ifdef FBA_DEBUG
            log_cb(RETRO_LOG_INFO, "Macro '%s' unchanged '%s'\n", macro_option->friendly_name, print_label(macro_value->retro_device_id));
#endif
            continue;
         }

         macro_changed = true;

         if (macro_value->retro_device_id == RETRO_DEVICE_ID_JOYPAD_EMPTY)
         {
            // deactivate the macro
            macro_option->selected_value = NULL;
            macro_option->pgi->Macro.nMode = 0;
#ifdef FBA_DEBUG
            log_cb(RETRO_LOG_INFO, "Macro '%s' disable from '%s'\n", macro_option->friendly_name, print_label(old_retro_device_id));
#endif
         }
         else
         {
            // activate the macro
            macro_option->selected_value = macro_value;
            macro_option->pgi->Macro.nMode = 1;
#ifdef FBA_DEBUG
            log_cb(RETRO_LOG_INFO, "Macro '%s' changed from '%s' to '%s'\n", macro_option->friendly_name, print_label(old_retro_device_id), print_label(macro_value->retro_device_id));
#endif
         }

         // set the retro device id for the macro
         keybinds[macro_option->pgi->Macro.Switch.nCode][0] = macro_value->retro_device_id;
      }
   }

   return macro_changed;
}

bool poll_diag_input()
{
   if (pgi_diag && diag_input)
   {
      one_diag_input_pressed = false;
      all_diag_input_pressed = true;
      
      for (int combo_idx = 0; diag_input[combo_idx] != RETRO_DEVICE_ID_JOYPAD_EMPTY; combo_idx++)
      {
         if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, diag_input[combo_idx]) == false)
            all_diag_input_pressed = false;
         else
            one_diag_input_pressed = true;
      }

      if (diag_combo_activated == false && all_diag_input_pressed)
      {
         if (diag_input_combo_start_frame == 0) // => User starts holding all the combo inputs
            diag_input_combo_start_frame = nCurrentFrame;
         else if ((nCurrentFrame - diag_input_combo_start_frame) > diag_input_hold_frame_delay) // Delays of the hold reached
            diag_combo_activated = true;
      }
      else if (one_diag_input_pressed == false)
      {
         diag_combo_activated = false;
         diag_input_combo_start_frame = 0;
      }
      
      if (diag_combo_activated)
      {
         // Cancel each input of the combo at the emulator side to not interfere when the diagnostic menu will be opened and the combo not yet released
         struct GameInp* pgi = GameInp;
         for (int combo_idx = 0; diag_input[combo_idx] != RETRO_DEVICE_ID_JOYPAD_EMPTY; combo_idx++)
         {
            for (int i = 0; i < nGameInpCount; i++, pgi++)
            {
               if (pgi->nInput == GIT_SWITCH)
               {
                  pgi->Input.nVal = 0;
                  *(pgi->Input.pVal) = pgi->Input.nVal;
               }
            }
         }

         // Activate the diagnostic key
         pgi_diag->Input.nVal = 1;
         *(pgi_diag->Input.pVal) = pgi_diag->Input.nVal;

         // Return true to stop polling game inputs while diagnostic combo inputs is pressed
         return true;
      }
   }

   // Return false to poll game inputs
   return false;
}

bool init_input()
{
	switch_ncode = 0;

	normal_input_descriptors.clear();
	for (unsigned i = 0; i < MAX_KEYBINDS; i++) {
		keybinds[i][0] = 0xff;
		keybinds[i][2] = 0;
		keybinds[i][4] = RETRO_DEVICE_JOYPAD;
	}
	for (unsigned i = 0; i < 5; i++) {
		for (unsigned j = 0; j < 8; j++) {
			axibinds[i][j][0] = 0;
			axibinds[i][j][1] = 0;
			axibinds[i][j][2] = 0;
		}
	}

	GameInpInit();
	GameInpDefault();

	init_macro_core_options();

	// Update core option for diagnostic and macro inputs
	set_environment();
	// Read the user core option values
	check_variables();
	apply_macro_from_variables();

	// Now that the macro_core_options are created and core option values are read, we can create the list of macro input_descriptors
	init_macro_input_descriptors();
	// The list of normal and macro input_descriptors are filled, we can assign all the input_descriptors to retroarch
	set_input_descriptors();

	/* serialization quirks for netplay, cps3 seems problematic, neogeo, cps1 and 2 seem to be good to go 
	uint64_t serialization_quirks = RETRO_SERIALIZATION_QUIRK_SINGLE_SESSION;
	if(!strcmp(systemname, "CPS-3"))
	environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &serialization_quirks);*/

	return 0;
}
