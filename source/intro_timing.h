/* intro_timing.h - the opening's flying pieces: where each one starts, how
 * it spins and when it sets off. Shared by the animation (splash.c) and its
 * sound (sfxsynth.c), so every click lands with its piece. */
#pragma once
#include <math.h>
#include "m3d.h"

#define INTRO_T_FLY 3.6f       /* each piece's flight */
#define INTRO_SEED 11u

typedef struct { float fx, fy, fz, rx, ry, delay; } IntroFlight;

static inline float intro_frand(unsigned *s) { *s = *s * 1103515245u + 12345u; return ((*s >> 8) & 0xFFFF) / 65535.0f; }

/* all 3 x 27 pieces (cube c, piece p at [c * 27 + p]) */
static inline void intro_flights(IntroFlight *out) {
    unsigned seed = INTRO_SEED;
    for (int c = 0; c < 3; c++)
        for (int p = 0; p < 27; p++) {
            int ix = p % 3, iy = (p / 3) % 3, iz = p / 9;
            float x = (ix - 1) * 60.0f, y = (iy - 1) * 60.0f, z = (iz - 1) * 60.0f;
            float n = sqrtf(x * x + y * y + z * z);
            if (n < 1) n = 1;
            float far = 1450 + 900 * intro_frand(&seed);
            IntroFlight *f = &out[c * 27 + p];
            f->fx = x / n * far; f->fy = y / n * far; f->fz = z / n * far;
            f->rx = (intro_frand(&seed) * 2 - 1) * 540;
            f->ry = (intro_frand(&seed) * 2 - 1) * 540;
            f->delay = 0.3f + (c == 0 ? 0.7f + 0.6f * intro_frand(&seed) : 0.6f * intro_frand(&seed));
        }
}

/* how far along its flight a piece is, lt seconds after it set off (0..1) */
static inline float intro_progress(float lt) { return ease_bezier(lt / INTRO_T_FLY, 0.15f, 0.6f, 0.25f, 1); }
