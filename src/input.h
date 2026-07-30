#pragma once

typedef enum
{
	I_KEYBOARD,
	I_MOUSE,
	I_JOYSTICK,
	I_FORCE
}ITYPE;

#define BUT_1 ( buttons[0] )
#define BUT_2 ( buttons[1] )
#define BUT_3 ( buttons[2] )
#define BUT_4 ( buttons[3] )

extern int buttons[4];
extern int control;
extern int haptic;
extern int joy_ipt_MenuNew;
// True when menus should synthesize keypresses from the controller
// (D-pad arrows, letter cycling in input fields). Mirrors joy_ipt_MenuNew
// everywhere except PSP, which runs the pointer AND the menu keys together:
// nub moves the cursor, Cross clicks, D-pad navigates fields / cycles letters.
extern int joy_menu_keys;

void IPT_LoadPrefs(void);
void IPT_GetButtons(void);
void IPT_Start(void);
void IPT_PauseControl(int flag);
void IPT_FMovePlayer(int addx, int addy);
void IPT_MovePlayer(void);
void IPT_End(void);
void IPT_Init(void);
