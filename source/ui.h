/* ui.h - the look of the GameCube system menu, shared by every screen:
 * the fine-grid background, teal glass panels with silver frames, blue
 * titles, highlighted rows, radio buttons, silver/gold buttons, the purple
 * description bar and the controller hints along the bottom.
 *
 * Screens lay out across the whole width (UI_W: 640 in 4:3, 854 in 16:9). */
#pragma once
#include <stdbool.h>
#include "gfx.h"

extern float UI_X;            /* left edge of the layout (0) */
extern float UI_W;            /* layout width: 640 in 4:3, 854 in 16:9 */
void ui_frame_begin(void);    /* updates UI_W */

/* Colours used throughout. */
#define COL_TITLE  rgba(0x73, 0x92, 0xff, 255)
#define COL_TEXT   rgba(0xee, 0xf1, 0xf6, 255)
#define COL_SOFT   rgba(0xc9, 0xd2, 0xee, 255)
#define COL_DESC2  rgba(0xc9, 0xc6, 0xff, 255)
#define COL_GOLD   rgba(0xe8, 0xcf, 0x4a, 255)

/* Text in a CSS-style line box: vertically centred in [top, top+h). */
float ui_text(FontId f, float size, float spacing, float x, float top, float h, Color c, const char *s);
float ui_text_right(FontId f, float size, float spacing, float right, float top, float h, Color c, const char *s);
float ui_text_center(FontId f, float size, float spacing, float cx, float top, float h, Color c, const char *s);
/* Text with a soft glow around it (cube labels). */
void  ui_text_glow(FontId f, float size, float spacing, float x, float top, float h, Color c, Color glow, float glow_px,
                   const char *s);

void ui_background_grid(float alpha);          /* the same background as the cube screen, fading in */
void ui_background_cube(void);                 /* the cube screen's darker 24px grid and vignette */
void ui_background_cube_a(float alpha);
/* Text that scrolls back and forth when it's wider than w (clipped to the box).
 * Returns the width used. Call ui_text_scroll_tick() once per frame. */
float ui_text_fit_scroll(FontId f, float size, float spacing, float x, float top, float w, float h, Color c, const char *s);
void  ui_text_scroll_tick(void);
void ui_panel(float x, float y, float w, float h, float alpha);
void ui_ptitle(float px, float py, float pw, const char *left, const char *right, float alpha);
void ui_row(float x, float y, float w, float h, bool on, float alpha);
void ui_radio(float x, float cy, bool on, float alpha);
void ui_button(float x, float y, float w, float h, const char *label, bool on, float alpha);
void ui_desc(float x, float y, float w, float h, float alpha);
/* Left and right text in a description bar (the left part scrolls if long). */
void ui_desc_texts(float x, float y, float w, float h, const char *left, const char *right, float alpha);
void ui_thumb_frame(float x, float y, float w, float h, float alpha);
void ui_gold_ring(float x, float y, float w, float h, float pulse, float alpha);

/* Controller hints. A hint is "S:Select", "LR:Tab", "Y:Slideshow", "B:Back",
 * "A:Confirm", "DP:Next" (D-pad), "Z:..." etc. The stick icon's knob moves in
 * the direction given (DIR_* or -1, from input). */
void ui_hints(const char *const *hints, int n, float alpha, int stick_dir);
#define UI_HINT_Y (480 - 30)   /* the centre line of the hints, clear of the panels */
/* The same, centred as a group (cube screens). */
void ui_hints_center(const char *const *hints, int n, float y, float gap, float alpha, int stick_dir, bool big);
float ui_hint_width(const char *hint, bool big);

/* A short message box in the middle of the screen. */
void ui_message(const char *title, const char *text, float alpha);
/* A spinning "busy" mark. */
void ui_spinner(float cx, float cy, float r, float t, float alpha);

/* The swirl when a section opens (from the Memory Card screen): items burst
 * out of the middle, sweep round in an arc and settle into place.
 * Call before drawing item i of n (x,y,w,h = where it ends up); draw it
 * normally, then call ui_swirl_end(). Returns the item's opacity. */
float ui_swirl_begin(int i, int n, float x, float y, float w, float h, float t, float radius, float spin);
void  ui_swirl_end(void);
/* When the swirl of n items is complete. */
float ui_swirl_duration(int n);

/* Time since start (seconds), set by main each frame. */
extern float g_time;
extern float g_dt;
