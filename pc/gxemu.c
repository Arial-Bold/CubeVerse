/* gxemu.c - a tiny software GX for the PC test programs: rasterises what
 * gfx.c sends (quads, strips, fans) into a 640x480 picture with the same
 * blending, textures (I4, I8, RGB565, RGB5A3 in GX tile order), scissor and
 * depth-mask behaviour. No anti-aliasing, like the real console. */
#include <gccore.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GXRModeObj TVNtsc480IntDf = { 0, 640, 480, 480, 40, 0, 640, 480, 0, 0, 0, {{0}}, {0} };
GXRModeObj TVNtsc480Prog, TVEurgb60Hz480Prog, TVPal528IntDf;
#define W 640
#define H 480
static float fb[W * H * 3], disp[W * H * 3];
static unsigned char zb[W * H];   /* 1 = marked by a mask */
static int sx0 = 0, sy0 = 0, sx1 = W, sy1 = H;
static Mtx pos;
static float pm00 = 2.0f / 640, pm03 = -1, pm11 = -2.0f / 480, pm13 = 1;
static int zmode;   /* 0 off, 1 write marks, 2 test equal */
static bool additive;
static int tev;     /* 0 pass colour, 1 modulate, 2 alpha from texture */
static int alpha_tex_mode;
static GXTexObj *tex;
static void (*done_cb)(void);

void VIDEO_Init(void) {} void VIDEO_Configure(GXRModeObj *m) { (void)m; }
void VIDEO_ClearFrameBuffer(GXRModeObj *m, void *f, u32 c) { (void)m; (void)f; (void)c; }
void VIDEO_SetNextFramebuffer(void *f) { (void)f; } void VIDEO_SetBlack(int b) { (void)b; } void VIDEO_Flush(void) {}
void VIDEO_WaitVSync(void) {}
GXRModeObj *VIDEO_GetPreferredMode(void *x) { (void)x; return &TVNtsc480IntDf; }
int VIDEO_GetCurrentTvMode(void) { return 0; } int VIDEO_HaveComponentCable(void) { return 0; }
static GXFifoObj fo_;
GXFifoObj *GX_Init(void *f, u32 s) { (void)f; (void)s; return &fo_; }
void GX_InitFifoLimits(GXFifoObj *f, u32 h, u32 l) { (void)f; (void)h; (void)l; }
void GX_SetCPUFifo(GXFifoObj *f) { (void)f; } void GX_SetGPFifo(GXFifoObj *f) { (void)f; } void GX_SetCopyClear(GXColor c, u32 z) { (void)c; (void)z; }
f32 GX_GetYScaleFactor(u16 a, u16 b) { (void)a; (void)b; return 1; } u32 GX_SetDispCopyYScale(f32 s) { (void)s; return 480; }
void GX_SetViewport(f32 x, f32 y, f32 w, f32 h, f32 n, f32 f) { (void)x; (void)y; (void)w; (void)h; (void)n; (void)f; }
void GX_SetScissor(u32 x, u32 y, u32 w, u32 h) { sx0 = (int)x; sy0 = (int)y; sx1 = (int)(x + w); sy1 = (int)(y + h); }
void GX_SetDispCopySrc(u16 a, u16 b, u16 c, u16 d) { (void)a; (void)b; (void)c; (void)d; }
void GX_SetDispCopyDst(u16 a, u16 b) { (void)a; (void)b; }
void GX_SetCopyFilter(u8 aa, u8 sp[12][2], u8 vf, u8 *v) { (void)aa; (void)sp; (void)vf; (void)v; }
void GX_SetFieldMode(u8 a, u8 b) { (void)a; (void)b; } void GX_SetPixelFmt(u8 a, u8 b) { (void)a; (void)b; }
void GX_SetDispCopyGamma(u8 g) { (void)g; } void GX_SetCullMode(u8 m) { (void)m; }
void GX_CopyDisp(void *f, u8 c) { (void)f; memcpy(disp, fb, sizeof(fb)); if (c) { memset(fb, 0, sizeof(fb)); memset(zb, 0, sizeof(zb)); } }
void GX_SetDrawDoneCallback(void (*cb)(void)) { done_cb = cb; }
void GX_SetDrawDone(void) { if (done_cb) done_cb(); }
void GX_DrawDone(void) {} void GX_AbortFrame(void) {} void GX_InvalidateTexAll(void) {} void GX_InvVtxCache(void) {}
void GX_ClearVtxDesc(void) {} void GX_SetVtxAttrFmt(u8 a, u32 b, u32 c, u32 d, u8 e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void GX_SetVtxDesc(u8 a, u8 b) { (void)a; (void)b; } void GX_SetNumChans(u8 n) { (void)n; }
void GX_SetChanCtrl(int a, int b, int c, int d, int e, int f, int g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void GX_SetZMode(u8 en, u8 fn, u8 upd) { zmode = !en ? 0 : (fn == GX_ALWAYS && upd) ? 1 : 2; }
void GX_SetBlendMode(u8 t, u8 s, u8 d, u8 o) { (void)t; (void)s; (void)o; additive = d == GX_BL_ONE; }
void GX_SetAlphaUpdate(u8 e) { (void)e; } void GX_SetColorUpdate(u8 e) { (void)e; } void GX_SetNumTevStages(u8 n) { (void)n; }
static int ntexgens;
static float texoff[4][2];   /* the four looks of a smoothed picture (texture matrices) */
static int gen_mtx[4];
void GX_SetNumTexGens(u32 n) { ntexgens = (int)n; if (!n) tev = 0; if (n == 4) tev = 3; }
void GX_SetTevKColorSel(u8 st, u8 sel) { (void)st; (void)sel; }
void GX_LoadTexMtxImm(float m[3][4], u32 id, u8 type) {
    (void)type; int k = ((int)id - GX_TEXMTX0) / 3; if (k >= 0 && k < 4) { texoff[k][0] = m[0][3]; texoff[k][1] = m[1][3]; } }
void GX_SetTevOrder(u8 s, u8 c, u32 m, u8 col) { (void)s; (void)c; (void)m; (void)col; }
void GX_SetTevOp(u8 s, u8 mode) { (void)s; tev = mode == GX_MODULATE ? 1 : 0; }
void GX_SetTexCoordGen(u16 a, u32 b, u32 c, u32 d) {
    (void)b; (void)c; int i = a == GX_TEXCOORD0 ? 0 : a == GX_TEXCOORD1 ? 1 : a == GX_TEXCOORD2 ? 2 : a == GX_TEXCOORD3 ? 3 : -1;
    if (i >= 0) gen_mtx[i] = (int)d; }
void GX_SetTevColorIn(u8 s, u8 a, u8 b, u8 c, u8 d) { (void)s; (void)a; (void)b; (void)c; (void)d; }
void GX_SetTevColorOp(u8 s, u8 a, u8 b, u8 c, u8 d, u8 e) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; }
void GX_SetTevAlphaIn(u8 s, u8 a, u8 b, u8 c, u8 d) { (void)s; (void)a; (void)c; (void)d; if (b == GX_CA_TEXA) tev = 2; }
void GX_SetTevAlphaOp(u8 s, u8 a, u8 b, u8 c, u8 d, u8 e) { (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; }
void GX_InitTexObj(GXTexObj *o, void *data, u16 w, u16 h, u8 fmt, u8 ws, u8 wt, u8 mip) {
    (void)wt; (void)mip; o->data = data; o->w = w; o->h = h; o->fmt = fmt; o->wrap = ws; }
void GX_InitTexObjLOD(GXTexObj *o, int a, int b, f32 c, f32 d, f32 e, u8 f, u8 g, int h) {
    (void)o; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; }
void GX_LoadTexObj(GXTexObj *o, u8 m) { (void)m; tex = o; }
void GX_LoadPosMtxImm(Mtx m, u32 id) { (void)id; memcpy(pos, m, sizeof(Mtx)); }
void GX_SetCurrentMtx(u32 id) { (void)id; }
void GX_LoadProjectionMtx(Mtx44 m, u8 t) { (void)t; pm00 = m[0][0]; pm03 = m[0][3]; pm11 = m[1][1]; pm13 = m[1][3]; }
void guMtxIdentity(Mtx m) { memset(m, 0, sizeof(Mtx)); m[0][0] = m[1][1] = m[2][2] = 1; }
void guOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
    (void)n; (void)f; memset(m, 0, sizeof(Mtx44));
    m[0][0] = 2.0f / (r - l); m[0][3] = -(r + l) / (r - l);
    m[1][1] = 2.0f / (t - b); m[1][3] = -(t + b) / (t - b); }

/* ---- textures */
static void texel(const GXTexObj *o, int x, int y, float *c) {
    if (o->wrap == GX_REPEAT) { x = ((x % o->w) + o->w) % o->w; y = ((y % o->h) + o->h) % o->h; }
    else { if (x < 0) x = 0; if (y < 0) y = 0; if (x >= o->w) x = o->w - 1; if (y >= o->h) y = o->h - 1; }
    const unsigned char *d = o->data;
    if (o->fmt == GX_TF_I4) {
        int tile = (y / 8) * (o->w / 8) + x / 8, idx = tile * 32 + (y % 8) * 4 + (x % 8) / 2;
        int v = (x % 2) ? (d[idx] & 15) : (d[idx] >> 4);
        c[0] = c[1] = c[2] = c[3] = v / 15.0f;
    } else if (o->fmt == GX_TF_I8) {
        int tile = (y / 4) * (o->w / 8) + x / 8;
        float v = d[tile * 32 + (y % 4) * 8 + (x % 8)] / 255.0f;
        c[0] = c[1] = c[2] = c[3] = v;
    } else {
        const uint16_t *p = (const uint16_t *)d;
        uint32_t i = (uint32_t)((((y >> 2) * (o->w >> 2) + (x >> 2)) << 4) + ((y & 3) << 2) + (x & 3));
        uint16_t v = p[i];
        if (o->fmt == GX_TF_RGB565) {
            c[0] = ((v >> 11) & 31) / 31.0f; c[1] = ((v >> 5) & 63) / 63.0f; c[2] = (v & 31) / 31.0f; c[3] = 1;
        } else if (v & 0x8000) {
            c[0] = ((v >> 10) & 31) / 31.0f; c[1] = ((v >> 5) & 31) / 31.0f; c[2] = (v & 31) / 31.0f; c[3] = 1;
        } else {
            c[3] = ((v >> 12) & 7) / 7.0f; c[0] = ((v >> 8) & 15) / 15.0f; c[1] = ((v >> 4) & 15) / 15.0f; c[2] = (v & 15) / 15.0f;
        }
    }
}
static void sample(float u, float v, float *out) {
    float fx = u * tex->w - 0.5f, fy = v * tex->h - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - x0, ay = fy - y0, a[4], b[4], c[4], d[4];
    texel(tex, x0, y0, a); texel(tex, x0 + 1, y0, b); texel(tex, x0, y0 + 1, c); texel(tex, x0 + 1, y0 + 1, d);
    for (int k = 0; k < 4; k++) out[k] = (a[k] * (1 - ax) + b[k] * ax) * (1 - ay) + (c[k] * (1 - ax) + d[k] * ax) * ay;
}

/* ---- triangles */
typedef struct { float x, y, r, g, b, a, u, v; } V;
static void plot(int x, int y, float r, float g, float b, float a) {
    if (x < sx0 || y < sy0 || x >= sx1 || y >= sy1 || x < 0 || y < 0 || x >= W || y >= H) return;
    if (zmode == 1) zb[y * W + x] = 1;
    if (zmode == 2 && !zb[y * W + x]) return;
    if (a <= 0) return;
    if (a > 1) a = 1;
    float *p = &fb[(y * W + x) * 3];
    if (additive) { p[0] = fminf(1, p[0] + r * a); p[1] = fminf(1, p[1] + g * a); p[2] = fminf(1, p[2] + b * a); return; }
    p[0] = p[0] * (1 - a) + r * a; p[1] = p[1] * (1 - a) + g * a; p[2] = p[2] * (1 - a) + b * a;
}
static void tri(const V *A, const V *B, const V *C) {
    float area = (B->x - A->x) * (C->y - A->y) - (C->x - A->x) * (B->y - A->y);
    if (fabsf(area) < 1e-7f) return;
    int minx = (int)floorf(fminf(A->x, fminf(B->x, C->x))), maxx = (int)ceilf(fmaxf(A->x, fmaxf(B->x, C->x)));
    int miny = (int)floorf(fminf(A->y, fminf(B->y, C->y))), maxy = (int)ceilf(fmaxf(A->y, fmaxf(B->y, C->y)));
    if (minx < 0) minx = 0; if (miny < 0) miny = 0; if (maxx > W - 1) maxx = W - 1; if (maxy > H - 1) maxy = H - 1;
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float l1 = ((B->x - px) * (C->y - py) - (C->x - px) * (B->y - py)) / area;
            float l2 = ((C->x - px) * (A->y - py) - (A->x - px) * (C->y - py)) / area;
            float l3 = 1 - l1 - l2;
            /* top-left-ish fill rule: include edges on one side only */
            if (l1 < -1e-6f || l2 < -1e-6f || l3 < -1e-6f) continue;
            if ((l1 <= 1e-6f && l1 >= -1e-6f && area < 0) || (l2 <= 1e-6f && l2 >= -1e-6f && area < 0)) continue;
            float r = A->r * l1 + B->r * l2 + C->r * l3, g = A->g * l1 + B->g * l2 + C->g * l3;
            float b = A->b * l1 + B->b * l2 + C->b * l3, a = A->a * l1 + B->a * l2 + C->a * l3;
            if (tev && tex) {
                float t[4];
                float u = A->u * l1 + B->u * l2 + C->u * l3, v = A->v * l1 + B->v * l2 + C->v * l3;
                if (tev == 3) {   /* four offset looks averaged, then times the vertex colour */
                    float acc[4] = { 0, 0, 0, 0 };
                    for (int k = 0; k < 4; k++) {
                        float du = gen_mtx[k] == GX_IDENTITY ? 0 : texoff[k][0], dv = gen_mtx[k] == GX_IDENTITY ? 0 : texoff[k][1];
                        sample(u + du, v + dv, t);
                        for (int q = 0; q < 4; q++) acc[q] += t[q] * 0.25f;
                    }
                    r *= acc[0]; g *= acc[1]; b *= acc[2];
                } else sample(u, v, t);
                if (tev == 1) { r *= t[0]; g *= t[1]; b *= t[2]; a *= t[3]; }
                else a *= t[3];
            }
            plot(x, y, r, g, b, a);
        }
}
static V vb[8192];
static int vn, vexp, prim;
void GX_Begin(u8 p, u8 f, u16 n) { (void)f; prim = p; vn = 0; vexp = n; }
void GX_Position2f32(f32 x, f32 y) {
    if (vn >= 8192) return;
    float X = pos[0][0] * x + pos[0][1] * y + pos[0][3], Y = pos[1][0] * x + pos[1][1] * y + pos[1][3];
    vb[vn].x = (pm00 * X + pm03 + 1) * 0.5f * W; vb[vn].y = (1 - (pm11 * Y + pm13)) * 0.5f * H; vb[vn].u = vb[vn].v = 0; vn++;
}
void GX_Color4u8(u8 r, u8 g, u8 b, u8 a) { V *v = &vb[vn - 1]; v->r = r / 255.0f; v->g = g / 255.0f; v->b = b / 255.0f; v->a = a / 255.0f; }
void GX_TexCoord2f32(f32 s, f32 t) { vb[vn - 1].u = s; vb[vn - 1].v = t; }
void GX_End(void) {
    if (prim == GX_QUADS) for (int i = 0; i + 3 < vn; i += 4) { tri(&vb[i], &vb[i + 1], &vb[i + 2]); tri(&vb[i], &vb[i + 2], &vb[i + 3]); }
    else if (prim == GX_TRIANGLES) for (int i = 0; i + 2 < vn; i += 3) tri(&vb[i], &vb[i + 1], &vb[i + 2]);
    else if (prim == GX_TRIANGLESTRIP) for (int i = 0; i + 2 < vn; i++) tri(&vb[i], &vb[i + 1], &vb[i + 2]);
    else if (prim == GX_TRIANGLEFAN) for (int i = 1; i + 1 < vn; i++) tri(&vb[0], &vb[i], &vb[i + 1]);
}

void gxemu_clear(void) { memset(fb, 0, sizeof(fb)); memset(zb, 0, sizeof(zb)); }
void gxemu_save(const char *path, int out_w) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", out_w, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < out_w; x++) {
            float sx = (x + 0.5f) * W / out_w - 0.5f;
            int x0 = (int)floorf(sx); float a = sx - x0;
            if (x0 < 0) { x0 = 0; a = 0; }
            int x1 = x0 + 1 < W ? x0 + 1 : x0;
            for (int k = 0; k < 3; k++) {
                float v = disp[(y * W + x0) * 3 + k] * (1 - a) + disp[(y * W + x1) * 3 + k] * a;
                fputc((int)(fminf(1, fmaxf(0, v)) * 255 + 0.5f), f);
            }
        }
    fclose(f);
}
