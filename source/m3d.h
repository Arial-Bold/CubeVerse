/* m3d.h - small 3D matrices (3 rows x 4 columns) using the same conventions
 * as CSS transforms: x right, y down, z towards the viewer, angles in degrees,
 * and A*B meaning "B first, then A". */
#pragma once
#include <math.h>
#include <string.h>

typedef struct { float m[12]; } M34;

static inline M34 m34_identity(void) {
    M34 r = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 } };
    return r;
}

static inline M34 m34_mul(M34 a, M34 b) {
    M34 r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            r.m[i * 4 + j] = a.m[i * 4] * b.m[j] + a.m[i * 4 + 1] * b.m[4 + j] + a.m[i * 4 + 2] * b.m[8 + j];
        r.m[i * 4 + 3] = a.m[i * 4] * b.m[3] + a.m[i * 4 + 1] * b.m[7] + a.m[i * 4 + 2] * b.m[11] + a.m[i * 4 + 3];
    }
    return r;
}

static inline M34 m34_translate(float x, float y, float z) {
    M34 r = m34_identity();
    r.m[3] = x; r.m[7] = y; r.m[11] = z;
    return r;
}

static inline M34 m34_scale(float s) {
    M34 r = { { s, 0, 0, 0, 0, s, 0, 0, 0, 0, s, 0 } };
    return r;
}

static inline M34 m34_scale3(float sx, float sy, float sz) {
    M34 r = { { sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0 } };
    return r;
}

static inline M34 m34_rx(float deg) {
    float a = deg * (float)M_PI / 180.0f, c = cosf(a), s = sinf(a);
    M34 r = { { 1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0 } };
    return r;
}

static inline M34 m34_ry(float deg) {
    float a = deg * (float)M_PI / 180.0f, c = cosf(a), s = sinf(a);
    M34 r = { { c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0 } };
    return r;
}

static inline M34 m34_rz(float deg) {
    float a = deg * (float)M_PI / 180.0f, c = cosf(a), s = sinf(a);
    M34 r = { { c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0 } };
    return r;
}

/* Where the point (x, y, 0) ends up. */
static inline void m34_apply(const M34 *a, float x, float y, float *X, float *Y, float *Z) {
    *X = a->m[0] * x + a->m[1] * y + a->m[3];
    *Y = a->m[4] * x + a->m[5] * y + a->m[7];
    *Z = a->m[8] * x + a->m[9] * y + a->m[11];
}

/* The plane's facing direction's z (> 0: facing the viewer). */
static inline float m34_facing(const M34 *a) {
    /* normal = column x cross column y */
    return a->m[0] * a->m[5] - a->m[4] * a->m[1];
}

/* Easing like CSS cubic-bezier(x1, y1, x2, y2). */
static inline float ease_bezier(float t, float x1, float y1, float x2, float y2) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    float u = t;
    for (int i = 0; i < 8; i++) {   /* solve x(u) = t */
        float iu = 1 - u;
        float x = 3 * iu * iu * u * x1 + 3 * iu * u * u * x2 + u * u * u;
        float dx = 3 * iu * iu * x1 + 6 * iu * u * (x2 - x1) + 3 * u * u * (1 - x2);
        if (fabsf(dx) < 1e-5f) break;
        u -= (x - t) / dx;
        if (u < 0) u = 0;
        if (u > 1) u = 1;
    }
    float iu = 1 - u;
    return 3 * iu * iu * u * y1 + 3 * iu * u * u * y2 + u * u * u;
}

static inline float ease_inout(float t) { return ease_bezier(t, 0.42f, 0, 0.58f, 1); }
static inline float ease_out(float t) { return ease_bezier(t, 0, 0, 0.58f, 1); }
static inline float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
/* 0 before a, 1 after b, linear between. */
static inline float ramp(float t, float a, float b) { return clamp01((t - a) / (b - a)); }
