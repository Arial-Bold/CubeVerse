/* pointer.h - CubeVerse's hand pointer, moved with the Control Stick like
 * a mouse (as in CubeBrowser). Screens mark what can be pointed at while
 * they draw (ptr_hot); the next frame's update asks what was clicked.
 * The D-pad hops the hand between those things, for quick moves. */
#pragma once
#include <stdbool.h>

void  ptr_init(void);
void  ptr_update(void);        /* once a frame, before the screen's update */
void  ptr_frame_begin(void);   /* before the screen draws */
void  ptr_draw(float alpha);   /* after the screen draws */
/* For this frame, show the drawing pen (a ring of this radius) instead of the hand. */
void  ptr_pen(float radius);
/* Marks a clickable area with an id (>= 0). True if the hand is on it. */
bool  ptr_hot(int id, float x, float y, float w, float h);
/* The id under the hand (from the last frame), or -1. */
int   ptr_over(void);
/* The id A was pressed on this frame, or -1. */
int   ptr_clicked(void);
/* The hand's tip. */
float ptr_x(void);
float ptr_y(void);
void  ptr_set(float x, float y);
void  ptr_show(bool on);
bool  ptr_shown(void);
/* While A is held: true, and where (for the drawing pad). */
bool  ptr_held(void);
/* Keeps the hand inside a box (drawing); w = 0 to free it. */
void  ptr_confine(float x, float y, float w, float h);
/* Scrolling a list with the C-stick: how far to scroll this frame. (The
 * area is no longer used; the pointer never scrolls the list.) */
float ptr_scroll(float top, float bottom);
