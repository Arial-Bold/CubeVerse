/* logo2d.c - the CubeVerse mark (three tumbling cubes with faces, a white
 * sticker outline round a green one) drawn flat, for the header and the
 * sign-in screen. Same shapes as the design's "A2 Tumble" mark. */
#include "logo2d.h"
#include "m3d.h"
#include <math.h>

typedef struct { float x, y, s, d, rot, px, py; int face; } LC;
/* in the design's 290 x 170 box (its viewBox started at 20, 20) */
static const LC CUBES[3] = {
    { 50, 98, 52, 24, -14, 76, 124, 2 },    /* left: a wink */
    { 208, 98, 52, 24, 12, 234, 124, 3 },   /* right: open mouth */
    { 118, 66, 70, 32, 0, 0, 0, 1 },        /* middle: happy */
};

static float OX, OY, SC;
static float rc, rs, rpx, rpy;   /* the current cube's turn */

static void P(float x, float y, float *X, float *Y) {
    float dx = x - rpx, dy = y - rpy;
    float xx = rpx + dx * rc - dy * rs, yy = rpy + dx * rs + dy * rc;
    *X = OX + (xx - 20) * SC;
    *Y = OY + (yy - 20) * SC;
}

static void set_cube(const LC *c) {
    float a = c->rot * (float)M_PI / 180;
    rc = cosf(a); rs = sinf(a); rpx = c->px; rpy = c->py;
}

static void poly(const float *pts, int n, Color col) {
    float X0, Y0;
    P(pts[0], pts[1], &X0, &Y0);
    for (int i = 1; i + 1 < n; i++) {
        float X1, Y1, X2, Y2;
        P(pts[i * 2], pts[i * 2 + 1], &X1, &Y1);
        P(pts[i * 2 + 2], pts[i * 2 + 3], &X2, &Y2);
        gfx_tri(X0, Y0, X1, Y1, X2, Y2, col);
    }
}

/* the outline of a shape grown by r (round joins): circles, edges, fill */
static void grown(const float *pts, int n, float r, Color col) {
    for (int i = 0; i < n; i++) {
        float X, Y, X2, Y2;
        P(pts[i * 2], pts[i * 2 + 1], &X, &Y);
        int j = (i + 1) % n;
        P(pts[j * 2], pts[j * 2 + 1], &X2, &Y2);
        gfx_circle(X, Y, r * SC, col);
        gfx_line(X, Y, X2, Y2, 2 * r * SC, col);
    }
    poly(pts, n, col);
}

static void silhouette(const LC *c, float *o) {
    float x = c->x, y = c->y, s = c->s, d = c->d, dy = d * 0.8f;
    float p[12] = { x, y, x + d, y - dy, x + s + d, y - dy, x + s + d, y + s - dy, x + s, y + s, x, y + s };
    for (int i = 0; i < 12; i++) o[i] = p[i];
}

static void face(const LC *c, Color ink) {
    float x = c->x, y = c->y, s = c->s;
    float cx = x + s / 2, ey = y + s * 0.38f, r = s * 0.075f, dx = s * 0.19f, w = s * 0.08f, my = y + s * 0.6f;
    float X, Y, X2, Y2;
    /* eyes */
    if (c->face == 1) {   /* happy: little arches */
        for (int k = -1; k <= 1; k += 2) {
            P(cx + k * dx, ey + r * 0.9f, &X, &Y);
            gfx_ring(X, Y, r * 1.5f * SC, w * SC, (float)M_PI + 0.5f + rc * 0, 2 * (float)M_PI - 0.5f, ink);
        }
    } else {
        P(cx - dx, ey, &X, &Y);
        gfx_circle(X, Y, r * SC, ink);
        if (c->face == 2) {
            P(cx + dx - r * 1.4f, ey, &X, &Y);
            P(cx + dx + r * 1.4f, ey, &X2, &Y2);
            gfx_line(X, Y, X2, Y2, w * SC, ink);
        } else {
            P(cx + dx, ey, &X, &Y);
            gfx_circle(X, Y, r * SC, ink);
        }
    }
    /* mouth */
    if (c->face == 3) {
        float q[8] = { cx - s * 0.16f, my, cx + s * 0.16f, my, cx + s * 0.08f, my + s * 0.12f, cx - s * 0.08f, my + s * 0.12f };
        poly(q, 4, ink);
    } else {
        float mw = c->face == 1 ? 0.2f : 0.18f;
        P(cx, my - s * mw * 0.6f, &X, &Y);
        gfx_ring(X, Y, s * mw * SC, w * SC, 0.45f + c->rot * (float)M_PI / 180, (float)M_PI - 0.45f + c->rot * (float)M_PI / 180, ink);
    }
}

void logo_mark_draw(float x, float y, float h, float al) {
    if (al <= 0) return;
    SC = h / 170.0f;
    OX = x;
    OY = y;
    u8 A = (u8)(255 * al);
    float pts[12];
    for (int pass = 0; pass < 2; pass++) {
        Color col = pass ? rgba(0x56, 0xC2, 0x2F, A) : rgba(255, 255, 255, A);
        for (int i = 0; i < 3; i++) {
            set_cube(&CUBES[i]);
            silhouette(&CUBES[i], pts);
            grown(pts, 6, pass ? 13 : 20, col);
        }
    }
    for (int i = 0; i < 3; i++) {
        const LC *c = &CUBES[i];
        set_cube(c);
        float x0 = c->x, y0 = c->y, s = c->s, d = c->d, dy = d * 0.8f;
        float top[8] = { x0, y0, x0 + d, y0 - dy, x0 + s + d, y0 - dy, x0 + s, y0 };
        float right[8] = { x0 + s, y0, x0 + s + d, y0 - dy, x0 + s + d, y0 + s - dy, x0 + s, y0 + s };
        float front[8] = { x0, y0, x0 + s, y0, x0 + s, y0 + s, x0, y0 + s };
        poly(top, 4, rgba(0xF1, 0xFB, 0xEC, A));
        poly(right, 4, rgba(0xCD, 0xEB, 0xBE, A));
        poly(front, 4, rgba(255, 255, 255, A));
        face(c, rgba(0x3A, 0x9A, 0x18, A));
    }
}

float logo_mark_width(float h) { return h * 290.0f / 170.0f; }
