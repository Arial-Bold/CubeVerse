/* cvui.h - CubeVerse's look, shared by its screens: the dark background
 * leaning toward the Wii's, coloured section titles with a thin rule, silver
 * tabs, white square-cornered cards (Flashcube's gold ring when the pointer
 * is on one), pills, Mii faces, wrapped text, and lists that run off the
 * bottom of the screen into a fading light line. */
#pragma once
#include <stdbool.h>
#include "gfx.h"
#include "ui.h"

/* section colours: Activity, Communities, Notifications, My page */
#define CV_GREEN   rgba(0x56, 0xC2, 0x2F, 255)
#define CV_BLUE    rgba(0x2F, 0xA6, 0xE0, 255)
#define CV_ORANGE  rgba(0xF5, 0x86, 0x2A, 255)
#define CV_PURPLE  rgba(0x7D, 0x5B, 0xD6, 255)
#define CV_T_GREEN  rgba(0x7F, 0xE0, 0x5A, 255)
#define CV_T_BLUE   rgba(0x6F, 0xC8, 0xF5, 255)
#define CV_T_ORANGE rgba(0xFF, 0xA6, 0x5A, 255)
#define CV_T_PURPLE rgba(0xB5, 0x9B, 0xFF, 255)
#define CV_INK     rgba(0x2A, 0x2F, 0x45, 255)
#define CV_INK2    rgba(0x5C, 0x63, 0x80, 255)
#define CV_YEAH    rgba(0xF0, 0x8A, 0x3C, 255)

/* the layout: content between CV_L and UI_W - CV_L, title at the top, the
 * controller hints along the bottom; lists end at CV_BOTTOM */
#define CV_L 22.0f
#define CV_TOP 18.0f
#define CV_BOTTOM 418.0f

void  cv_background(float alpha);
/* Section title in its colour, a soft right-hand text, and the rule under them. */
void  cv_title(const char *left, Color c, const char *right, float alpha);
/* Silver tabs from x at y. Returns the width used. Tab i is clickable as
 * pointer id base + i; on = the selected one. */
float cv_tabs(float x, float y, const char *const *labels, int n, int on, int base, float alpha);
/* A white card; hot = the pointer is on it (gold ring). */
void  cv_card(float x, float y, float w, float h, bool hot, float alpha);
/* A flat pill (Yeah!, Post...). Returns its width (w = 0: fit the label). */
float cv_pill(float x, float y, float w, float h, const char *label, Color bg, Color fg, bool hot, float alpha);
/* A Mii face (or picture) in a square, with a letter on a colour while it loads. */
void  cv_face(const char *url, const char *name, float x, float y, float size, float alpha);
/* Wrapped text: draws at most max_lines lines (the last ending in "..." if
 * cut). Returns the number of lines. draw = false only measures. */
int   cv_wrap(FontId f, float size, float sp, const char *s, float x, float y, float w, float line_h, int max_lines,
              Color c, bool draw);
/* Where a list is shown: everything drawn between cv_list_begin and
 * cv_list_end is clipped to x..x+w, y..bottom, fading into a light line at
 * the bottom when more continues below. */
void  cv_list_begin(float x, float y, float w, float bottom);
void  cv_list_end(bool more_below, float alpha);
/* A small message in the middle of a list area ("Loading...", errors). */
void  cv_center_note(float x, float y, float w, float h, const char *title, const char *text, bool spinner, float alpha);
/* A tiny feeling face (0..5) as in Juxt's posts. */
void  cv_feeling(int feeling, float cx, float cy, float r, Color ink, float alpha);
extern const char *const CV_FEELINGS[6];
