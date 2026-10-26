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

float logo_mark_width(float h) { return h * LOGO_W / LOGO_H; }

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
    if (ok) gfx_image_draw(&tex, x, y, logo_mark_width(h), h, rgba(255, 255, 255, (u8)(255 * al)));
}
