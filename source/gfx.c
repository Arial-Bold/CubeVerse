/* gfx.c - GX setup, 2D primitives, bitmap-font text, photo textures and the
 * 3D plane mode. Video setup, GPU-hang recovery and the primitives come from
 * CubeBrowser (where they were proven on hardware); the 3D mode projects
 * points on the CPU, so the GPU only ever sees the same flat drawing it
 * already handles. */
#include "gfx.h"
#include "font_data.h"
#include "storage.h"
#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The ring the graphics chip reads its drawing commands from while we write
 * them. When we get too far ahead, an interrupt pauses our writing until it
 * catches up; that interrupt can be held up for a moment by the music, and
 * with the library's usual margin of 16 KB we could run right round the ring
 * and overwrite commands not yet read: the graphics chip then stops and a
 * frame is lost (a black flash). So the ring is bigger and the pause comes at
 * half full, leaving 384 KB of margin. */
#define FIFO_SIZE (768 * 1024)
#define XFB_BYTES (640 * 576 * VI_DISPLAY_PIX_SZ)

static void *fifo;
static void *xfb[2];
static int fb;
static GXRModeObj *rmode;
static bool wide;
static float vw = 640.0f;
static float margin;          /* screen margin (overscan): part of the width/height kept clear on each side */
/* 2D transform */
static float t_a = 1, t_b = 0, t_c = 0, t_d = 1, t_tx = 0, t_ty = 0;
static bool t_rotated;
/* 3D plane */
static bool p3d;
static float p_m[12], p_cx, p_cy, p_persp;
static int cur_mode = -1;     /* 0 colour, 1 text, 2 image, 3 pattern, 4 smooth image */
static bool tex_invalidate;
static int cur_font = -1;
static GXTexObj font_tex[FONT_COUNT];
static unsigned frame_verts, gpu_resets;
static volatile bool gpu_done;

#define BEGIN(prim, count) do { frame_verts += (unsigned)(count); GX_Begin(prim, GX_VTXFMT0, (u16)(count)); } while (0)

Color color_alpha(Color c, float mul) {
    float a = c.a * mul;
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    c.a = (u8)a;
    return c;
}

Color color_lerp(Color a, Color b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    Color c;
    c.r = (u8)(a.r + (b.r - a.r) * t);
    c.g = (u8)(a.g + (b.g - a.g) * t);
    c.b = (u8)(a.b + (b.b - a.b) * t);
    c.a = (u8)(a.a + (b.a - a.a) * t);
    return c;
}

/* ------------------------------------------------------------------ video */

static GXRModeObj custom_mode;
static int applied_pref = -1, applied_full = -1;

/* The GPU always draws 640 pixels per line; in full-width mode the video
 * interface stretches that to 704, the whole width of the TV picture. */
static GXRModeObj *pick_mode(int pref, bool full_width) {
    bool pal = (VIDEO_GetCurrentTvMode() == VI_PAL);
    GXRModeObj *base;
    if (pref == VMODE_480P && VIDEO_HaveComponentCable())
        base = pal ? &TVEurgb60Hz480Prog : &TVNtsc480Prog;
    else if (pref == VMODE_480I)
        base = pal ? &TVPal528IntDf : &TVNtsc480IntDf;
    else
        base = VIDEO_GetPreferredMode(NULL);
    custom_mode = *base;
    if (full_width) {
        custom_mode.viWidth = 704;
        custom_mode.viXOrigin = (720 - 704) / 2;
    }
    return &custom_mode;
}

static void setup_gx_for_mode(void) {
    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfb_h = GX_SetDispCopyYScale(yscale);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfb_h);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering,
                    (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    if (rmode->aa)
        GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
    else
        GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetDispCopyGamma(GX_GM_1_0);
}

static void configure_vi(GXRModeObj *m) {
    rmode = m;
    VIDEO_Configure(rmode);
    VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    fb = 1;
}

static void drawdone_cb(void) { gpu_done = true; }

static void apply_state(void) {
    GX_ClearVtxDesc();
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetNumTevStages(1);
}

void gfx_init(int video_mode, bool widescreen, bool full_width) {
    VIDEO_Init();
    xfb[0] = MEM_K0_TO_K1(memalign(32, XFB_BYTES));
    xfb[1] = MEM_K0_TO_K1(memalign(32, XFB_BYTES));
    configure_vi(pick_mode(video_mode, full_width));
    applied_pref = video_mode;
    applied_full = full_width;

    fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    GXFifoObj *fo = GX_Init(fifo, FIFO_SIZE);
    GX_InitFifoLimits(fo, FIFO_SIZE / 2, FIFO_SIZE / 4);   /* pause at half full, carry on at a quarter */
    GX_SetCPUFifo(fo);
    GX_SetGPFifo(fo);
    GXColor black = { 0, 0, 0, 255 };
    GX_SetCopyClear(black, 0x00FFFFFF);
    setup_gx_for_mode();
    GX_SetCullMode(GX_CULL_NONE);
    GX_CopyDisp(xfb[0], GX_TRUE);
    apply_state();
    GX_SetDrawDoneCallback(drawdone_cb);

    const unsigned char *texdata[FONT_COUNT] = { font_ui_tex, font_body_tex, font_italic_tex, font_mono_tex };
    const u32 texsize[FONT_COUNT] = { sizeof(font_ui_tex), sizeof(font_body_tex), sizeof(font_italic_tex),
                                      sizeof(font_mono_tex) };
    for (int i = 0; i < FONT_COUNT; i++) {
        DCFlushRange((void *)texdata[i], texsize[i]);
        GX_InitTexObj(&font_tex[i], (void *)texdata[i], FONT_TEX_W, FONT_TEX_H, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjLOD(&font_tex[i], GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    }
    GX_InvalidateTexAll();
    GX_InvVtxCache();
    gfx_set_widescreen(widescreen);
}

bool gfx_apply_video_mode(int video_mode, bool full_width) {
    bool ok = !(video_mode == VMODE_480P && !VIDEO_HaveComponentCable());
    if (video_mode == applied_pref && (int)full_width == applied_full) return ok;
    applied_pref = video_mode;
    applied_full = full_width;
    GXRModeObj *m = pick_mode(video_mode, full_width);
    GX_DrawDone();
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    configure_vi(m);
    setup_gx_for_mode();
    return ok;
}

void gfx_set_widescreen(bool w) {
    wide = w;
    vw = w ? 854.0f : 640.0f;
}

bool gfx_widescreen(void) { return wide; }

void gfx_set_margin(float m) { margin = m < 0 ? 0 : m > 0.15f ? 0.15f : m; }

void gfx_full_area(float *x, float *y, float *w, float *h) {
    float mx = vw * margin, my = 480.0f * margin;
    *x = -mx; *y = -my; *w = vw + 2 * mx; *h = 480 + 2 * my;
}
float gfx_width(void) { return vw; }
float gfx_height(void) { return 480.0f; }

const char *gfx_mode_name(void) {
    if (rmode->viTVMode & VI_NON_INTERLACE) return "480p";
    return rmode->xfbHeight > 480 ? "576i" : "480i";
}

/* ------------------------------------------------------------------ frame */

static void load_matrix(void) {
    Mtx m;
    guMtxIdentity(m);
    if (!p3d) {
        m[0][0] = t_a; m[0][1] = t_b; m[0][3] = t_tx;
        m[1][0] = t_c; m[1][1] = t_d; m[1][3] = t_ty;
    }
    GX_LoadPosMtxImm(m, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
}

void gfx_begin_frame(void) {
    Mtx44 proj;
    gfx_clear_base_clip();
    /* the design area (0..vw, 0..480) is shrunk into the middle of the
     * screen by the margin; backgrounds draw past it (gfx_full_area) */
    float mx = vw * margin, my = 480.0f * margin;
    guOrtho(proj, -my, 480 + my, -mx, vw + mx, 0, 300);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    p3d = false;
    gfx_reset_transform();
    gfx_clear_clip();
    cur_mode = -1;
    cur_font = -1;
    gfx_set_additive(false);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    if (tex_invalidate) { GX_InvalidateTexAll(); tex_invalidate = false; }
}

/* Masks use the depth buffer (otherwise unused). */
void gfx_mask_begin(void) { GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE); }
void gfx_mask_use(void) { GX_SetZMode(GX_TRUE, GX_EQUAL, GX_FALSE); }
void gfx_mask_end(void) { GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE); }

void gfx_set_additive(bool on) {
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, on ? GX_BL_ONE : GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

static bool gpu_wait(int ms) {
    gpu_done = false;
    GX_SetDrawDone();
    for (int i = 0; i < ms; i++) {
        if (gpu_done) return true;
        usleep(1000);
    }
    return gpu_done;
}

/* The GPU stopped responding: abort the frame and restore our state. */
static void gpu_recover(void) {
    GX_AbortFrame();
    setup_gx_for_mode();
    GX_SetCullMode(GX_CULL_NONE);
    apply_state();
    cur_mode = -1;
    cur_font = -1;
    tex_invalidate = true;
    gpu_resets++;
    char path[300];
    storage_path(path, sizeof(path), "gpu-reset.log");
    FILE *f = fopen(path, "ab");
    if (f) { fprintf(f, "GPU reset %u: %u vertices\n", gpu_resets, frame_verts); fclose(f); }
}

void gfx_end_frame(void) {
    p3d = false;
    bool ok = gpu_wait(1500);
    if (ok) {
        GX_SetColorUpdate(GX_TRUE);
        GX_CopyDisp(xfb[fb], GX_TRUE);
        ok = gpu_wait(1500);
    }
    if (!ok) {
        gpu_recover();
    } else {
        VIDEO_SetNextFramebuffer(xfb[fb]);
        VIDEO_Flush();
        fb ^= 1;
    }
    VIDEO_WaitVSync();
    frame_verts = 0;
}

void gfx_set_affine(float a, float b, float c, float d, float tx, float ty) {
    if (!isfinite(a) || !isfinite(b) || !isfinite(c) || !isfinite(d) || !isfinite(tx) || !isfinite(ty)) return;
    t_a = a; t_b = b; t_c = c; t_d = d; t_tx = tx; t_ty = ty;
    t_rotated = (b != 0 || c != 0 || a <= 0 || d <= 0);
    load_matrix();
}

void gfx_set_transform(float sx, float sy, float tx, float ty) { gfx_set_affine(sx, 0, 0, sy, tx, ty); }

void gfx_set_spin(float px, float py, float s, float deg, float mx, float my) {
    float r = deg * (float)M_PI / 180.0f, cs = cosf(r) * s, sn = sinf(r) * s;
    /* p' = R*(p - pivot) + pivot + move */
    gfx_set_affine(cs, -sn, sn, cs, px + mx - (cs * px - sn * py), py + my - (sn * px + cs * py));
}

void gfx_reset_transform(void) { gfx_set_affine(1, 0, 0, 1, 0, 0); }

void gfx_begin_3d(const float m[12], float cx, float cy, float persp) {
    memcpy(p_m, m, sizeof(p_m));
    p_cx = cx; p_cy = cy; p_persp = persp;
    p3d = true;
    load_matrix();
}

static void mode_color(void);

/* A triangle given by three points already placed in 3D (x right, y down,
 * z towards the viewer), seen in the current 3D mode's perspective. */
void gfx_tri3d(const float *p, const Color *c) {
    if (!p3d) return;
    mode_color();   /* the position matrix is already plain in 3D mode */
    BEGIN(GX_TRIANGLES, 3);
    for (int i = 0; i < 3; i++) {
        float d = p_persp - p[i * 3 + 2];
        if (d < 1.0f) d = 1.0f;
        float k = p_persp / d;
        GX_Position2f32(p_cx + p[i * 3] * k, p_cy + p[i * 3 + 1] * k);
        GX_Color4u8(c[i].r, c[i].g, c[i].b, c[i].a);
    }
    GX_End();
}

void gfx_end_3d(void) {
    p3d = false;
    load_matrix();
}

/* The base clip (a scrolling list): every other clip stays inside it, and
 * gfx_clear_clip goes back to it rather than to the whole screen. */
static int base_x, base_y, base_w = -1, base_h = -1;

static void clip_rect(float x, float y, float w, float h, int *opx, int *opy, int *opw, int *oph) {
    float x0 = x * t_a + t_tx, y0 = y * t_d + t_ty;
    float x1 = (x + w) * t_a + t_tx, y1 = (y + h) * t_d + t_ty;
    float mx = vw * margin, my = 480.0f * margin;
    float kx = rmode->fbWidth / (vw + 2 * mx), ky = rmode->efbHeight / (480.0f + 2 * my);
    int px = (int)floorf((x0 + mx) * kx), py = (int)floorf((y0 + my) * ky);
    int pw = (int)ceilf((x1 + mx) * kx) - px, ph = (int)ceilf((y1 + my) * ky) - py;
    *opx = px; *opy = py; *opw = pw; *oph = ph;
}

static int ss_cur = -1;                  /* the supersampled tile being drawn, or -1 */
static int ss_ex, ss_ey, ss_ew, ss_eh;   /* its place on the screen (framebuffer pixels) */

static void apply_scissor(int px, int py, int pw, int ph) {
    if (ss_cur >= 0) {
        /* drawing a tile at twice the size in the framebuffer's corner */
        int x0 = (px - ss_ex) * 2, y0 = (py - ss_ey) * 2, x1 = x0 + pw * 2, y1 = y0 + ph * 2;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > ss_ew * 2) x1 = ss_ew * 2;
        if (y1 > ss_eh * 2) y1 = ss_eh * 2;
        GX_SetScissor(x0, y0, x1 > x0 ? x1 - x0 : 1, y1 > y0 ? y1 - y0 : 1);
        return;
    }
    int bx = 0, by = 0, bw = rmode->fbWidth, bh = rmode->efbHeight;
    if (base_w >= 0) { bx = base_x; by = base_y; bw = base_w; bh = base_h; }
    int x0 = px > bx ? px : bx, y0 = py > by ? py : by;
    int x1 = px + pw < bx + bw ? px + pw : bx + bw, y1 = py + ph < by + bh ? py + ph : by + bh;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > rmode->fbWidth) x1 = rmode->fbWidth;
    if (y1 > rmode->efbHeight) y1 = rmode->efbHeight;
    if (x0 > rmode->fbWidth - 1) x0 = rmode->fbWidth - 1;
    if (y0 > rmode->efbHeight - 1) y0 = rmode->efbHeight - 1;
    int w = x1 - x0, h = y1 - y0;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    GX_SetScissor(x0, y0, w, h);
}

void gfx_set_base_clip(float x, float y, float w, float h) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h)) return;
    base_w = -1;
    clip_rect(x, y, w, h, &base_x, &base_y, &base_w, &base_h);
    if (base_w < 0) base_w = 0;
    apply_scissor(base_x, base_y, base_w, base_h);
}

void gfx_clear_base_clip(void) {
    base_w = base_h = -1;
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
}

void gfx_set_clip(float x, float y, float w, float h) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h)) { gfx_clear_clip(); return; }
    int px, py, pw, ph;
    clip_rect(x, y, w, h, &px, &py, &pw, &ph);
    apply_scissor(px, py, pw, ph);
}

void gfx_clear_clip(void) {
    if (ss_cur >= 0) GX_SetScissor(0, 0, ss_ew * 2, ss_eh * 2);
    else if (base_w >= 0) apply_scissor(base_x, base_y, base_w, base_h);
    else GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
}

/* ------------------------------------------------------------------ state */

static void mode_color(void) {
    if (cur_mode == 0) return;
    cur_mode = 0;
    GX_SetNumTevStages(1);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
}

static void mode_text(int font) {
    if (cur_mode != 1) {
        cur_mode = 1;
        GX_SetNumTevStages(1);
        GX_ClearVtxDesc();
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        cur_font = -1;
    }
    if (cur_font != font) {
        cur_font = font;
        GX_LoadTexObj(&font_tex[font], GX_TEXMAP0);
    }
}

/* colour = texture x vertex colour */
static void mode_image(void) {
    if (cur_mode != 2) {
        cur_mode = 2;
        GX_SetNumTevStages(1);
        GX_ClearVtxDesc();
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    }
    cur_font = -1;
}

/* A picture drawn smaller than it is: four looks into the texture, a little
 * apart, averaged. The GPU's own filtering only blends the four nearest
 * texels, so a photo shrunk to a half or a third skips texels and its fine
 * detail shimmers and stair-steps as it moves; spreading four of those
 * samples across the area each screen pixel covers smooths that out, for
 * no extra memory. (spread: how far apart, in texels.) */
static void mode_image_smooth(void) {
    if (cur_mode == 4) return;
    cur_mode = 4;
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    static const u32 mtx[4] = { GX_TEXMTX0, GX_TEXMTX1, GX_TEXMTX2, GX_TEXMTX3 };
    static const u8 coord[4] = { GX_TEXCOORD0, GX_TEXCOORD1, GX_TEXCOORD2, GX_TEXCOORD3 };
    static const u8 stage[5] = { GX_TEVSTAGE0, GX_TEVSTAGE1, GX_TEVSTAGE2, GX_TEVSTAGE3, GX_TEVSTAGE4 };
    GX_SetNumTexGens(4);
    GX_SetNumTevStages(5);
    for (int i = 0; i < 4; i++) {
        GX_SetTexCoordGen(coord[i], GX_TG_MTX2x4, GX_TG_TEX0, mtx[i]);
        /* stages 0-3: a quarter of each look, added up */
        GX_SetTevOrder(stage[i], coord[i], GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevKColorSel(stage[i], GX_TEV_KCSEL_1_4);
        GX_SetTevColorIn(stage[i], GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, i ? GX_CC_CPREV : GX_CC_ZERO);
        GX_SetTevColorOp(stage[i], GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        /* the picture's own see-through-ness, averaged the same way (the
         * logo and other cut-out pictures need it; photos are solid) */
        GX_SetTevKAlphaSel(stage[i], GX_TEV_KASEL_1_4);
        GX_SetTevAlphaIn(stage[i], GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, i ? GX_CA_APREV : GX_CA_ZERO);
        GX_SetTevAlphaOp(stage[i], GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    }
    /* stage 4: times the vertex colour (tint and fade) */
    GX_SetTevOrder(GX_TEVSTAGE4, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevColorIn(GX_TEVSTAGE4, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASC, GX_CC_ZERO);
    GX_SetTevColorOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaIn(GX_TEVSTAGE4, GX_CA_ZERO, GX_CA_APREV, GX_CA_RASA, GX_CA_ZERO);
    GX_SetTevAlphaOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    cur_font = -1;
}

static void smooth_offsets(float du, float dv) {   /* in texture coordinates (0..1) */
    static const u32 mtx[4] = { GX_TEXMTX0, GX_TEXMTX1, GX_TEXMTX2, GX_TEXMTX3 };
    static const signed char sx[4] = { -1, 1, -1, 1 }, sy[4] = { -1, -1, 1, 1 };
    for (int i = 0; i < 4; i++) {
        Mtx m;
        guMtxIdentity(m);
        m[0][3] = sx[i] * du;
        m[1][3] = sy[i] * dv;
        GX_LoadTexMtxImm(m, mtx[i], GX_MTX2x4);
    }
}

static inline bool sane(float v) { return isfinite(v) && v > -100000.0f && v < 100000.0f; }

/* Every vertex position goes through here: in 3D mode the point is placed
 * on its plane and seen in perspective (on the CPU: a handful of vertices). */
static inline void pos(float x, float y) {
    if (p3d) {
        float X = p_m[0] * x + p_m[1] * y + p_m[3];
        float Y = p_m[4] * x + p_m[5] * y + p_m[7];
        float Z = p_m[8] * x + p_m[9] * y + p_m[11];
        float d = p_persp - Z;
        if (d < 1.0f) d = 1.0f;
        float k = p_persp / d;
        x = p_cx + X * k;
        y = p_cy + Y * k;
    }
    GX_Position2f32(x, y);
}

static inline void vtx(float x, float y, Color c) {
    pos(x, y);
    GX_Color4u8(c.r, c.g, c.b, c.a);
}

/* ------------------------------------------------------------- primitives */

/* Shapes far off screen are trimmed first (the GPU mis-draws corners
 * thousands of units away). Not when rotated or in 3D (nothing there is). */
#define GUARD 64.0f
static void guard_bounds(float *x0, float *y0, float *x1, float *y1) {
    if (p3d || t_rotated) { *x0 = *y0 = -50000; *x1 = *y1 = 50000; return; }
    float sx = t_a > 0.01f ? t_a : 0.01f, sy = t_d > 0.01f ? t_d : 0.01f;
    float mx = vw * margin, my = 480.0f * margin;
    *x0 = (-GUARD - mx - t_tx) / sx;
    *x1 = (vw + GUARD + mx - t_tx) / sx;
    *y0 = (-GUARD - my - t_ty) / sy;
    *y1 = (480.0f + GUARD + my - t_ty) / sy;
}

/* The size of one screen pixel in drawing units. Lines thinner than a
 * pixel can fall between the GPU's pixel centres and vanish (the drawing
 * space is scaled to the TV by a little under or over 1), so thin lines are
 * widened to a whole pixel. */
static void pixel_units(float *ux, float *uy) {
    *ux = *uy = 0;
    if (p3d || t_rotated) return;
    float mx = vw * margin, my = 480.0f * margin;
    float kx = rmode->fbWidth / (vw + 2 * mx) * fabsf(t_a), ky = rmode->efbHeight / (480.0f + 2 * my) * fabsf(t_d);
    if (kx > 1e-4f) *ux = 1.0f / kx;
    if (ky > 1e-4f) *uy = 1.0f / ky;
}

float gfx_pixel_w(void) { float ux, uy; pixel_units(&ux, &uy); return ux > 0 ? ux : 1; }
float gfx_pixel_h(void) { float ux, uy; pixel_units(&ux, &uy); return uy > 0 ? uy : 1; }

/* Moves a coordinate onto the nearest screen pixel edge (for fine grids,
 * so every line is drawn the same). */
float gfx_snap_x(float x) {
    if (p3d || t_rotated || fabsf(t_a) < 1e-4f) return x;
    float mx = vw * margin, k = rmode->fbWidth / (vw + 2 * mx);
    float d = floorf((x * t_a + t_tx + mx) * k + 0.5f);
    return (d / k - mx - t_tx) / t_a;
}
float gfx_snap_y(float y) {
    if (p3d || t_rotated || fabsf(t_d) < 1e-4f) return y;
    float my = 480.0f * margin, k = rmode->efbHeight / (480.0f + 2 * my);
    float d = floorf((y * t_d + t_ty + my) * k + 0.5f);
    return (d / k - my - t_ty) / t_d;
}

void gfx_rect(float x, float y, float w, float h, Color c) { gfx_rect_vgrad(x, y, w, h, c, c); }

void gfx_fill_screen(Color c) {
    float x, y, w, h;
    gfx_full_area(&x, &y, &w, &h);
    gfx_rect(x, y, w, h, c);
}

void gfx_rect_vgrad(float x, float y, float w, float h, Color top, Color bottom) {
    if (!(w > 0) || !(h > 0) || !isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h)) return;
    float ux, uy;
    pixel_units(&ux, &uy);
    if (w < ux) { x -= (ux - w) * 0.5f; w = ux; }
    if (h < uy) { y -= (uy - h) * 0.5f; h = uy; }
    float gx0, gy0, gx1, gy1;
    guard_bounds(&gx0, &gy0, &gx1, &gy1);
    if (x >= gx1 || y >= gy1 || x + w <= gx0 || y + h <= gy0) return;
    if (x < gx0) { w -= gx0 - x; x = gx0; }
    if (x + w > gx1) w = gx1 - x;
    if (y < gy0 || y + h > gy1) {
        float ny0 = fmaxf(y, gy0), ny1 = fminf(y + h, gy1);
        Color nt = color_lerp(top, bottom, (ny0 - y) / h), nb = color_lerp(top, bottom, (ny1 - y) / h);
        top = nt; bottom = nb; y = ny0; h = ny1 - ny0;
    }
    mode_color();
    BEGIN(GX_QUADS, 4);
    vtx(x, y, top);
    vtx(x + w, y, top);
    vtx(x + w, y + h, bottom);
    vtx(x, y + h, bottom);
    GX_End();
}

void gfx_rect_hgrad(float x, float y, float w, float h, Color left, Color right) {
    if (!(w > 0) || !(h > 0) || !isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h)) return;
    float gx0, gy0, gx1, gy1;
    guard_bounds(&gx0, &gy0, &gx1, &gy1);
    if (x >= gx1 || y >= gy1 || x + w <= gx0 || y + h <= gy0) return;
    if (y < gy0) { h -= gy0 - y; y = gy0; }
    if (y + h > gy1) h = gy1 - y;
    if (x < gx0 || x + w > gx1) {
        float nx0 = fmaxf(x, gx0), nx1 = fminf(x + w, gx1);
        Color nl = color_lerp(left, right, (nx0 - x) / w), nr = color_lerp(left, right, (nx1 - x) / w);
        left = nl; right = nr; x = nx0; w = nx1 - nx0;
    }
    mode_color();
    BEGIN(GX_QUADS, 4);
    vtx(x, y, left);
    vtx(x + w, y, right);
    vtx(x + w, y + h, right);
    vtx(x, y + h, left);
    GX_End();
}

#define RR_SEG 6
#define RR_PTS (4 * (RR_SEG + 1))

static int rr_points(float x, float y, float w, float h, float r, float *px, float *py) {
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0) r = 0;
    const float cx[4] = { x + w - r, x + w - r, x + r, x + r };
    const float cy[4] = { y + r, y + h - r, y + h - r, y + r };
    int n = 0;
    for (int c = 0; c < 4; c++) {
        float a0 = (-90.0f + 90.0f * c) * (float)M_PI / 180.0f;
        for (int i = 0; i <= RR_SEG; i++) {
            float a = a0 + (float)M_PI * 0.5f * i / RR_SEG;
            px[n] = cx[c] + cosf(a) * r;
            py[n] = cy[c] + sinf(a) * r;
            n++;
        }
    }
    return n;
}

static bool guard_rrect(float *x, float *y, float *w, float *h, float r, Color *top, Color *bottom) {
    float gx0, gy0, gx1, gy1;
    guard_bounds(&gx0, &gy0, &gx1, &gy1);
    float pad = (r > 0 ? r : 0) + 2;
    gx0 -= pad; gy0 -= pad; gx1 += pad; gy1 += pad;
    if (*x >= gx1 || *y >= gy1 || *x + *w <= gx0 || *y + *h <= gy0) return false;
    if (*x < gx0) { *w -= gx0 - *x; *x = gx0; }
    if (*x + *w > gx1) *w = gx1 - *x;
    if (*y < gy0 || *y + *h > gy1) {
        float ny0 = fmaxf(*y, gy0), ny1 = fminf(*y + *h, gy1);
        if (top && bottom) {
            Color nt = color_lerp(*top, *bottom, (ny0 - *y) / *h), nb = color_lerp(*top, *bottom, (ny1 - *y) / *h);
            *top = nt; *bottom = nb;
        }
        *y = ny0; *h = ny1 - ny0;
    }
    return *w > 0 && *h > 0;
}

void gfx_rrect_vgrad(float x, float y, float w, float h, float r, Color top, Color bottom) {
    if (!(w > 0) || !(h > 0) || !isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || !isfinite(r)) return;
    if (!guard_rrect(&x, &y, &w, &h, r, &top, &bottom)) return;
    float px[RR_PTS], py[RR_PTS];
    int n = rr_points(x, y, w, h, r, px, py);
    mode_color();
    BEGIN(GX_TRIANGLEFAN, n + 2);
    vtx(x + w * 0.5f, y + h * 0.5f, color_lerp(top, bottom, 0.5f));
    for (int i = 0; i <= n; i++) {
        int k = i % n;
        vtx(px[k], py[k], color_lerp(top, bottom, (py[k] - y) / h));
    }
    GX_End();
}

void gfx_rrect(float x, float y, float w, float h, float r, Color c) { gfx_rrect_vgrad(x, y, w, h, r, c, c); }

/* A rounded rectangle shaded top -> middle -> bottom, drawn in one piece
 * (two separate see-through halves would double up where they meet). */
void gfx_rrect_vgrad3(float x, float y, float w, float h, float r, Color top, Color mid, Color bottom) {
    if (!(w > 0) || !(h > 0) || !isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || !isfinite(r)) return;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0) r = 0;
    enum { CS = 4 };
    float ys[2 * CS + 4], in[2 * CS + 4];
    int n = 0;
    for (int i = 0; i <= CS; i++) {          /* down the top corners */
        float a = (float)M_PI * 0.5f * (1 - (float)i / CS);
        ys[n] = y + r - r * sinf(a);
        in[n++] = r - r * cosf(a);
    }
    ys[n] = y + h * 0.5f; in[n++] = 0;       /* the middle colour stop */
    for (int i = 0; i <= CS; i++) {          /* down the bottom corners */
        float a = (float)M_PI * 0.5f * (float)i / CS;
        ys[n] = y + h - r + r * sinf(a);
        in[n++] = r - r * cosf(a);
    }
    /* keep the middle stop in order if the corners pass it */
    for (int i = 1; i < n; i++) if (ys[i] < ys[i - 1]) ys[i] = ys[i - 1];
    mode_color();
    BEGIN(GX_TRIANGLESTRIP, n * 2);
    for (int i = 0; i < n; i++) {
        float t = (ys[i] - y) / h;
        Color c = t < 0.5f ? color_lerp(top, mid, t * 2) : color_lerp(mid, bottom, t * 2 - 1);
        vtx(x + in[i], ys[i], c);
        vtx(x + w - in[i], ys[i], c);
    }
    GX_End();
}

void gfx_rrect_outline(float x, float y, float w, float h, float r, float t, Color c) {
    if (!(w > 0) || !(h > 0) || !isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || !isfinite(t)) return;
    if (!guard_rrect(&x, &y, &w, &h, r + t, NULL, NULL)) return;
    float ux, uy;
    pixel_units(&ux, &uy);
    float tx = t < ux ? ux : t, ty = t < uy ? uy : t;   /* never thinner than a pixel */
    if (2 * tx >= w || 2 * ty >= h) { gfx_rrect(x, y, w, h, r, c); return; }
    float ox[RR_PTS], oy[RR_PTS], ix[RR_PTS], iy[RR_PTS];
    int n = rr_points(x, y, w, h, r, ox, oy);
    float ri = r - (tx > ty ? tx : ty);
    rr_points(x + tx, y + ty, w - 2 * tx, h - 2 * ty, ri > 0 ? ri : 0, ix, iy);
    mode_color();
    BEGIN(GX_TRIANGLESTRIP, (n + 1) * 2);
    for (int i = 0; i <= n; i++) {
        int k = i % n;
        vtx(ox[k], oy[k], c);
        vtx(ix[k], iy[k], c);
    }
    GX_End();
}

void gfx_shadow(float x, float y, float w, float h, float r, float spread, Color c) {
    for (int i = 4; i >= 1; i--) {
        float s = spread * i / 4.0f;
        gfx_rrect(x - s, y - s, w + 2 * s, h + 2 * s, r + s, color_alpha(c, 0.28f));
    }
}

void gfx_circle(float cx, float cy, float r, Color c) {
    if (!sane(cx) || !sane(cy) || !sane(r)) return;
    const int N = 24;
    mode_color();
    BEGIN(GX_TRIANGLEFAN, N + 2);
    vtx(cx, cy, c);
    for (int i = 0; i <= N; i++) {
        float a = 2.0f * (float)M_PI * i / N;
        vtx(cx + cosf(a) * r, cy + sinf(a) * r, c);
    }
    GX_End();
}

void gfx_ring(float cx, float cy, float r, float t, float a0, float a1, Color c) {
    if (!sane(cx) || !sane(cy) || !sane(r) || !sane(t) || !sane(a0) || !sane(a1)) return;
    float ux, uy;
    pixel_units(&ux, &uy);
    if (t < ux) t = ux;
    if (t < uy) t = uy;
    const int N = 24;
    mode_color();
    BEGIN(GX_TRIANGLESTRIP, (N + 1) * 2);
    for (int i = 0; i <= N; i++) {
        float a = a0 + (a1 - a0) * i / N;
        vtx(cx + cosf(a) * r, cy + sinf(a) * r, c);
        vtx(cx + cosf(a) * (r - t), cy + sinf(a) * (r - t), c);
    }
    GX_End();
}

void gfx_tri(float x1, float y1, float x2, float y2, float x3, float y3, Color c) {
    if (!sane(x1) || !sane(y1) || !sane(x2) || !sane(y2) || !sane(x3) || !sane(y3)) return;
    mode_color();
    BEGIN(GX_TRIANGLES, 3);
    vtx(x1, y1, c);
    vtx(x2, y2, c);
    vtx(x3, y3, c);
    GX_End();
}

void gfx_quad(const float *p, Color c) {
    for (int i = 0; i < 8; i++) if (!sane(p[i])) return;
    mode_color();
    BEGIN(GX_QUADS, 4);
    for (int i = 0; i < 4; i++) vtx(p[i * 2], p[i * 2 + 1], c);
    GX_End();
}

void gfx_quad_colors(const float *p, const Color *c) {
    for (int i = 0; i < 8; i++) if (!sane(p[i])) return;
    mode_color();
    BEGIN(GX_QUADS, 4);
    for (int i = 0; i < 4; i++) vtx(p[i * 2], p[i * 2 + 1], c[i]);
    GX_End();
}

void gfx_line(float x1, float y1, float x2, float y2, float t, Color c) {
    if (!sane(x1) || !sane(y1) || !sane(x2) || !sane(y2) || !sane(t)) return;
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    float q[8] = { x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny };
    gfx_quad(q, c);
}

void gfx_mesh(int cols, int rows, const float *xy, const Color *c) {
    if (cols < 1 || rows < 1 || cols > 64 || rows > 64) return;
    int n = (cols + 1) * (rows + 1);
    for (int i = 0; i < n * 2; i++) if (!sane(xy[i])) return;
    mode_color();
    for (int r = 0; r < rows; r++) {
        BEGIN(GX_TRIANGLESTRIP, (cols + 1) * 2);
        for (int k = 0; k <= cols; k++)
            for (int d = 0; d < 2; d++) {
                int i = (r + d) * (cols + 1) + k;
                vtx(xy[i * 2], xy[i * 2 + 1], c[i]);
            }
        GX_End();
    }
}

/* ------------------------------------------------------------------- text */

static const FontGlyph *glyph_table(FontId f) {
    switch (f) {
    case FONT_CUBE: return font_ui_glyphs;
    case FONT_ROUND: return font_italic_glyphs;
    case FONT_MENU_BOLD: return font_mono_glyphs;
    default: return font_body_glyphs;
    }
}
static float font_px(FontId f) {
    switch (f) {
    case FONT_CUBE: return FONT_UI_PX;
    case FONT_ROUND: return FONT_ITALIC_PX;
    case FONT_MENU_BOLD: return FONT_MONO_PX;
    default: return FONT_BODY_PX;
    }
}
static float font_line(FontId f) {
    switch (f) {
    case FONT_CUBE: return FONT_UI_LINE;
    case FONT_ROUND: return FONT_ITALIC_LINE;
    case FONT_MENU_BOLD: return FONT_MONO_LINE;
    default: return FONT_BODY_LINE;
    }
}

/* ASCII, then the extra characters (see ui.h: middle dot, times, arrows...) */
#define GLYPH_LAST (126 + 6)
static inline const FontGlyph *glyph_for(const FontGlyph *G, unsigned char ch) {
    if (ch < 32 || ch > GLYPH_LAST) ch = '?';
    return &G[ch - 32];
}

float gfx_line_height(FontId f, float size) { return font_line(f) * size / font_px(f); }

float gfx_cap_top(FontId f, float size) {
    const FontGlyph *g = glyph_for(glyph_table(f), 'H');
    return g->yoff * size / font_px(f);
}

float gfx_cap_height(FontId f, float size) {
    const FontGlyph *g = glyph_for(glyph_table(f), 'H');
    return g->h * size / font_px(f);
}

float gfx_text_width_sp(FontId f, float size, float spacing, const char *s, int len) {
    if (len < 0) len = (int)strlen(s);
    const FontGlyph *G = glyph_table(f);
    float w = 0;
    for (int i = 0; i < len; i++) w += glyph_for(G, (unsigned char)s[i])->adv;
    w = w * size / font_px(f);
    if (len > 1) w += spacing * (len - 1);
    return w;
}

float gfx_text_width(FontId f, float size, const char *s, int len) { return gfx_text_width_sp(f, size, 0, s, len); }

int gfx_text_fit(FontId f, float size, const char *s, int len, float maxw) {
    if (len < 0) len = (int)strlen(s);
    const FontGlyph *G = glyph_table(f);
    float sc = size / font_px(f), w = 0;
    for (int i = 0; i < len; i++) {
        float a = glyph_for(G, (unsigned char)s[i])->adv * sc;
        if (w + a * 0.5f > maxw) return i;
        w += a;
    }
    return len;
}

float gfx_text_sp(FontId f, float size, float spacing, float x, float y, Color c, const char *s, int len) {
    if (len < 0) len = (int)strlen(s);
    if (len > 2000) len = 2000;
    if (!isfinite(x) || !isfinite(y) || !isfinite(size) || size <= 0 || size > 400) return 0;
    const FontGlyph *G = glyph_table(f);
    float sc = size / font_px(f);
    int n = 0;
    for (int i = 0; i < len; i++)
        if (glyph_for(G, (unsigned char)s[i])->w) n++;
    float cx = x;
    if (n > 0) {
        mode_text(f);
        const float iw = 1.0f / FONT_TEX_W, ih = 1.0f / FONT_TEX_H;
        BEGIN(GX_QUADS, n * 4);
        for (int i = 0; i < len; i++) {
            const FontGlyph *g = glyph_for(G, (unsigned char)s[i]);
            if (g->w) {
                float x0 = cx + g->xoff * sc, y0 = y + g->yoff * sc;
                float x1 = x0 + g->w * sc, y1 = y0 + g->h * sc;
                float u0 = g->x * iw, v0 = g->y * ih, u1 = (g->x + g->w) * iw, v1 = (g->y + g->h) * ih;
                pos(x0, y0); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u0, v0);
                pos(x1, y0); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u1, v0);
                pos(x1, y1); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u1, v1);
                pos(x0, y1); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u0, v1);
            }
            cx += g->adv * sc + spacing;
        }
        GX_End();
    } else {
        for (int i = 0; i < len; i++) cx += glyph_for(G, (unsigned char)s[i])->adv * sc + spacing;
    }
    return len > 0 ? cx - x - spacing : 0;
}

float gfx_text(FontId f, float size, float x, float y, Color c, const char *s, int len) {
    return gfx_text_sp(f, size, 0, x, y, c, s, len);
}

void gfx_text_centered(FontId f, float size, float cx, float y, Color c, const char *s) {
    float w = gfx_text_width(f, size, s, -1);
    gfx_text(f, size, cx - w * 0.5f, y, c, s, -1);
}

/* ----------------------------------------------------------------- images */

bool gfx_image_init(GfxImage *img, void *data, int w, int h, int tex_w, int tex_h, int fmt) {
    img->data = NULL;
    if (!data || w <= 0 || h <= 0 || tex_w <= 0 || tex_h <= 0 || tex_w > 1024 || tex_h > 1024 ||
        (tex_w & 3) || (tex_h & 3) || ((unsigned long)data & 31) || w > tex_w || h > tex_h) {
        free(data);
        return false;
    }
    img->data = data;
    img->w = (u16)w; img->h = (u16)h; img->tex_w = (u16)tex_w; img->tex_h = (u16)tex_h;
    img->fmt = (u8)fmt;
    img->repeat = false;
    DCFlushRange(data, (u32)tex_w * tex_h * 2);
    GX_InitTexObj(&img->obj, data, (u16)tex_w, (u16)tex_h, fmt == GFX_RGB5A3 ? GX_TF_RGB5A3 : GX_TF_RGB565,
                  GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&img->obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    tex_invalidate = true;
    return true;
}

void gfx_image_touch(GfxImage *img) {
    if (!img->data) return;
    DCFlushRange(img->data, (u32)img->tex_w * img->tex_h * 2);
    tex_invalidate = true;
}

void gfx_image_free(GfxImage *img) {
    free(img->data);
    img->data = NULL;
}

/* Where a point lands on the screen, in pixels (for working out how much
 * a picture is being shrunk). */
static void screen_point(float x, float y, float *sx, float *sy) {
    if (p3d) {
        float X = p_m[0] * x + p_m[1] * y + p_m[3];
        float Y = p_m[4] * x + p_m[5] * y + p_m[7];
        float Z = p_m[8] * x + p_m[9] * y + p_m[11];
        float d = p_persp - Z;
        if (d < 1.0f) d = 1.0f;
        float k = p_persp / d;
        x = p_cx + X * k;
        y = p_cy + Y * k;
    } else {
        float x2 = t_a * x + t_b * y + t_tx;
        y = t_c * x + t_d * y + t_ty;
        x = x2;
    }
    float mx = vw * margin, my = 480.0f * margin;
    *sx = x * rmode->fbWidth / (vw + 2 * mx);
    *sy = y * rmode->efbHeight / (480.0f + 2 * my);
}

/* Texels per screen pixel along the quad's sides (the most of them). */
static float shrink_of(const float *p, const float *uv) {
    float s[8], most = 0;
    for (int i = 0; i < 4; i++) screen_point(p[i * 2], p[i * 2 + 1], &s[i * 2], &s[i * 2 + 1]);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        float px = s[j * 2] - s[i * 2], py = s[j * 2 + 1] - s[i * 2 + 1];
        float tx = uv[j * 2] - uv[i * 2], ty = uv[j * 2 + 1] - uv[i * 2 + 1];
        float pl = px * px + py * py, tl = tx * tx + ty * ty;
        if (pl < 1.0f) continue;
        float k = sqrtf(tl / pl);
        if (k > most) most = k;
    }
    return most;
}

void gfx_image_quad(GfxImage *img, const float *p, const float *uv, Color c) {
    if (!img->data || !img->tex_w || !img->tex_h) return;
    for (int i = 0; i < 8; i++) if (!sane(p[i]) || !isfinite(uv[i])) return;
    float shrink = shrink_of(p, uv);
    if (shrink > 1.15f) {
        /* the four looks a quarter of a screen pixel's width from its middle
         * each way (up to 6 texels across: smaller than that it is a thumbnail
         * already, and the eye doesn't catch it) */
        float spread = 0.25f * (shrink < 6.0f ? shrink : 6.0f);
        mode_image_smooth();
        smooth_offsets(spread / img->tex_w, spread / img->tex_h);
    } else {
        mode_image();
    }
    GX_LoadTexObj(&img->obj, GX_TEXMAP0);
    float iu = 1.0f / img->tex_w, iv = 1.0f / img->tex_h;
    BEGIN(GX_QUADS, 4);
    for (int i = 0; i < 4; i++) {
        pos(p[i * 2], p[i * 2 + 1]);
        GX_Color4u8(c.r, c.g, c.b, c.a);
        GX_TexCoord2f32(uv[i * 2] * iu, uv[i * 2 + 1] * iv);
    }
    GX_End();
}

void gfx_image_draw_part(GfxImage *img, float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1, Color c) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || w <= 0 || h <= 0) return;
    float p[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
    float uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
    gfx_image_quad(img, p, uv, c);
}

void gfx_image_draw(GfxImage *img, float x, float y, float w, float h, Color c) {
    if (!img->data) return;
    gfx_image_draw_part(img, x, y, w, h, 0, 0, img->w, img->h, c);
}

/* ---------------------------------------------------------- supersampling */

/* Antialiasing for a part of the screen: each tile of it is drawn at twice
 * the size in the framebuffer's top-left corner, then copied out to a
 * texture at the normal size with the GPU's copy filter, which averages
 * every 2x2 pixels (four samples a pixel). The tiles are drawn before the
 * frame itself (each copy clears the corner again), then pasted in place. */
#define SS_MAX 8
#define SS_TW 320            /* the biggest tile: twice it fills the framebuffer */
#define SS_TH 260
typedef struct { int ex, ey, ew, eh; void *buf; size_t cap; GXTexObj obj; bool ok; } SsTile;
static SsTile ss[SS_MAX];
static int ss_n;

static void efb_k(float *kx, float *ky, float *mx, float *my) {
    *mx = vw * margin; *my = 480.0f * margin;
    *kx = rmode->fbWidth / (vw + 2 * *mx);
    *ky = rmode->efbHeight / (480.0f + 2 * *my);
}

int gfx_ss_plan(float x, float y, float w, float h) {
    ss_n = 0;
#ifdef PC_TEST
    (void)x; (void)y; (void)w; (void)h;
    return 0;                /* the PC test build has no framebuffer copies */
#else
    if (!isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || w <= 0 || h <= 0) return 0;
    float kx, ky, mx, my;
    efb_k(&kx, &ky, &mx, &my);
    int x0 = (int)floorf((x + mx) * kx), y0 = (int)floorf((y + my) * ky);
    int x1 = (int)ceilf((x + w + mx) * kx), y1 = (int)ceilf((y + h + my) * ky);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > rmode->fbWidth) x1 = rmode->fbWidth;
    if (y1 > rmode->efbHeight) y1 = rmode->efbHeight;
    x0 &= ~3; y0 &= ~3;      /* on the 4-pixel grid */
    x1 = (x1 + 3) & ~3; y1 = (y1 + 3) & ~3;
    if (x1 - x0 < 8 || y1 - y0 < 8) return 0;
    int nx = (x1 - x0 + SS_TW - 1) / SS_TW, ny = (y1 - y0 + SS_TH - 1) / SS_TH;
    if (nx * ny > SS_MAX) return 0;
    int tw = (((x1 - x0) + nx - 1) / nx + 3) & ~3, th = (((y1 - y0) + ny - 1) / ny + 3) & ~3;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            SsTile *t = &ss[ss_n];
            t->ex = x0 + i * tw;
            t->ey = y0 + j * th;
            t->ew = x0 + (i + 1) * tw > x1 ? x1 - t->ex : tw;
            t->eh = y0 + (j + 1) * th > y1 ? y1 - t->ey : th;
            if (t->ew < 4 || t->eh < 4) continue;
            size_t need = GX_GetTexBufferSize((u16)t->ew, (u16)t->eh, GX_TF_RGBA8, GX_FALSE, 0);
            if (need > t->cap) {
                free(t->buf);
                t->buf = memalign(32, need);
                t->cap = t->buf ? need : 0;
            }
            if (!t->buf) return ss_n = 0;
            t->ok = false;
            ss_n++;
        }
    return ss_n;
#endif
}

void gfx_ss_begin(int i) {
#ifndef PC_TEST
    if (i < 0 || i >= ss_n) return;
    SsTile *t = &ss[i];
    float kx, ky, mx, my;
    efb_k(&kx, &ky, &mx, &my);
    /* this tile's part of the design area fills a framebuffer area twice its size */
    float l = t->ex / kx - mx, r = (t->ex + t->ew) / kx - mx, top = t->ey / ky - my, bot = (t->ey + t->eh) / ky - my;
    Mtx44 proj;
    guOrtho(proj, top, bot, l, r, 0, 300);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    GX_SetViewport(0, 0, t->ew * 2, t->eh * 2, 0, 1);
    ss_cur = i;
    ss_ex = t->ex; ss_ey = t->ey; ss_ew = t->ew; ss_eh = t->eh;
    GX_SetScissor(0, 0, t->ew * 2, t->eh * 2);
#else
    (void)i;
#endif
}

void gfx_ss_end(int i) {
#ifndef PC_TEST
    if (i < 0 || i >= ss_n || ss_cur != i) return;
    SsTile *t = &ss[i];
    p3d = false;
    GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);   /* no deflicker blur on the copy */
    GX_SetTexCopySrc(0, 0, t->ew * 2, t->eh * 2);
    GX_SetTexCopyDst((u16)t->ew, (u16)t->eh, GX_TF_RGBA8, GX_TRUE);   /* GX_TRUE: halve it, averaging 2x2 */
    GX_CopyTex(t->buf, GX_TRUE);
    GX_PixModeSync();
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_InitTexObj(&t->obj, t->buf, (u16)t->ew, (u16)t->eh, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&t->obj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    t->ok = true;
    ss_cur = -1;
#else
    (void)i;
#endif
}

void gfx_ss_finish(void) {
#ifndef PC_TEST
    ss_cur = -1;
    float mx = vw * margin, my = 480.0f * margin;
    Mtx44 proj;
    guOrtho(proj, -my, 480 + my, -mx, vw + mx, 0, 300);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    gfx_clear_clip();
    GX_InvalidateTexAll();   /* the tiles were just written */
#endif
}

void gfx_ss_draw(float al) {
#ifndef PC_TEST
    if (al <= 0) return;
    float kx, ky, mx, my;
    efb_k(&kx, &ky, &mx, &my);
    bool p = p3d;
    p3d = false;
    for (int i = 0; i < ss_n; i++) {
        SsTile *t = &ss[i];
        if (!t->ok) continue;
        mode_image();
        GX_LoadTexObj(&t->obj, GX_TEXMAP0);
        float x0 = t->ex / kx - mx, y0 = t->ey / ky - my, x1 = (t->ex + t->ew) / kx - mx, y1 = (t->ey + t->eh) / ky - my;
        BEGIN(GX_QUADS, 4);
        vtx(x0, y0, rgba(255, 255, 255, (u8)(255 * al))); GX_TexCoord2f32(0, 0);
        vtx(x1, y0, rgba(255, 255, 255, (u8)(255 * al))); GX_TexCoord2f32(1, 0);
        vtx(x1, y1, rgba(255, 255, 255, (u8)(255 * al))); GX_TexCoord2f32(1, 1);
        vtx(x0, y1, rgba(255, 255, 255, (u8)(255 * al))); GX_TexCoord2f32(0, 1);
        GX_End();
    }
    p3d = p;
#else
    (void)al;
#endif
}

/* --------------------------------------------------------------- patterns */

bool gfx_pattern_init(GfxPattern *pt, const unsigned char *data, int w, int h) {
    pt->data = NULL;
    if (w < 8 || h < 4 || w > 256 || h > 256 || (w & (w - 1)) || (h & (h - 1))) return false;
    unsigned char *t = memalign(32, (size_t)w * h);
    if (!t) return false;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            t[((y >> 2) * (w >> 3) + (x >> 3)) * 32 + (y & 3) * 8 + (x & 7)] = data[y * w + x];
    DCFlushRange(t, (u32)(w * h));
    pt->data = t;
    pt->w = (u16)w;
    pt->h = (u16)h;
    GX_InitTexObj(&pt->obj, t, (u16)w, (u16)h, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_FALSE);
    GX_InitTexObjLOD(&pt->obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    tex_invalidate = true;
    return true;
}

void gfx_pattern_free(GfxPattern *pt) {
    free(pt->data);
    pt->data = NULL;
}

/* The pattern as a coverage mask: colour c, alpha c.a x pattern. */
void gfx_pattern_rect(GfxPattern *pt, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color c) {
    if (!pt->data || !sane(x) || !sane(y) || !sane(w) || !sane(h)) return;
    if (cur_mode != 3) {
        cur_mode = 3;
        GX_SetNumTevStages(1);
        GX_ClearVtxDesc();
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        /* same combiner as text: vertex colour, alpha = vertex alpha x texel */
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        cur_font = -1;
    }
    GX_LoadTexObj(&pt->obj, GX_TEXMAP0);
    BEGIN(GX_QUADS, 4);
    pos(x, y);         GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u0, v0);
    pos(x + w, y);     GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u1, v0);
    pos(x + w, y + h); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u1, v1);
    pos(x, y + h);     GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u0, v1);
    GX_End();
}
