/* pixels.h - the GPU's tiled texture layout. RGB565 and RGB5A3 textures are
 * stored in 4x4-pixel tiles of 32 bytes, row of tiles after row of tiles,
 * so a picture is written straight into a form the GPU can read. */
#pragma once
#include <stdint.h>

static inline uint32_t tex_index(int tex_w, int x, int y) {
    return (uint32_t)((((y >> 2) * (tex_w >> 2) + (x >> 2)) << 4) + ((y & 3) << 2) + (x & 3));
}

static inline uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline void unrgb565(uint16_t c, int *r, int *g, int *b) {
    *r = ((c >> 11) & 31) * 255 / 31;
    *g = ((c >> 5) & 63) * 255 / 63;
    *b = (c & 31) * 255 / 31;
}

/* Rounds a size up to the 4-pixel tile grid. */
static inline int tex_round(int v) { return (v + 3) & ~3; }

