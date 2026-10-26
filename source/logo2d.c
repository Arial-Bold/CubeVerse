/* logo2d.c - the CubeVerse mark (the design's "A2 Tumble": three tumbling
 * cubes with faces in a white sticker outline round a green one) for the
 * header and the sign-in screen. It's a picture (textures.h) rendered from
 * the design at about five times the size it's shown and shrunk, so the
 * faces and outlines stay smooth and clear. */
#include "logo2d.h"
#include "textures.h"
#include <malloc.h>
#include <string.h>

static GfxImage tex;
static bool tried, ok;

#define IW (LOGO_W - 2 * LOGO_PAD)   /* the picture inside the see-through border */
#define IH (LOGO_H - 2 * LOGO_PAD)

float logo_mark_width(float h) { return h * IW / IH; }

void logo_mark_draw(float x, float y, float h, float al) {
    if (al <= 0) return;
    if (!tried) {
        tried = true;
        void *d = memalign(32, sizeof(LOGO_TEX));
        if (d) {
            memcpy(d, LOGO_TEX, sizeof(LOGO_TEX));
            ok = gfx_image_init(&tex, d, LOGO_W, LOGO_H, LOGO_TW, LOGO_TH, GFX_RGB5A3);
        }
    }
    if (!ok) return;
    /* the quad takes in the border too, so the edges fade out cleanly */
    float k = h / IH;
    gfx_image_draw(&tex, x - LOGO_PAD * k, y - LOGO_PAD * k, LOGO_W * k, LOGO_H * k, rgba(255, 255, 255, (u8)(255 * al)));
}
