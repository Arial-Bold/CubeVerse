/* vcube.c - see vcube.h. From Flashcube's cube.c: every face is a flat
 * 180x180 plane placed in 3D (gfx_begin_3d), drawn back to front so the
 * glass blends correctly; the rounded edges are lit per vertex. */
#include "vcube.h"
#include "icons.h"
#include "ui.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CUBE 180.0f
#define HALF 90.0f
typedef struct { float r, g, b, a; } RGBA;

static RGBA over(RGBA top, RGBA bot) {
    RGBA o;
    o.a = top.a + bot.a * (1 - top.a);
    if (o.a < 1e-4f) { o.r = o.g = o.b = 0; return o; }
    o.r = (top.r * top.a + bot.r * bot.a * (1 - top.a)) / o.a;
    o.g = (top.g * top.a + bot.g * bot.a * (1 - top.a)) / o.a;
    o.b = (top.b * top.a + bot.b * bot.a * (1 - top.a)) / o.a;
    return o;
}

static RGBA radial(float u, float v, float cx, float cy, float stop, float r, float g, float b, float a) {
    float d = sqrtf((u - cx) * (u - cx) + (v - cy) * (v - cy));
    float far = 0;   /* farthest corner */
    for (int k = 0; k < 4; k++) {
        float qx = (k & 1) ? 1 : 0, qy = (k & 2) ? 1 : 0;
        float e = sqrtf((qx - cx) * (qx - cx) + (qy - cy) * (qy - cy));
        if (e > far) far = e;
    }
    float t = d / (far * stop);
    RGBA o = { r, g, b, a * (1 - clamp01(t)) };
    return o;
}

/* colour of glass kind at (u,v) in 0..1 */
static float tint_r = 70, tint_g = 140, tint_b = 120;

static RGBA glass_at(int kind, float u, float v) {
    if (kind == 0 || kind == 2) {
        float k = kind == 0 ? 0.26f : 0.20f;
        RGBA l1 = radial(u, v, 0.78f, 0.22f, 0.55f, tint_r, tint_g, tint_b, k);
        RGBA l2 = radial(u, v, 0.22f, 0.82f, 0.58f, 40, 62, 150, k);
        RGBA l3;
        if (kind == 0) {
            float d = sqrtf((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) / 0.7071f;
            d = clamp01(d);
            l3.r = lerpf(18, 6, d); l3.g = lerpf(30, 10, d); l3.b = lerpf(42, 16, d); l3.a = lerpf(0.66f, 0.72f, d);
        } else {
            l3.r = 10; l3.g = 16; l3.b = 24; l3.a = 0.34f;
        }
        return over(l1, over(l2, l3));
    }
    /* the sides: linear-gradient(135deg, ...) */
    float p = ((u - 0.5f) * 0.7071f + (v - 0.5f) * 0.7071f) / 1.4142f + 0.5f;
    RGBA a = { 120, 160, 190, 0.10f }, b = { 40, 60, 90, 0.06f }, c = { 140, 110, 200, 0.10f };
    RGBA o;
    if (p < 0.5f) {
        float t = clamp01(p / 0.5f);
        o.r = lerpf(a.r, b.r, t); o.g = lerpf(a.g, b.g, t); o.b = lerpf(a.b, b.b, t); o.a = lerpf(a.a, b.a, t);
    } else {
        float t = clamp01((p - 0.5f) / 0.5f);
        o.r = lerpf(b.r, c.r, t); o.g = lerpf(b.g, c.g, t); o.b = lerpf(b.b, c.b, t); o.a = lerpf(b.a, c.a, t);
    }
    return o;
}

#define GN 6
static Color glass_cache[3][(GN + 1) * (GN + 1)];
static bool glass_ready;
static Color glass_tint;

static void glass_prepare(void) {
    for (int k = 0; k < 3; k++)
        for (int j = 0; j <= GN; j++)
            for (int i = 0; i <= GN; i++) {
                RGBA c = glass_at(k, (float)i / GN, (float)j / GN);
                glass_cache[k][j * (GN + 1) + i] = rgba((u8)c.r, (u8)c.g, (u8)c.b, (u8)(clamp01(c.a) * 255));
            }
    glass_ready = true;
}

/* Rounded-square outline points (clockwise), corner radius r, inset by d. */
static int perimeter(float size, float r, float d, float *px, float *py) {
    int n = 0;
    float x0 = d, y0 = d, x1 = size - d, y1 = size - d;
    r -= d;
    if (r < 0.5f) r = 0.5f;
    const float cx[4] = { x1 - r, x1 - r, x0 + r, x0 + r }, cy[4] = { y0 + r, y1 - r, y1 - r, y0 + r };
    for (int c = 0; c < 4; c++) {
        float a0 = (-90.0f + 90.0f * c) * (float)M_PI / 180.0f;
        for (int i = 0; i <= 4; i++) {
            float a = a0 + (float)M_PI * 0.5f * i / 4;
            px[n] = cx[c] + cosf(a) * r;
            py[n] = cy[c] + sinf(a) * r;
            n++;
        }
        /* points along the straight edge after this corner */
        float ex = px[n - 1], ey = py[n - 1];
        float nx = cx[(c + 1) % 4] + cosf(a0 + (float)M_PI * 0.5f) * r, ny = cy[(c + 1) % 4] + sinf(a0 + (float)M_PI * 0.5f) * r;
        for (int k = 1; k < 6; k++) {
            px[n] = ex + (nx - ex) * k / 6;
            py[n] = ey + (ny - ey) * k / 6;
            n++;
        }
    }
    return n;
}

static Color conic(float x, float y, float size, float a) {
    /* conic-gradient(from 20deg, green, blue, purple, green) */
    float ang = atan2f(x - size / 2, -(y - size / 2)) * 180.0f / (float)M_PI;   /* 0 = up, clockwise */
    float f = fmodf(ang - 20 + 720, 360) / 360.0f;
    static const float C[4][3] = { { 120, 255, 170 }, { 110, 170, 255 }, { 200, 120, 255 }, { 120, 255, 170 } };
    float s = f * 3;
    int i = (int)s;
    if (i > 2) i = 2;
    float t = s - i;
    return rgba((u8)lerpf(C[i][0], C[i + 1][0], t), (u8)lerpf(C[i][1], C[i + 1][1], t), (u8)lerpf(C[i][2], C[i + 1][2], t),
                (u8)(255 * 0.9f * a));
}

/* A band of colour following the outline between insets d0 and d1. */
static void edge_band(float size, float r, float d0, float d1, bool rainbow, Color c) {
    float ox[64], oy[64], ix[64], iy[64];
    int n = perimeter(size, r, d0, ox, oy);
    perimeter(size, r, d1, ix, iy);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float q[8] = { ox[i], oy[i], ox[j], oy[j], ix[j], iy[j], ix[i], iy[i] };
        if (rainbow) {
            Color cc[4] = { conic(ox[i], oy[i], size, c.a / 255.0f), conic(ox[j], oy[j], size, c.a / 255.0f),
                            conic(ix[j], iy[j], size, c.a / 255.0f), conic(ix[i], iy[i], size, c.a / 255.0f) };
            gfx_quad_colors(q, cc);
        } else {
            gfx_quad(q, c);
        }
    }
}

/* Inner glow (box-shadow: inset 0 0 blur colour), as fading bands. */
static void inner_glow(float size, float r, float blur, Color c) {
    if (blur <= 0 || !c.a) return;
    int steps = blur > 8 ? 4 : 2;
    for (int i = 0; i < steps; i++) {
        float d0 = blur * i / steps, d1 = blur * (i + 1) / steps;
        float k = 1.0f - (i + 0.5f) / steps;
        edge_band(size, r, d0, d1, false, color_alpha(c, k * k * 0.9f));
    }
}

void cube_glass(int kind, float size, float radius, float alpha, bool rim) {
    if (!glass_ready) glass_prepare();
    if (alpha <= 0) return;
    float xy[(GN + 1) * (GN + 1) * 2];
    Color col[(GN + 1) * (GN + 1)];
    for (int j = 0; j <= GN; j++)
        for (int i = 0; i <= GN; i++) {
            int k = j * (GN + 1) + i;
            xy[k * 2] = size * i / GN;
            xy[k * 2 + 1] = size * j / GN;
            col[k] = color_alpha(glass_cache[kind][k], alpha);
        }
    (void)radius;
    gfx_mesh(GN, GN, xy, col);
    if (!rim) return;
    float s = size / CUBE;
    if (kind == 1) {
        inner_glow(size, radius, 18 * s, rgba(120, 160, 200, (u8)(0.10f * 255 * alpha)));
        edge_band(size, radius, 0, 1.0f * s, false, rgba(220, 230, 250, (u8)(0.30f * 255 * alpha)));
    } else {
        inner_glow(size, radius, 12 * s, rgba(170, 200, 235, (u8)(0.16f * 255 * alpha)));
        inner_glow(size, radius, 3 * s, rgba(220, 230, 255, (u8)(0.18f * 255 * alpha)));
        edge_band(size, radius, 0, 1.0f * s, false, rgba(225, 232, 245, (u8)(0.34f * 255 * alpha)));
    }
    edge_band(size, radius, 0, 1.5f * s, true, rgba(255, 255, 255, (u8)(0.4f * 255 * alpha)));
}

/* --------------------------------------------------------------- content */

typedef struct { Color hi, mid, lo, shadow, glow; } BlockColors;
/* Activity green, Communities blue, Notifications orange, My page purple */
static const BlockColors block_colors[VC_ICON_COUNT] = {
    { { 0xe4, 0xff, 0xe9, 255 }, { 0x5f, 0xe3, 0x8a, 255 }, { 0x22, 0xa0, 0x4e, 255 }, { 0x15, 0x7a, 0x38, 255 }, { 90, 230, 140, 180 } },
    { { 0xdf, 0xf4, 0xff, 255 }, { 0x64, 0xc4, 0xff, 255 }, { 0x2b, 0x86, 0xd8, 255 }, { 0x1d, 0x5a, 0xa6, 255 }, { 90, 190, 255, 180 } },
    { { 0xff, 0xf0, 0xd6, 255 }, { 0xff, 0xb0, 0x4a, 255 }, { 0xd0, 0x74, 0x1a, 255 }, { 0xa4, 0x52, 0x0e, 255 }, { 255, 170, 70, 180 } },
    { { 0xef, 0xe8, 0xff, 255 }, { 0xad, 0x94, 0xff, 255 }, { 0x6f, 0x4f, 0xd6, 255 }, { 0x4b, 0x32, 0xa8, 255 }, { 170, 140, 255, 180 } },
};

#define NB(a) (int)(sizeof(a) / sizeof(a[0]))
static const IconBlock *icon_of(int icon, int *n) {
    switch (icon) {
    case VC_ICON_COMMUNITIES: *n = NB(icon_communities); return icon_communities;
    case VC_ICON_NOTIFICATIONS: *n = NB(icon_notifications); return icon_notifications;
    case VC_ICON_MYPAGE: *n = NB(icon_mypage); return icon_mypage;
    default: *n = NB(icon_activity); return icon_activity;
    }
}

static float morph_at(float t) {
    t = fmodf(t, 6.0f);
    if (t < 1.2f) return 0;
    if (t < 2.0f) return ease_bezier((t - 1.2f) / 0.8f, 0.45f, 0, 0.25f, 1);
    if (t < 4.4f) return 1;
    if (t < 5.2f) return 1 - ease_bezier((t - 4.4f) / 0.8f, 0.45f, 0, 0.25f, 1);
    return 0;
}

static void draw_icon(int side, float a, float t, float collapse) {
    int n;
    const IconBlock *b = icon_of(side, &n);
    const BlockColors *bc = &block_colors[side];
    for (int i = 0; i < n; i++) {
        float m = morph_at(t - (i % 14) * 0.025f);
        float x = lerpf(b[i].ax, b[i].bx, m), y = lerpf(b[i].ay, b[i].by, m), s = ICON_BLOCK;
        float ba = a;
        if (collapse > 0) {
            float c = ease_bezier(clamp01(collapse * 1.15f - (i % 9) * 0.015f), 0.6f, 0, 0.4f, 1);
            x = lerpf(x, 48, c); y = lerpf(y, 36, c);
            s = lerpf(ICON_BLOCK, 1.4f, c);
            ba *= 1 - c;
        }
        if (ba <= 0.01f) continue;
        x += 38 + (ICON_BLOCK - s) / 2; y += 46 + (ICON_BLOCK - s) / 2;
        gfx_rrect(x - 2.5f, y - 2.5f, s + 5, s + 5, 3, color_alpha(bc->glow, ba * 0.35f));
        gfx_rect(x + 1.5f, y + 1.5f, s, s, color_alpha(bc->shadow, ba));
        float q[8] = { x, y, x + s, y, x + s, y + s, x, y + s };
        Color c4[4] = { color_alpha(bc->hi, ba), color_alpha(bc->mid, ba), color_alpha(bc->lo, ba), color_alpha(bc->mid, ba) };
        gfx_quad_colors(q, c4);
    }
}

/* ------------------------------------------------------------- the cube */

/* idle sway (degrees): the design's keyframes, every 2 s over 8 s, joined
 * by a smooth looping curve (Catmull-Rom), so the cube never comes to a
 * stop between them, it just keeps drifting gently */
static void sway(float t, float *x, float *y, float *z) {
    static const float K[4][3] = { { 0, 0, 0 }, { 5, 8, -2 }, { 2, 18, 2 }, { -2, 10, -1 } };
    float p = fmodf(t, 8.0f) / 2.0f;
    int i = (int)p;
    if (i > 3) i = 3;
    float u = p - i, u2 = u * u, u3 = u2 * u;
    const float *p0 = K[(i + 3) % 4], *p1 = K[i], *p2 = K[(i + 1) % 4], *p3 = K[(i + 2) % 4];
    float o[3];
    for (int k = 0; k < 3; k++)
        o[k] = 0.5f * (2 * p1[k] + (-p0[k] + p2[k]) * u + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * u2 +
                       (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * u3);
    *x = o[0];
    *y = o[1];
    *z = o[2];
}

/* face transforms: front, right, back, left, top, bottom */
static M34 face_rot(int f) {
    switch (f) {
    case 1: return m34_ry(90);
    case 2: return m34_ry(180);
    case 3: return m34_ry(-90);
    case 4: return m34_rx(90);
    case 5: return m34_rx(-90);
    default: return m34_identity();
    }
}

/* The cube's edges are rounded like a block of thick glass. Each face owns
 * the half of the rounding next to it: along each edge a strip curving from
 * the face (0 degrees) to the diagonal (45), and at each corner a patch of
 * the sphere. They're lit per vertex: a soft light from the top left and
 * in front, a sharp highlight, brighter towards grazing angles (as glass
 * reflects more at a slant), and tinted green, blue or purple by which way
 * they face, like light split through the glass. */

#define BEV 9.0f    /* how far the rounding reaches in from each edge */

typedef struct { float x, y, z; } V3;

static V3 xf_point(const M34 *m, V3 p) {
    V3 o = { m->m[0] * p.x + m->m[1] * p.y + m->m[2] * p.z + m->m[3], m->m[4] * p.x + m->m[5] * p.y + m->m[6] * p.z + m->m[7],
             m->m[8] * p.x + m->m[9] * p.y + m->m[10] * p.z + m->m[11] };
    return o;
}
static V3 xf_normal(const M34 *m, V3 n) {
    V3 o = { m->m[0] * n.x + m->m[1] * n.y + m->m[2] * n.z, m->m[4] * n.x + m->m[5] * n.y + m->m[6] * n.z,
             m->m[8] * n.x + m->m[9] * n.y + m->m[10] * n.z };
    float l = sqrtf(o.x * o.x + o.y * o.y + o.z * o.z);
    if (l > 1e-6f) { o.x /= l; o.y /= l; o.z /= l; }
    return o;
}

/* light from the top left, in front (y is down) */
#define LX -0.42f
#define LY -0.70f
#define LZ 0.58f

static Color bevel_light(V3 n, float alpha) {
    float hx = LX, hy = LY, hz = LZ + 1;   /* half-way between the light and the eye */
    float hl = sqrtf(hx * hx + hy * hy + hz * hz);
    float ndh = (n.x * hx + n.y * hy + n.z * hz) / hl;
    if (ndh < 0) ndh = 0;
    float sp = ndh * ndh; sp *= sp; sp *= sp; sp *= sp; sp *= sp;   /* ^32: a tight highlight */
    float dif = n.x * LX + n.y * LY + n.z * LZ;
    if (dif < 0) dif = 0;
    float fr = 1 - fabsf(n.z);
    fr *= fr;
    /* colour split by direction: green, blue, purple around the rim */
    float ang = atan2f(n.y, n.x) / (2 * (float)M_PI) + 0.5f;   /* 0..1 */
    static const float C[4][3] = { { 110, 240, 190 }, { 110, 170, 255 }, { 190, 120, 255 }, { 110, 240, 190 } };
    float f = ang * 3;
    int i = (int)f;
    if (i > 2) i = 2;
    float t = f - i;
    float r = lerpf(C[i][0], C[i + 1][0], t), g = lerpf(C[i][1], C[i + 1][1], t), b = lerpf(C[i][2], C[i + 1][2], t);
    r = lerpf(r, 200, 0.45f); g = lerpf(g, 212, 0.45f); b = lerpf(b, 232, 0.45f);   /* mostly silvery glass */
    float k = 0.40f + 0.55f * dif;
    r = r * k + 255 * sp; g = g * k + 255 * sp; b = b * k + 255 * sp;
    float a = 0.16f + 0.30f * fr + 0.28f * dif + 0.75f * sp;
    if (n.z < 0) a *= 0.45f;   /* seen through the glass */
    a *= alpha;
    return rgba((u8)fminf(r, 255), (u8)fminf(g, 255), (u8)fminf(b, 255), (u8)(clamp01(a) * 255));
}

/* rotates a point of the face (and its normal) a quarter turn k times about the middle */
static V3 quarter(V3 p, int k) {
    for (int i = 0; i < k; i++) {
        float x = p.x - HALF, y = p.y - HALF;
        p.x = HALF - y;
        p.y = HALF + x;
    }
    return p;
}
static V3 quarter_n(V3 n, int k) {
    for (int i = 0; i < k; i++) { float x = n.x; n.x = -n.y; n.y = x; }
    return n;
}

typedef struct { V3 p, n; } BVert;

static void bevel_tri(const M34 *m, BVert a, BVert b, BVert c, float alpha) {
    V3 pa = xf_point(m, a.p), pb = xf_point(m, b.p), pc = xf_point(m, c.p);
    float xyz[9] = { pa.x, pa.y, pa.z, pb.x, pb.y, pb.z, pc.x, pc.y, pc.z };
    Color col[3] = { bevel_light(xf_normal(m, a.n), alpha), bevel_light(xf_normal(m, b.n), alpha),
                     bevel_light(xf_normal(m, c.n), alpha) };
    gfx_tri3d(xyz, col);
}

/* a point on the rounding along the top edge (t along it, phi 0..45 degrees) */
static BVert edge_vert(float t, float phi, int k) {
    float s = sinf(phi), c = cosf(phi);
    BVert v;
    v.p = quarter((V3){ t, BEV - BEV * s, -BEV + BEV * c }, k);
    v.n = quarter_n((V3){ 0, -s, c }, k);
    return v;
}

/* a point on the top-left corner's rounding (a, b: 0..45 degrees towards the left and top edges) */
static BVert corner_vert(float a, float b, int k) {
    V3 d = { -tanf(a), -tanf(b), 1 };
    float l = sqrtf(d.x * d.x + d.y * d.y + 1);
    d.x /= l; d.y /= l; d.z /= l;
    BVert v;
    v.p = quarter((V3){ BEV + BEV * d.x, BEV + BEV * d.y, -BEV + BEV * d.z }, k);
    v.n = quarter_n(d, k);
    return v;
}

static void face_bevel(const M34 *m, float alpha) {
    const float Q = (float)M_PI / 4;
    enum { SEG = 2 };
    for (int k = 0; k < 4; k++) {
        for (int s = 0; s < SEG; s++) {   /* the edge */
            float p0 = Q * s / SEG, p1 = Q * (s + 1) / SEG;
            BVert a = edge_vert(BEV, p0, k), b = edge_vert(CUBE - BEV, p0, k), c = edge_vert(CUBE - BEV, p1, k), d = edge_vert(BEV, p1, k);
            bevel_tri(m, a, b, c, alpha);
            bevel_tri(m, a, c, d, alpha);
        }
        for (int j = 0; j < SEG; j++)     /* the corner */
            for (int i = 0; i < SEG; i++) {
                float a0 = Q * i / SEG, a1 = Q * (i + 1) / SEG, b0 = Q * j / SEG, b1 = Q * (j + 1) / SEG;
                BVert a = corner_vert(a0, b0, k), b = corner_vert(a1, b0, k), c = corner_vert(a1, b1, k), d = corner_vert(a0, b1, k);
                bevel_tri(m, a, b, c, alpha);
                bevel_tri(m, a, c, d, alpha);
            }
    }
}

/* Where the rounded edge meets the flat glass, the light fades softly into
 * the face instead of stopping at a line. */
static void face_fade(float alpha) {
    const float lo = BEV - 2, hi = CUBE - BEV + 2, w = 28;
    Color e = rgba(150, 185, 235, (u8)(0.16f * 255 * alpha)), in = rgba(150, 185, 235, 0);
    for (int k = 0; k < 4; k++) {
        /* a band from the edge inwards, mitred at the corners */
        float p[8] = { lo, lo, hi, lo, hi - w, lo + w, lo + w, lo + w };
        float q[8];
        for (int i = 0; i < 4; i++) {
            V3 v = quarter((V3){ p[i * 2], p[i * 2 + 1], 0 }, k);
            q[i * 2] = v.x;
            q[i * 2 + 1] = v.y;
        }
        Color c4[4] = { e, e, in, in };
        gfx_quad_colors(q, c4);
    }
}

/* What the glass reflects. The menu's world is dark, with a teal-green
 * light above, deep blue to the left and violet below, so as the cube
 * turns its faces pick up sweeping colour, strongest towards the edges
 * (each face is treated as very slightly curved, so the reflection varies
 * across it) and dark in the middle. */
static Color env_at(V3 r, float alpha) {
    float up = fmaxf(0, -r.y), dn = fmaxf(0, r.y), lf = fmaxf(0, -r.x), rt = fmaxf(0, r.x);
    float R = 40 * up + 40 * lf + 110 * dn * 0.8f + 70 * rt;
    float G = 190 * up + 70 * lf + 60 * dn * 0.8f + 60 * rt;
    float B = 150 * up + 220 * lf + 190 * dn * 0.8f + 150 * rt;
    float w = up * 0.95f + lf * 0.85f + dn * 0.6f + rt * 0.45f;
    if (w > 1e-4f) { R /= w; G /= w; B /= w; }
    float a = 0.42f * (up * up * up * 1.6f + lf * lf * lf * 1.1f + dn * dn * dn * 0.45f + rt * rt * rt * 0.4f);
    return rgba((u8)fminf(R, 255), (u8)fminf(G, 255), (u8)fminf(B, 255), (u8)(255 * clamp01(a * alpha)));
}

static void face_env(const M34 *m, float alpha) {
    enum { N = 6 };
    float xy[(N + 1) * (N + 1) * 2];
    Color col[(N + 1) * (N + 1)];
    const float lo = BEV - 2, span = CUBE - 2 * BEV + 4, curve = 0.9f;
    for (int j = 0; j <= N; j++)
        for (int i = 0; i <= N; i++) {
            int k = j * (N + 1) + i;
            float u = (float)i / N - 0.5f, v = (float)j / N - 0.5f;
            xy[k * 2] = lo + span * (u + 0.5f);
            xy[k * 2 + 1] = lo + span * (v + 0.5f);
            V3 n = xf_normal(m, (V3){ u * curve, v * curve, 1 });
            /* reflect the view direction (towards -z) off the glass */
            V3 rf = { 2 * n.z * n.x, 2 * n.z * n.y, 2 * n.z * n.z - 1 };
            float a = alpha * (n.z < 0 ? 0.15f : 1);
            col[k] = env_at(rf, a);
        }
    gfx_mesh(N, N, xy, col);
}

/* A soft glare across a face, sliding as the face turns. */
static void face_glare(const M34 *m, float alpha) {
    V3 n = xf_normal(m, (V3){ 0, 0, 1 });
    if (n.z <= 0.05f) return;
    float a = alpha * n.z * 0.10f;
    float s = 100 + 160 * (n.x - n.y) * 0.5f;   /* where the band crosses */
    float w = 46;
    float lo = BEV, hi = CUBE - BEV;
    /* the band runs from the top right to the bottom left: x + y = s .. s + w */
    for (int half = 0; half < 2; half++) {
        float d0 = s + half * w * 0.5f, d1 = d0 + w * 0.5f;
        /* corners of the slice, clipped to the face */
        float x0a = fminf(hi, fmaxf(lo, d0 - lo)), y0a = d0 - x0a;
        float x0b = fmaxf(lo, fminf(hi, d0 - hi)), y0b = d0 - x0b;
        float x1a = fminf(hi, fmaxf(lo, d1 - lo)), y1a = d1 - x1a;
        float x1b = fmaxf(lo, fminf(hi, d1 - hi)), y1b = d1 - x1b;
        if (y0a < lo || y0b > hi || y1a < lo || y1b > hi) continue;
        float p[8] = { x0a, y0a, x1a, y1a, x1b, y1b, x0b, y0b };
        Color c0 = rgba(230, 240, 255, (u8)(255 * a * (half ? 1 : 0))), c1 = rgba(230, 240, 255, (u8)(255 * a * (half ? 0 : 1)));
        Color c4[4] = { c0, c1, c1, c0 };
        gfx_quad_colors(p, c4);
    }
}


typedef struct { int kind; int face; M34 m; float z; } Item;

static int cmp_item(const void *a, const void *b) {
    float x = ((const Item *)a)->z, y = ((const Item *)b)->z;
    return x < y ? -1 : x > y;
}

void vcube_init(VCube *c) {
    memset(c, 0, sizeof(*c));
    c->scale = 1;
    c->alpha = 1;
    c->sway = 1;
    c->ax = VC_BASE_X; c->ay = VC_BASE_Y; c->az = VC_BASE_Z;
    c->icon = -1;
    c->tint = rgba(70, 140, 120, 255);
}

M34 vcube_matrix(const VCube *c) {
    float sx = 0, sy = 0, sz = 0;
    sway(c->time, &sx, &sy, &sz);
    sx *= c->sway; sy *= c->sway; sz *= c->sway;
    return m34_mul(m34_scale(c->scale),
           m34_mul(m34_mul(m34_rx(sx), m34_mul(m34_ry(sy), m34_rz(sz))),
                   m34_mul(m34_rx(c->ax), m34_mul(m34_ry(c->ay), m34_rz(c->az)))));
}

void vcube_bounds(const VCube *v, float *x0, float *y0, float *x1, float *y1) {
    M34 C = vcube_matrix(v);
    *x0 = *y0 = 1e9f; *x1 = *y1 = -1e9f;
    for (int k = 0; k < 8; k++) {
        float x = (k & 1) ? HALF : -HALF, y = (k & 2) ? HALF : -HALF, z = (k & 4) ? HALF : -HALF;
        float X = C.m[0] * x + C.m[1] * y + C.m[2] * z + C.m[3];
        float Y = C.m[4] * x + C.m[5] * y + C.m[6] * z + C.m[7];
        float Z = C.m[8] * x + C.m[9] * y + C.m[10] * z + C.m[11];
        float d = VC_PERSP - Z;
        if (d < 1) d = 1;
        float kk = VC_PERSP / d, sx = v->cx + X * kk, sy = v->cy + Y * kk;
        if (sx < *x0) *x0 = sx;
        if (sx > *x1) *x1 = sx;
        if (sy < *y0) *y0 = sy;
        if (sy > *y1) *y1 = sy;
    }
    /* the soft glow round the edges */
    *x0 -= 14; *y0 -= 14; *x1 += 14; *y1 += 14;
}

static void set_tint(Color t) {
    if (glass_ready && glass_tint.r == t.r && glass_tint.g == t.g && glass_tint.b == t.b) return;
    tint_r = t.r; tint_g = t.g; tint_b = t.b;
    glass_tint = t;
    glass_prepare();
}

void vcube_draw(const VCube *v) {
    if (v->alpha <= 0) return;
    set_tint(v->tint);
    M34 C = vcube_matrix(v);
    Item items[8];
    int n = 0;
    for (int f = 0; f < 6; f++) {
        Item *it = &items[n++];
        it->kind = 0;
        it->face = f;
        it->m = m34_mul(C, m34_mul(face_rot(f), m34_translate(-HALF, -HALF, HALF)));
        float X, Y, Z;
        m34_apply(&it->m, HALF, HALF, &X, &Y, &Z);
        it->z = Z;
    }
    if (v->icon >= 0 && v->icon_alpha > 0.01f) {
        Item *it = &items[n++];
        it->kind = 1;
        it->face = 0;
        float w1 = sinf(v->wob_time * 2 * (float)M_PI / 4.4f - (float)M_PI / 2);   /* -1..1 */
        M34 wob = m34_mul(m34_translate(90, 86, 0), m34_mul(m34_ry(18 * w1), m34_mul(m34_rx(-8 * w1), m34_translate(-90, -86, 0))));
        it->m = m34_mul(C, m34_mul(m34_translate(-HALF, -HALF, 34), wob));
        float X, Y, Z;
        m34_apply(&it->m, HALF, HALF, &X, &Y, &Z);
        it->z = Z;
    }
    qsort(items, n, sizeof(Item), cmp_item);
    float cx = v->cx, cy = v->cy;
    for (int k = 0; k < n; k++) {
        Item *it = &items[k];
        gfx_begin_3d(it->m.m, cx, cy, VC_PERSP);
        if (it->kind == 1) {
            draw_icon(v->icon, v->icon_alpha * v->alpha, v->icon_time, 0);
            continue;
        }
        int f = it->face;
        /* the flat glass sits inside the rounded edges */
        M34 inset = m34_mul(it->m, m34_mul(m34_translate(BEV - 2, BEV - 2, 0), m34_scale3((CUBE - 2 * BEV + 4) / CUBE, (CUBE - 2 * BEV + 4) / CUBE, 1)));
        gfx_begin_3d(inset.m, cx, cy, VC_PERSP);
        cube_glass(f == 0 ? 0 : 1, CUBE, 7, v->alpha, false);
        gfx_begin_3d(it->m.m, cx, cy, VC_PERSP);
        face_env(&it->m, v->alpha);
        face_fade(v->alpha);
        face_glare(&it->m, v->alpha);
        face_bevel(&it->m, v->alpha);
    }
    gfx_end_3d();
}
