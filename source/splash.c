/* splash.c - the opening. Solid blocks fly in from every direction and
 * stack into the three cubes of the CubeVerse logo, each block carrying
 * its piece of the logo's art; "CubeVerse" appears under them, fades, and
 * the three cubes glide and turn into the front three glass cubes of the
 * home screen (the fourth fades in behind), which then takes over.
 *
 * The logo cubes are real rounded cubes: flat sides, edges rounded in
 * strips and a cap on each corner, all lit from the top left. Each cube is
 * cut 3 x 3 x 3; every piece holds its own part of the surface (and the
 * face drawn on the front), plus plain inner walls seen only in flight. */
#include "app.h"
#include "home.h"
#include "vcube.h"
#include "cvui.h"
#include "input.h"
#include "sound.h"
#include "settings.h"
#include "m3d.h"
#include "intro_timing.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define H 90.0f            /* half the cube (the glass cubes are 180 across) */
#define RB 13.0f           /* the rounding */
#define F (H - RB)         /* flat half-width */
#define CUT 30.0f          /* pieces: cut at -30 and 30 */
#define NSEG 3             /* strips across each rounded edge */
#define NCORN 3            /* corner caps: NCORN x NCORN patches */

typedef struct { float x, y, z; } V3;
typedef struct { V3 p[3]; Color c; bool art; } Tri;

static V3 v3(float x, float y, float z) { V3 v = { x, y, z }; return v; }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vmul(V3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static V3 vcross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vnorm(V3 a) { float l = sqrtf(vdot(a, a)); return l > 1e-6f ? vmul(a, 1 / l) : a; }

/* ------------------------------------------------------------ the model */

#define MAXT 1400
typedef struct {
    Tri tris[MAXT];
    int n;
    int piece_of[MAXT];    /* which of the 27 cells */
} Model;

static Model model[3];     /* one per expression: happy, open, wink */
static bool built;

static const V3 LIGHT = { -0.42f, -0.70f, 0.58f };

static Color lit(Color base, V3 n) {
    V3 L = vnorm(LIGHT);
    float dif = vdot(n, L);
    if (dif < 0) dif = 0;
    V3 h = vnorm(vadd(L, v3(0, 0, 1)));
    float sp = vdot(n, h);
    if (sp < 0) sp = 0;
    sp = powf(sp, 40);
    float k = 0.62f + 0.48f * dif;
    int r = (int)(base.r * k + 200 * sp), g = (int)(base.g * k + 200 * sp), b = (int)(base.b * k + 200 * sp);
    return rgba((u8)(r > 255 ? 255 : r), (u8)(g > 255 ? 255 : g), (u8)(b > 255 ? 255 : b), 255);
}

static int cell_of(V3 p) {
    int ix = p.x < -CUT ? 0 : p.x > CUT ? 2 : 1;
    int iy = p.y < -CUT ? 0 : p.y > CUT ? 2 : 1;
    int iz = p.z < -CUT ? 0 : p.z > CUT ? 2 : 1;
    return ix + iy * 3 + iz * 9;
}

static void add_tri(Model *m, V3 a, V3 b, V3 c, Color col, bool art) {
    if (m->n >= MAXT) return;
    Tri *t = &m->tris[m->n];
    /* wind every triangle to face outwards (the cube is convex) */
    V3 mid0 = vmul(vadd(vadd(a, b), c), 1.0f / 3);
    V3 e1 = v3(b.x - a.x, b.y - a.y, b.z - a.z), e2 = v3(c.x - a.x, c.y - a.y, c.z - a.z);
    if (vdot(vcross(e1, e2), mid0) < 0) { V3 tmp = b; b = c; c = tmp; }
    t->p[0] = a; t->p[1] = b; t->p[2] = c;
    t->c = col;
    t->art = art;
    V3 mid = vmul(vadd(vadd(a, b), c), 1.0f / 3);
    m->piece_of[m->n] = cell_of(mid);
    m->n++;
}

static void add_quad(Model *m, V3 a, V3 b, V3 c, V3 d, Color col) {
    add_tri(m, a, b, c, col, false);
    add_tri(m, a, c, d, col, false);
}

/* splits [a0, a1] at the piece cuts */
static int splits(float a0, float a1, float *out) {
    int n = 0;
    out[n++] = a0;
    if (a0 < -CUT && a1 > -CUT) out[n++] = -CUT;
    if (a0 < CUT && a1 > CUT) out[n++] = CUT;
    out[n++] = a1;
    return n;
}

typedef struct { V3 n, u, v; Color c; } FaceDef;
/* front, right, top, left, back, bottom (u, v: the face's own across/down) */
static const FaceDef FACES[6] = {
    { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 255, 255, 255, 255 } },
    { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 }, { 0xCD, 0xEB, 0xBE, 255 } },
    { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0xF1, 0xFB, 0xEC, 255 } },
    { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0xCD, 0xEB, 0xBE, 255 } },
    { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0xCD, 0xEB, 0xBE, 255 } },
    { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0xB9, 0xDC, 0xA6, 255 } },
};

static V3 face_point(const FaceDef *f, float u, float v, float lift) {
    return vadd(vadd(vmul(f->n, H + lift), vmul(f->u, u)), vmul(f->v, v));
}

/* --- the face's art, as triangles in the front face's own (u, v) --- */

typedef struct { float u, v; } P2;
#define MAXART 400
static P2 art[MAXART][3];
static int nart;

static void art_tri(float u0, float v0, float u1, float v1, float u2, float v2) {
    if (nart >= MAXART) return;
    art[nart][0].u = u0; art[nart][0].v = v0;
    art[nart][1].u = u1; art[nart][1].v = v1;
    art[nart][2].u = u2; art[nart][2].v = v2;
    nart++;
}

static void art_disc(float cu, float cv, float r) {
    const int N = 14;
    for (int i = 0; i < N; i++) {
        float a0 = i * 2 * (float)M_PI / N, a1 = (i + 1) * 2 * (float)M_PI / N;
        art_tri(cu, cv, cu + cosf(a0) * r, cv + sinf(a0) * r, cu + cosf(a1) * r, cv + sinf(a1) * r);
    }
}

static void art_arc(float cu, float cv, float r, float t, float a0, float a1) {
    const int N = 12;
    for (int i = 0; i < N; i++) {
        float b0 = a0 + (a1 - a0) * i / N, b1 = a0 + (a1 - a0) * (i + 1) / N;
        float ri = r - t / 2, ro = r + t / 2;
        float x0 = cosf(b0), y0 = sinf(b0), x1 = cosf(b1), y1 = sinf(b1);
        art_tri(cu + x0 * ri, cv + y0 * ri, cu + x0 * ro, cv + y0 * ro, cu + x1 * ro, cv + y1 * ro);
        art_tri(cu + x0 * ri, cv + y0 * ri, cu + x1 * ro, cv + y1 * ro, cu + x1 * ri, cv + y1 * ri);
    }
    art_disc(cu + cosf(a0) * r, cv + sinf(a0) * r, t / 2);
    art_disc(cu + cosf(a1) * r, cv + sinf(a1) * r, t / 2);
}

static void art_line(float u0, float v0, float u1, float v1, float t) {
    float du = u1 - u0, dv = v1 - v0, l = sqrtf(du * du + dv * dv);
    float nu = -dv / l * t / 2, nv = du / l * t / 2;
    art_tri(u0 + nu, v0 + nv, u1 + nu, v1 + nv, u1 - nu, v1 - nv);
    art_tri(u0 + nu, v0 + nv, u1 - nu, v1 - nv, u0 - nu, v0 - nv);
    art_disc(u0, v0, t / 2);
    art_disc(u1, v1, t / 2);
}

/* the logo's faces, in the design's 112 box scaled to the 180 cube */
static void make_art(int expr) {
    nart = 0;
    float k = 180.0f / 112, o = -90;
#define U(x) ((x) * k + o)
    float t = 9 * k;
    if (expr == 0) {          /* happy */
        art_arc(U(36), U(47) + 4 * k, 9 * k, t, (float)M_PI + 0.35f, 2 * (float)M_PI - 0.35f);
        art_arc(U(76), U(47) + 4 * k, 9 * k, t, (float)M_PI + 0.35f, 2 * (float)M_PI - 0.35f);
        art_arc(U(56), U(58), 24 * k, t, 0.55f, (float)M_PI - 0.55f);
    } else if (expr == 1) {   /* open mouth */
        art_disc(U(35), U(43), 8.4f * k);
        art_disc(U(77), U(43), 8.4f * k);
        const int N = 12;
        for (int i = 0; i < N; i++) {   /* a half disc */
            float a0 = (float)M_PI * i / N, a1 = (float)M_PI * (i + 1) / N;
            art_tri(U(56), U(67), U(56) + cosf(a0) * 18 * k, U(67) + sinf(a0) * 15 * k, U(56) + cosf(a1) * 18 * k,
                    U(67) + sinf(a1) * 15 * k);
        }
    } else {                  /* wink */
        art_disc(U(35), U(43), 8.4f * k);
        art_line(U(71), U(43), U(89), U(43), t);
        art_arc(U(56), U(54), 22 * k, t, 0.6f, (float)M_PI - 0.6f);
    }
#undef U
}

/* clips a triangle to the box [u0,u1] x [v0,v1]: a convex polygon */
static int clip_poly(P2 *in, int n, float u0, float v0, float u1, float v1, P2 *out) {
    P2 buf[16];
    P2 *src = in, *dst = buf;
    int cnt = n;
    for (int edge = 0; edge < 4 && cnt > 0; edge++) {
        int m = 0;
        for (int i = 0; i < cnt; i++) {
            P2 a = src[i], b = src[(i + 1) % cnt];
            float da, db;
            switch (edge) {
            case 0: da = a.u - u0; db = b.u - u0; break;
            case 1: da = u1 - a.u; db = u1 - b.u; break;
            case 2: da = a.v - v0; db = b.v - v0; break;
            default: da = v1 - a.v; db = v1 - b.v; break;
            }
            if (da >= 0) dst[m++] = a;
            if ((da >= 0) != (db >= 0) && m < 15) {
                float t = da / (da - db);
                P2 c = { a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t };
                dst[m++] = c;
            }
        }
        cnt = m;
        src = dst;
        dst = (dst == buf) ? out : buf;
    }
    if (src != out) memcpy(out, src, sizeof(P2) * (size_t)cnt);
    return cnt;
}

static void build_model(Model *m, int expr) {
    m->n = 0;
    float cuts[4];
    /* the six flat sides, split where the pieces meet */
    for (int f = 0; f < 6; f++) {
        const FaceDef *fd = &FACES[f];
        int nc = splits(-F, F, cuts);
        for (int i = 0; i + 1 < nc; i++)
            for (int j = 0; j + 1 < nc; j++) {
                float u0 = cuts[i], u1 = cuts[i + 1], v0 = cuts[j], v1 = cuts[j + 1];
                add_quad(m, face_point(fd, u0, v0, 0), face_point(fd, u1, v0, 0), face_point(fd, u1, v1, 0),
                         face_point(fd, u0, v1, 0), fd->c);
            }
    }
    /* the face's art on the front, cut along the same lines, a hair above it */
    make_art(expr);
    for (int a = 0; a < nart; a++) {
        int nc = splits(-F, F, cuts);
        for (int i = 0; i + 1 < nc; i++)
            for (int j = 0; j + 1 < nc; j++) {
                P2 poly[16];
                int k = clip_poly(art[a], 3, cuts[i], cuts[j], cuts[i + 1], cuts[j + 1], poly);
                for (int q = 1; q + 1 < k; q++)
                    add_tri(m, face_point(&FACES[0], poly[0].u, poly[0].v, 0.4f), face_point(&FACES[0], poly[q].u, poly[q].v, 0.4f),
                            face_point(&FACES[0], poly[q + 1].u, poly[q + 1].v, 0.4f), rgba(0x3A, 0x9A, 0x18, 255), true);
            }
    }
    /* the twelve rounded edges: between each pair of neighbouring sides */
    for (int a = 0; a < 6; a++)
        for (int b = a + 1; b < 6; b++) {
            V3 na = FACES[a].n, nb = FACES[b].n;
            if (fabsf(vdot(na, nb)) > 0.5f) continue;
            V3 axis = vcross(na, nb);
            axis = v3(fabsf(axis.x), fabsf(axis.y), fabsf(axis.z));
            V3 centre = vadd(vmul(na, F), vmul(nb, F));
            int nc = splits(-F, F, cuts);
            for (int s = 0; s < NSEG; s++) {
                float t0 = (float)M_PI / 2 * s / NSEG, t1 = (float)M_PI / 2 * (s + 1) / NSEG;
                V3 n0 = vadd(vmul(na, cosf(t0)), vmul(nb, sinf(t0)));
                V3 n1 = vadd(vmul(na, cosf(t1)), vmul(nb, sinf(t1)));
                V3 nm = vnorm(vadd(n0, n1));
                Color c = lit(rgba(0x56, 0xC2, 0x2F, 255), nm);
                for (int i = 0; i + 1 < nc; i++) {
                    V3 p00 = vadd(vadd(centre, vmul(n0, RB)), vmul(axis, cuts[i]));
                    V3 p01 = vadd(vadd(centre, vmul(n0, RB)), vmul(axis, cuts[i + 1]));
                    V3 p11 = vadd(vadd(centre, vmul(n1, RB)), vmul(axis, cuts[i + 1]));
                    V3 p10 = vadd(vadd(centre, vmul(n1, RB)), vmul(axis, cuts[i]));
                    add_quad(m, p00, p01, p11, p10, c);
                }
            }
        }
    /* the eight corners: a patch of a sphere each */
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                V3 centre = v3(sx * F, sy * F, sz * F);
                for (int i = 0; i < NCORN; i++)
                    for (int j = 0; j < NCORN; j++) {
                        V3 q[4];
                        for (int k = 0; k < 4; k++) {
                            float a = (float)(i + (k == 1 || k == 2)) / NCORN, b = (float)(j + (k >= 2)) / NCORN;
                            /* a point on the octant: from the x axis toward y (a) and toward z (b) */
                            float th = a * (float)M_PI / 2, ph = b * (float)M_PI / 2;
                            V3 d = v3(cosf(th) * cosf(ph) * sx, sinf(th) * cosf(ph) * sy, sinf(ph) * sz);
                            q[k] = vadd(centre, vmul(d, RB));
                        }
                        V3 mid = vnorm(vadd(vadd(q[0], q[2]), vmul(centre, -2)));
                        add_quad(m, q[0], q[1], q[2], q[3], lit(rgba(0x56, 0xC2, 0x2F, 255), mid));
                    }
            }
}

/* ------------------------------------------------------------ drawing */

static V3 xf(const M34 *m, V3 p) {
    return v3(m->m[0] * p.x + m->m[1] * p.y + m->m[2] * p.z + m->m[3], m->m[4] * p.x + m->m[5] * p.y + m->m[6] * p.z + m->m[7],
              m->m[8] * p.x + m->m[9] * p.y + m->m[10] * p.z + m->m[11]);
}

/* front-facing (seen from the viewer at +z with perspective) */
static bool facing(V3 a, V3 b, V3 c) {
    const float P = VC_PERSP;
    float ka = P / (P - a.z), kb = P / (P - b.z), kc = P / (P - c.z);
    float ax = a.x * ka, ay = a.y * ka, bx = b.x * kb, by = b.y * kb, cx = c.x * kc, cy = c.y * kc;
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax) > 0;
}

static void draw_tris(const Model *m, int piece, const M34 *mx, float cx, float cy, float al) {
    gfx_begin_3d(mx->m, cx, cy, VC_PERSP);
    /* surface first, then the art over it */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < m->n; i++) {
            const Tri *t = &m->tris[i];
            if (t->art != (pass == 1)) continue;
            if (piece >= 0 && m->piece_of[i] != piece) continue;
            V3 a = xf(mx, t->p[0]), b = xf(mx, t->p[1]), c = xf(mx, t->p[2]);
            if (!facing(a, b, c)) continue;
            float p[9] = { a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z };
            Color col = t->c;
            col.a = (u8)(255 * al);
            Color c3[3] = { col, col, col };
            gfx_tri3d(p, c3);
        }
    gfx_end_3d();
}

/* the inner walls of piece (ix, iy, iz), seen only while it flies */
static void draw_walls(int ix, int iy, int iz, const M34 *mx, float cx, float cy, float al, float settle) {
    float lo[3], hi[3];
    int id[3] = { ix, iy, iz };
    for (int k = 0; k < 3; k++) {
        lo[k] = id[k] == 0 ? -H : id[k] == 1 ? -CUT : CUT;
        hi[k] = id[k] == 0 ? -CUT : id[k] == 1 ? CUT : H;
    }
    gfx_begin_3d(mx->m, cx, cy, VC_PERSP);
    for (int f = 0; f < 6; f++) {
        const FaceDef *fd = &FACES[f];
        float nv[3] = { fd->n.x, fd->n.y, fd->n.z };
        int ax = nv[0] != 0 ? 0 : nv[1] != 0 ? 1 : 2;
        float plane = nv[ax] > 0 ? hi[ax] : lo[ax];
        if (fabsf(plane) > H - 0.01f) continue;   /* outside: the surface covers it */
        /* the wall's extent along the other two axes: the whole block while it
         * flies (a solid cube), drawing in to the rounding as it lands */
        float edge = lerpf(H, F, settle);
        float a0[3], a1[3];
        for (int k = 0; k < 3; k++) {
            a0[k] = lo[k] <= -H + 0.01f ? -edge : lo[k];
            a1[k] = hi[k] >= H - 0.01f ? edge : hi[k];
        }
        V3 c[4];
        int o1 = (ax + 1) % 3, o2 = (ax + 2) % 3;
        for (int k = 0; k < 4; k++) {
            float v[3];
            v[ax] = plane;
            v[o1] = (k == 1 || k == 2) ? a1[o1] : a0[o1];
            v[o2] = k >= 2 ? a1[o2] : a0[o2];
            c[k] = v3(v[0], v[1], v[2]);
        }
        Color col = lit(rgba(0xE4, 0xF1, 0xDC, 255), fd->n);
        col.a = (u8)(255 * al);
        Color c3[3] = { col, col, col };
        /* wind it so it faces out of the piece, then draw it only if that side
         * faces the viewer (a wall at the back would otherwise paint over the
         * one at the front, and the piece would look hollow) */
        V3 nrm = vcross(vadd(c[1], vmul(c[0], -1)), vadd(c[2], vmul(c[0], -1)));
        if (vdot(nrm, fd->n) < 0) { V3 tmp = c[1]; c[1] = c[3]; c[3] = tmp; }
        V3 w[4];
        for (int k = 0; k < 4; k++) w[k] = xf(mx, c[k]);
        if (!facing(w[0], w[1], w[2])) continue;
        int order[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
        for (int tri = 0; tri < 2; tri++) {
            V3 A = w[order[tri][0]], B = w[order[tri][1]], C = w[order[tri][2]];
            float p[9] = { A.x, A.y, A.z, B.x, B.y, B.z, C.x, C.y, C.z };
            gfx_tri3d(p, c3);
        }
    }
    gfx_end_3d();
}

/* ------------------------------------------------------------ the show */

#define T_FLY INTRO_T_FLY /* each piece's flight */
#define T_SPIN 2.8f       /* its spin is over by then */
#define T_SOLID 5.6f      /* the last piece has landed */
#define T_WORD_IN 5.9f
#define T_WORD_OUT 8.0f
#define T_GLIDE 8.7f
#define T_GLIDE_LEN 1.9f
#define T_HOME 11.4f

typedef IntroFlight Flight;
static Flight flights[3][27];
static float t;
static bool start_played, assemble_played;

/* which home cube each logo cube becomes, where the logo cube sits, how it's turned */
static const int TO_HOME[3] = { 0, 1, 3 };          /* middle -> Activity, right -> Communities, left -> My page */
static const int EXPR[3] = { 0, 1, 2 };             /* happy, open, wink */
static const float LX[3] = { 0, 116, -116 }, LY[3] = { -26, 10, 10 }, LS[3] = { 0.9f, 0.64f, 0.64f };
static const float LRZ[3] = { 0, 12, -14 };
static const float THETA[3] = { 0, 90, 270 };


static void splash_enter(const char *arg, bool back) {
    (void)arg; (void)back;
    if (!built) {
        for (int i = 0; i < 3; i++) build_model(&model[i], EXPR[i]);
        built = true;
    }
    intro_flights(&flights[0][0]);   /* (the same ones the sound is timed to) */
    t = 0;
    start_played = false;
    assemble_played = false;
    if (!g_set.intro) t = T_GLIDE + T_GLIDE_LEN;   /* opening turned off: straight to the cubes settling */
}

static void go_home(void) {
    g_home_section = 0;
    app_replace(SCR_HOME, "splash");
}

static void splash_update(void) {
    t += g_dt;
    /* the pieces' whoosh and clicks, from the very start (if it's made in time) */
    if (!assemble_played && t < 0.6f && g_set.intro && sound_loaded(SFX_ASSEMBLE)) { assemble_played = true; sound_play(SFX_ASSEMBLE); }
    if (t >= 0.6f) assemble_played = true;
    if (t > 0.3f && t < T_GLIDE && input_pressed(BTN_A | BTN_START | BTN_B)) t = T_GLIDE;   /* skip ahead */
    if (!start_played && t >= T_SOLID) {
        if (sound_loaded(SFX_START) || t > T_SOLID + 1.5f) { start_played = true; sound_play(SFX_START); }
    }
    if (t >= T_HOME) go_home();
}

static M34 logo_matrix(int c, float glide) {
    float sc_logo = LS[c] * 112.0f / 180.0f;
    float dx, dy, sc_home, al_home;
    home_slot(THETA[c], &dx, &dy, &sc_home, &al_home);
    float sc = lerpf(sc_logo, sc_home, glide);
    float ax = lerpf(-22, VC_BASE_X, glide), ay = lerpf(-30, VC_BASE_Y, glide), az = lerpf(LRZ[c], VC_BASE_Z, glide);
    return m34_mul(m34_scale(sc), m34_mul(m34_rx(ax), m34_mul(m34_ry(ay), m34_rz(az))));
}

static void splash_draw(float open) {
    (void)open;
    gfx_fill_screen(rgba(0, 0, 0, 255));
    float glide = ease_bezier(clamp01((t - T_GLIDE) / T_GLIDE_LEN), 0.5f, 0, 0.2f, 1);
    float bg = clamp01((t - T_GLIDE) / (T_GLIDE_LEN + 0.0f));
    cv_background(bg);
    float W = gfx_width(), cy0 = home_center_y();

    /* the back cube (Notifications) fades in as the others arrive */
    float back_a = clamp01((t - (T_GLIDE + 0.7f)) / 1.2f);
    if (back_a > 0) {
        VCube v;
        home_cube_setup(&v, 2, 180, g_time);
        v.alpha *= back_a;
        v.sway = glide;
        vcube_draw(&v);
    }
    /* the three logo cubes: back to front */
    int order[3] = { 1, 2, 0 };
    for (int oi = 0; oi < 3; oi++) {
        int c = order[oi];
        float dx, dy, s, a;
        home_slot(THETA[c], &dx, &dy, &s, &a);
        float ccx = W / 2 + lerpf(LX[c], dx, glide), ccy = cy0 + lerpf(LY[c], dy, glide);
        float glass = clamp01((t - (T_GLIDE + 0.1f)) / 1.3f);
        float solid_a = 1 - glass;
        M34 base = logo_matrix(c, glide);
        if (t < T_SOLID) {
            /* pieces in flight: farthest first */
            int ord[27];
            float dep[27];
            M34 mats[27];
            int n = 0;
            for (int p = 0; p < 27; p++) {
                if (p == 13) continue;   /* the middle one is never seen */
                const Flight *f = &flights[c][p];
                float lt = t - f->delay;
                if (lt <= 0) continue;
                float k = intro_progress(lt);
                float sk = ease_bezier(clamp01(lt / T_SPIN), 0.2f, 0.5f, 0.3f, 1);
                int ix = p % 3, iy = (p / 3) % 3, iz = p / 9;
                V3 pc = v3((ix - 1) * 60.0f, (iy - 1) * 60.0f, (iz - 1) * 60.0f);
                /* move out along its own line, turning about its own middle */
                M34 local = m34_mul(m34_translate(f->fx * (1 - k) + pc.x, f->fy * (1 - k) + pc.y, f->fz * (1 - k) + pc.z),
                                    m34_mul(m34_mul(m34_rx(f->rx * (1 - sk)), m34_ry(f->ry * (1 - sk))),
                                            m34_translate(-pc.x, -pc.y, -pc.z)));
                mats[p] = m34_mul(base, local);
                V3 w = xf(&mats[p], pc);
                if (w.z > VC_PERSP - 60) continue;   /* still behind the viewer */
                dep[p] = w.z;
                ord[n++] = p;
            }
            for (int i = 0; i < n; i++)
                for (int j = i + 1; j < n; j++)
                    if (dep[ord[j]] < dep[ord[i]]) { int tmp = ord[i]; ord[i] = ord[j]; ord[j] = tmp; }
            for (int i = 0; i < n; i++) {
                int p = ord[i];
                float lt = t - flights[c][p].delay;
                float fade = clamp01(lt / 0.25f);
                float settle = clamp01((lt - (T_FLY - 0.6f)) / 0.55f);
                if (lt < T_FLY - 0.05f) draw_walls(p % 3, (p / 3) % 3, p / 9, &mats[p], ccx, ccy, fade, settle);
                draw_tris(&model[c], p, &mats[p], ccx, ccy, fade);
            }
        } else if (solid_a > 0) {
            draw_tris(&model[c], -1, &base, ccx, ccy, solid_a);
        }
        if (glass > 0) {
            VCube v;
            home_cube_setup(&v, TO_HOME[c], THETA[c], g_time);
            v.cx = ccx;
            v.cy = ccy;
            v.ax = lerpf(-22, VC_BASE_X, glide);
            v.ay = lerpf(-30, VC_BASE_Y, glide);
            v.az = lerpf(LRZ[c], VC_BASE_Z, glide);
            v.scale = lerpf(LS[c] * 112.0f / 180.0f, v.scale, glide);
            v.sway = glide;
            v.alpha *= glass;
            v.icon_alpha = clamp01((t - (T_GLIDE + 1.4f)) / 0.8f);
            vcube_draw(&v);
        }
    }
    /* the name */
    float wa = clamp01((t - T_WORD_IN) / 0.7f) * (1 - clamp01((t - T_WORD_OUT) / 0.7f));
    if (wa > 0) {
        float rise = (1 - clamp01((t - T_WORD_IN) / 0.7f)) * 10 - clamp01((t - T_WORD_OUT) / 0.7f) * 6;
        const char *name = "CubeVerse";
        float tw = gfx_text_width_sp(FONT_CUBE, 52, -1, name, -1);
        ui_text_glow(FONT_CUBE, 52, -1, W / 2 - tw / 2, 282 + rise, 60, rgba(255, 255, 255, (u8)(255 * wa)),
                     rgba(127, 224, 90, (u8)(150 * wa)), 9, name);
    }
    /* the home screen's parts arrive */
    float ha = clamp01((t - (T_GLIDE + 1.7f)) / 0.9f);
    if (ha > 0) {
        home_draw_header(ha);
        home_draw_labels(0, ha);
        home_draw_hints(ha);
    }
    /* a moment of black at the very start */
    float in = clamp01(t / 0.4f);
    if (in < 1) gfx_fill_screen(rgba(0, 0, 0, (u8)(255 * (1 - in))));
}

const Screen screen_splash = { splash_enter, splash_update, splash_draw, NULL };
