/* logo2d.h - the CubeVerse mark, drawn flat. */
#pragma once
#include "gfx.h"

/* Draws the mark with its top-left at (x, y), h tall. */
void  logo_mark_draw(float x, float y, float h, float alpha);
float logo_mark_width(float h);
