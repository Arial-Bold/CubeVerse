/* imgcache.h - pictures from Juxt (Mii faces, community icons, drawings
 * and screenshots), fetched and decoded on a thread of their own and kept
 * as textures while they're being shown. */
#pragma once
#include <stdbool.h>
#include "gfx.h"

typedef enum {
    IMG_SMALL,     /* Mii faces and icons: up to 96x96 */
    IMG_PAINTING,  /* drawings: 320x120 */
    IMG_SHOT,      /* screenshots: up to 400x240 */
} ImgKind;

void img_init(void);
/* The picture at url, or NULL while it's on its way (asks for it). */
GfxImage *img_get(const char *url, ImgKind kind);
/* True if the picture couldn't be loaded. */
bool img_failed(const char *url);
void img_tick(void);       /* main thread, once a frame */
/* Draws the picture at url into the box (cropped to fill it), or a soft
 * placeholder while it loads. */
void img_draw_cover(const char *url, ImgKind kind, float x, float y, float w, float h, float alpha);
/* The same, fitted inside the box (whole picture shown). */
void img_draw_fit(const char *url, ImgKind kind, float x, float y, float w, float h, float alpha);
