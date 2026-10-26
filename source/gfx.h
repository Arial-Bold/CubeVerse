/* gfx.h - video setup, 2D drawing, text, photo textures, and a simple 3D
 * mode (for the menu cube) on the GameCube's GX graphics chip.
 *
 * Taken from CubeBrowser's gfx and extended. All drawing uses a *virtual*
 * coordinate space that is always 480 units tall: 640 wide in 4:3, 854 in
 * 16:9 (squeezed into 640 pixels; the TV stretches it back).
 */
#pragma once
#include <gccore.h>
#include <stdbool.h>

typedef struct { u8 r, g, b, a; } Color;

static inline Color rgba(u8 r, u8 g, u8 b, u8 a) { Color c = { r, g, b, a }; return c; }
Color color_alpha(Color c, float mul);
Color color_lerp(Color a, Color b, float t);

/* FONT_CUBE: rounded extra-bold (cube labels, logo); FONT_MENU: the system
 * menu font; FONT_ROUND: rounded bold (small cube text); FONT_MENU_BOLD. */
typedef enum { FONT_CUBE = 0, FONT_MENU = 1, FONT_ROUND = 2, FONT_MENU_BOLD = 3, FONT_COUNT } FontId;

enum { VMODE_AUTO = 0, VMODE_480I = 1, VMODE_480P = 2 };

void  gfx_init(int video_mode, bool widescreen, bool full_width);
bool  gfx_apply_video_mode(int video_mode, bool full_width);
void  gfx_set_widescreen(bool wide);
bool  gfx_widescreen(void);
float gfx_width(void);   /* 640 or 854 */
float gfx_height(void);  /* 480 */
/* Screen margin for TVs that cut off the edges (overscan): the design area
 * is shrunk by this part of its size on each side (0 .. 0.15). */
void  gfx_set_margin(float m);
/* The whole visible screen in design units (bigger than 0..width, 0..480
 * when there is a margin): for backgrounds and full-screen pictures. */
void  gfx_full_area(float *x, float *y, float *w, float *h);
/* One screen pixel in drawing units, and coordinates moved onto pixel edges. */
void  gfx_rrect_vgrad3(float x, float y, float w, float h, float r, Color top, Color mid, Color bottom);
void  gfx_fill_screen(Color c);   /* the whole TV picture, margins included */
float gfx_pixel_w(void);
float gfx_pixel_h(void);
float gfx_snap_x(float x);
float gfx_snap_y(float y);
const char *gfx_mode_name(void);

void gfx_begin_frame(void);
void gfx_end_frame(void);
void gfx_set_additive(bool on);
void gfx_mask_begin(void);
void gfx_mask_use(void);
void gfx_mask_end(void);

/* Transform applied to everything drawn (2D):
 *   x' = a*x + b*y + tx,   y' = c*x + d*y + ty */
void gfx_set_affine(float a, float b, float c, float d, float tx, float ty);
void gfx_set_transform(float sx, float sy, float tx, float ty);   /* scale + move */
/* Scale s and rotation `deg` (clockwise) about the point (px,py), then move by (mx,my). */
void gfx_set_spin(float px, float py, float s, float deg, float mx, float my);
void gfx_reset_transform(void);

/* 3D mode: everything drawn is on a flat plane; m (3 rows of 4) takes a
 * point (x, y) of that plane to (X, Y, Z) in pixels around the screen point
 * (cx, cy), Z pointing at the viewer. It is then seen with CSS-style
 * perspective: scale = persp / (persp - Z). */
void gfx_begin_3d(const float m[12], float cx, float cy, float persp);
/* In 3D mode: a triangle from three points already placed in 3D (x, y, z each). */
void gfx_tri3d(const float *xyz9, const Color *c3);
void gfx_end_3d(void);

void gfx_set_clip(float x, float y, float w, float h);
void gfx_clear_clip(void);
/* A clip every other clip stays inside (a scrolling list); gfx_clear_clip
 * returns to it. */
void gfx_set_base_clip(float x, float y, float w, float h);

/* Antialiasing (supersampling) for part of the screen. Plan the area (it is
 * split into tiles; returns how many, 0 if not possible), then for each tile:
 * gfx_ss_begin(i), draw everything that shows there, gfx_ss_end(i). Then
 * gfx_ss_finish(), and later in the frame gfx_ss_draw() pastes the tiles in.
 * The tiles must all be drawn before anything else in the frame. */
int  gfx_ss_plan(float x, float y, float w, float h);
void gfx_ss_begin(int i);
void gfx_ss_end(int i);
void gfx_ss_finish(void);
void gfx_ss_draw(float alpha);
void gfx_clear_base_clip(void);

void gfx_rect(float x, float y, float w, float h, Color c);
void gfx_rect_vgrad(float x, float y, float w, float h, Color top, Color bottom);
void gfx_rect_hgrad(float x, float y, float w, float h, Color left, Color right);
void gfx_rrect(float x, float y, float w, float h, float r, Color c);
void gfx_rrect_vgrad(float x, float y, float w, float h, float r, Color top, Color bottom);
void gfx_rrect_outline(float x, float y, float w, float h, float r, float thick, Color c);
void gfx_shadow(float x, float y, float w, float h, float r, float spread, Color c);
void gfx_circle(float cx, float cy, float r, Color c);
void gfx_ring(float cx, float cy, float r, float thick, float a0, float a1, Color c);
void gfx_tri(float x1, float y1, float x2, float y2, float x3, float y3, Color c);
void gfx_quad(const float *xy8, Color c);                 /* 4 points, clockwise */
void gfx_quad_colors(const float *xy8, const Color *c4);  /* a colour per corner */
void gfx_line(float x1, float y1, float x2, float y2, float thick, Color c);
/* A grid of (cols+1) x (rows+1) points with a colour each (smooth shading). */
void gfx_mesh(int cols, int rows, const float *xy, const Color *c);

float gfx_line_height(FontId f, float size);
float gfx_text_width(FontId f, float size, const char *s, int len);
float gfx_text_width_sp(FontId f, float size, float spacing, const char *s, int len);
/* y is the top of the text line. Returns the width drawn. */
float gfx_text(FontId f, float size, float x, float y, Color c, const char *s, int len);
float gfx_text_sp(FontId f, float size, float spacing, float x, float y, Color c, const char *s, int len);
void  gfx_text_centered(FontId f, float size, float cx, float y, Color c, const char *s);
int   gfx_text_fit(FontId f, float size, const char *s, int len, float maxw);
/* Height from the top of the line to the top of capital letters, and the
 * capital height (for centring text vertically in a box). */
float gfx_cap_top(FontId f, float size);
float gfx_cap_height(FontId f, float size);

/* Textures. Data is in the GPU's tiled layout (see pixels.h), 32-byte
 * aligned; GfxImage owns it (freed by gfx_image_free). */
enum { GFX_RGB565 = 0, GFX_RGB5A3 = 1 };
typedef struct { GXTexObj obj; void *data; u16 w, h, tex_w, tex_h; u8 fmt; bool repeat; } GfxImage;
bool  gfx_image_init(GfxImage *img, void *data, int w, int h, int tex_w, int tex_h, int fmt);
void  gfx_image_free(GfxImage *img);
/* The texture's memory was changed (call after writing new pixels into it). */
void  gfx_image_touch(GfxImage *img);
void  gfx_image_draw(GfxImage *img, float x, float y, float w, float h, Color tint);
/* Part of the picture: u0..u1, v0..v1 in its pixels. */
void  gfx_image_draw_part(GfxImage *img, float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1, Color tint);
/* Four corners (clockwise from top-left) with their texture positions in pixels. */
void  gfx_image_quad(GfxImage *img, const float *xy8, const float *uv8, Color tint);

/* Repeating greyscale patterns. */
typedef struct { GXTexObj obj; void *data; u16 w, h; } GfxPattern;
bool  gfx_pattern_init(GfxPattern *pt, const unsigned char *data, int w, int h);
void  gfx_pattern_free(GfxPattern *pt);
void  gfx_pattern_rect(GfxPattern *pt, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color c);
