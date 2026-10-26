/* input.h - the GameCube controller (all four ports merged, as in
 * CubeBrowser: PAD_ScanPads, no PAD_Clamp). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { DIR_NONE = -1, DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

/* Buttons (same bits as libogc's PAD_BUTTON_*). */
#define BTN_LEFT  0x0001
#define BTN_RIGHT 0x0002
#define BTN_DOWN  0x0004
#define BTN_UP    0x0008
#define BTN_Z     0x0010
#define BTN_R     0x0020
#define BTN_L     0x0040
#define BTN_A     0x0100
#define BTN_B     0x0200
#define BTN_X     0x0400
#define BTN_Y     0x0800
#define BTN_START 0x1000

void  input_init(void);
void  input_update(void);              /* once per frame */
bool  input_pressed(uint32_t btn);     /* went down this frame */
bool  input_held(uint32_t btn);
bool  input_repeat(uint32_t btn);      /* pressed, or held long enough to repeat */
int   input_dir(void);                 /* stick or D-pad: DIR_* when pushed (with repeat) */
int   input_stick_dir(void);           /* where the stick is held now (no repeat), or DIR_NONE */
float input_stick_x(void);             /* -1..1 with dead zone */
float input_stick_y(void);             /* -1..1, up is negative */
float input_cstick_x(void);
float input_cstick_y(void);
float input_trigger_l(void);           /* 0..1 */
float input_trigger_r(void);
bool  input_any(void);                 /* anything pressed this frame */
void  input_inject(uint32_t pressed);  /* test programs: pretend buttons went down */
void  input_test_state(uint32_t held, float sx, float sy, float l, float r);   /* test programs only */
