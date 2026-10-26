/* PC stand-in for libogc's gccore.h: just enough of GX/VIDEO for gfx.c to
 * run on a PC, drawing into a picture (gxemu.c). Test builds only. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef float f32;
typedef float Mtx[3][4];
typedef float Mtx44[4][4];
#define FALSE 0
#define TRUE 1
#define GX_FALSE 0
#define GX_TRUE 1
#define GX_ENABLE 1
#define GX_DISABLE 0
typedef struct { u8 r, g, b, a; } GXColor;
typedef struct { const void *data; u16 w, h; u8 fmt; u8 wrap; } GXTexObj;
typedef struct {
    u32 viTVMode; u16 fbWidth, efbHeight, xfbHeight, viXOrigin, viYOrigin, viWidth, viHeight;
    u32 xfbMode; u8 field_rendering, aa; u8 sample_pattern[12][2]; u8 vfilter[7];
} GXRModeObj;
#define VI_NON_INTERLACE 1
#define VI_PAL 1
#define VI_DISPLAY_PIX_SZ 2
#define COLOR_BLACK 0
#define MEM_K0_TO_K1(x) (x)
extern GXRModeObj TVNtsc480IntDf, TVNtsc480Prog, TVEurgb60Hz480Prog, TVPal528IntDf;
static inline void DCFlushRange(void *a, u32 n) { (void)a; (void)n; }
static inline void DCInvalidateRange(void *a, u32 n) { (void)a; (void)n; }
void VIDEO_Init(void); void VIDEO_Configure(GXRModeObj *m); void VIDEO_ClearFrameBuffer(GXRModeObj *m, void *fb, u32 c);
void VIDEO_SetNextFramebuffer(void *fb); void VIDEO_SetBlack(int b); void VIDEO_Flush(void); void VIDEO_WaitVSync(void);
GXRModeObj *VIDEO_GetPreferredMode(void *x); int VIDEO_GetCurrentTvMode(void); int VIDEO_HaveComponentCable(void);
enum { GX_VTXFMT0, GX_VA_POS, GX_VA_CLR0, GX_VA_TEX0, GX_POS_XY, GX_F32, GX_CLR_RGBA, GX_RGBA8, GX_TEX_ST, GX_DIRECT };
enum { GX_QUADS = 0x80, GX_TRIANGLES = 0x90, GX_TRIANGLESTRIP = 0x98, GX_TRIANGLEFAN = 0xA0 };
enum { GX_COLOR0A0, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE };
enum { GX_ALWAYS, GX_EQUAL };
enum { GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_BL_ONE, GX_LO_CLEAR };
enum { GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXCOORDNULL, GX_TEXMAP0, GX_TEXMAP_NULL, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY };
enum { GX_PASSCLR = 100, GX_MODULATE = 101 };
enum { GX_TEVSTAGE1 = 201, GX_TEVSTAGE2, GX_TEVSTAGE3, GX_TEVSTAGE4, GX_TEXCOORD1 = 211, GX_TEXCOORD2, GX_TEXCOORD3,
       GX_CC_TEXC = 221, GX_CC_CPREV, GX_CC_KONST, GX_TEV_KCSEL_1_4 = 231, GX_MTX2x4 = 0 };
enum { GX_TEXMTX0 = 30, GX_TEXMTX1 = 33, GX_TEXMTX2 = 36, GX_TEXMTX3 = 39 };
void GX_SetTevKColorSel(u8 stage, u8 sel); void GX_LoadTexMtxImm(float m[3][4], u32 id, u8 type);
enum { GX_CC_ZERO, GX_CC_RASC, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TEVPREV };
enum { GX_TF_I4 = 0, GX_TF_I8 = 1, GX_TF_RGB565 = 4, GX_TF_RGB5A3 = 5 };
enum { GX_CLAMP = 0, GX_REPEAT = 1 };
enum { GX_LINEAR, GX_NEAR = 100, GX_ANISO_1, GX_PF_RGB565_Z16, GX_PF_RGB8_Z24, GX_ZC_LINEAR, GX_GM_1_0, GX_CULL_NONE, GX_PNMTX0, GX_ORTHOGRAPHIC };
typedef struct { int dummy; } GXFifoObj;
GXFifoObj *GX_Init(void *fifo, u32 size); void GX_InitFifoLimits(GXFifoObj *f, u32 hi, u32 lo); void GX_SetCPUFifo(GXFifoObj *f); void GX_SetGPFifo(GXFifoObj *f); void GX_SetCopyClear(GXColor c, u32 z);
f32 GX_GetYScaleFactor(u16 a, u16 b); u32 GX_SetDispCopyYScale(f32 s);
void GX_SetViewport(f32 x, f32 y, f32 w, f32 h, f32 n, f32 f); void GX_SetScissor(u32 x, u32 y, u32 w, u32 h);
void GX_SetDispCopySrc(u16 a, u16 b, u16 c, u16 d); void GX_SetDispCopyDst(u16 a, u16 b);
void GX_SetCopyFilter(u8 aa, u8 sp[12][2], u8 vf, u8 *vfilter); void GX_SetFieldMode(u8 a, u8 b);
void GX_SetPixelFmt(u8 a, u8 b); void GX_SetDispCopyGamma(u8 g); void GX_SetCullMode(u8 m);
void GX_CopyDisp(void *fb, u8 clear); void GX_SetDrawDoneCallback(void (*cb)(void)); void GX_SetDrawDone(void);
void GX_DrawDone(void); void GX_AbortFrame(void); void GX_InvalidateTexAll(void); void GX_InvVtxCache(void);
void GX_ClearVtxDesc(void); void GX_SetVtxAttrFmt(u8 a, u32 b, u32 c, u32 d, u8 e); void GX_SetVtxDesc(u8 a, u8 b);
void GX_SetNumChans(u8 n); void GX_SetChanCtrl(int a, int b, int c, int d, int e, int f, int g);
void GX_SetZMode(u8 en, u8 fn, u8 upd); void GX_SetBlendMode(u8 t, u8 s, u8 d, u8 o);
void GX_SetAlphaUpdate(u8 e); void GX_SetColorUpdate(u8 e); void GX_SetNumTevStages(u8 n); void GX_SetNumTexGens(u32 n);
void GX_SetTevOrder(u8 s, u8 c, u32 m, u8 col); void GX_SetTevOp(u8 s, u8 mode);
void GX_SetTexCoordGen(u16 a, u32 b, u32 c, u32 d);
void GX_SetTevColorIn(u8 s, u8 a, u8 b, u8 c, u8 d); void GX_SetTevColorOp(u8 s, u8 a, u8 b, u8 c, u8 d, u8 e);
void GX_SetTevAlphaIn(u8 s, u8 a, u8 b, u8 c, u8 d); void GX_SetTevAlphaOp(u8 s, u8 a, u8 b, u8 c, u8 d, u8 e);
void GX_InitTexObj(GXTexObj *o, void *data, u16 w, u16 h, u8 fmt, u8 ws, u8 wt, u8 mip);
void GX_InitTexObjLOD(GXTexObj *o, int a, int b, f32 c, f32 d, f32 e, u8 f, u8 g, int h);
void GX_LoadTexObj(GXTexObj *o, u8 map);
void GX_LoadPosMtxImm(Mtx m, u32 id); void GX_SetCurrentMtx(u32 id); void GX_LoadProjectionMtx(Mtx44 m, u8 type);
void GX_Begin(u8 prim, u8 fmt, u16 n); void GX_End(void);
void GX_Position2f32(f32 x, f32 y); void GX_Color4u8(u8 r, u8 g, u8 b, u8 a); void GX_TexCoord2f32(f32 s, f32 t);
void guMtxIdentity(Mtx m); void guOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f);
/* test helpers */
void gxemu_save(const char *path, int out_w);   /* writes a PPM; out_w 640 or 854 (stretched) */
