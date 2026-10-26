/* ui.c - see ui.h. Sizes and colours follow the design mockups (which were
 * matched to the GameCube's own menus). */
#include "ui.h"
#include "input.h"
#include "m3d.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

float UI_X = 0, UI_W = 640;
float g_time, g_dt;

/* Screens lay out across the whole width: 640 in 4:3, 854 in 16:9. */
void ui_frame_begin(void) { UI_X = 0; UI_W = gfx_width(); }

/* ------------------------------------------------------------------ text */

float ui_text(FontId f, float size, float sp, float x, float top, float h, Color c, const char *s) {
    float lh = gfx_line_height(f, size);
    return gfx_text_sp(f, size, sp, x, top + (h - lh) * 0.5f, c, s, -1);
}

float ui_text_right(FontId f, float size, float sp, float right, float top, float h, Color c, const char *s) {
    float w = gfx_text_width_sp(f, size, sp, s, -1);
    ui_text(f, size, sp, right - w, top, h, c, s);
    return w;
}

float ui_text_center(FontId f, float size, float sp, float cx, float top, float h, Color c, const char *s) {
    float w = gfx_text_width_sp(f, size, sp, s, -1);
    ui_text(f, size, sp, cx - w * 0.5f, top, h, c, s);
    return w;
}

void ui_text_glow(FontId f, float size, float sp, float x, float top, float h, Color c, Color glow, float r, const char *s) {
    if (glow.a && r > 0) {
        for (int ring = 0; ring < 2; ring++) {
            float rr = r * (ring ? 1.0f : 0.5f);
            Color g = color_alpha(glow, ring ? 0.10f : 0.16f);
            for (int k = 0; k < 8; k++) {
                float a = k * (float)M_PI / 4;
                ui_text(f, size, sp, x + cosf(a) * rr, top + sinf(a) * rr, h, g, s);
            }
        }
    }
    ui_text(f, size, sp, x, top + 1, h, rgba(0, 0, 0, (u8)(c.a * 0.8f)), s);
    ui_text(f, size, sp, x, top, h, c, s);
}

/* ----------------------------------------------------------- backgrounds */

/* The cube screen's background: dark, a 24px grid of 2px lines and a soft
 * vignette. Every screen uses it, so moving between them never changes it. */
void ui_background_cube_a(float alpha) {
    if (alpha <= 0) return;
    float fx, fy, fw, fh;
    gfx_full_area(&fx, &fy, &fw, &fh);
    u8 A = (u8)(255 * clamp01(alpha));
    gfx_rect(fx, fy, fw, fh, rgba(6, 6, 8, A));
    Color line = rgba(0x25, 0x25, 0x2b, A);
    /* lines line up on the screen's middle, whatever its width */
    float cx = gfx_width() / 2, cy = 240;
    float x0 = cx - 24 * ceilf((cx - fx) / 24), y0 = cy - 24 * ceilf((cy - fy) / 24);
    for (float x = x0; x < fx + fw; x += 24) gfx_rect(x, fy, 2, fh, line);
    for (float y = y0; y < fy + fh; y += 24) gfx_rect(fx, y, fw, 2, line);
    /* vignette: radial-gradient(ellipse, transparent 40%, rgba(0,0,0,.6)) */
    enum { C = 12, R = 10 };
    float xy[(C + 1) * (R + 1) * 2];
    Color col[(C + 1) * (R + 1)];
    for (int j = 0; j <= R; j++)
        for (int i = 0; i <= C; i++) {
            int k = j * (C + 1) + i;
            float x = fx + fw * i / C, y = fy + fh * j / R;
            xy[k * 2] = x; xy[k * 2 + 1] = y;
            float dx = (x - (fx + fw / 2)) / (fw / 2 * 1.414f), dy = (y - (fy + fh / 2)) / (fh / 2 * 1.414f);
            float d = sqrtf(dx * dx + dy * dy);
            float v = d < 0.4f ? 0 : (d - 0.4f) / 0.6f * 0.6f;
            col[k] = rgba(0, 0, 0, (u8)(clamp01(v) * 255 * clamp01(alpha)));
        }
    gfx_mesh(C, R, xy, col);
}

void ui_background_cube(void) { ui_background_cube_a(1); }
void ui_background_grid(float alpha) { ui_background_cube_a(alpha); }

/* ------------------------------------------------------- scrolling text */

/* Text wider than its box scrolls along it and back, with pauses at each
 * end (as in CubeBrowser). Each text keeps its own start time while shown. */
typedef struct { unsigned key; float start; unsigned last_frame; } Scroll;
static Scroll scrolls[24];
static unsigned frame_count;

void ui_text_scroll_tick(void) { frame_count++; }

static float scroll_offset(const char *s, float x, float top, float over) {
    unsigned h = 2166136261u;
    for (const char *p = s; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    h ^= (unsigned)(x * 7) * 2654435761u ^ (unsigned)(top * 13);
    Scroll *sc = NULL, *old = &scrolls[0];
    for (int i = 0; i < 24; i++) {
        if (scrolls[i].key == h) { sc = &scrolls[i]; break; }
        if (scrolls[i].last_frame < old->last_frame) old = &scrolls[i];
    }
    if (!sc || sc->last_frame + 2 < frame_count) {
        if (!sc) sc = old;
        sc->key = h;
        sc->start = g_time;
    }
    sc->last_frame = frame_count;
    const float pause = 1.6f, speed = 32.0f;
    float travel = over / speed, period = pause * 2 + travel;
    float t = fmodf(g_time - sc->start, period);
    if (t < pause) return 0;
    if (t < pause + travel) return (t - pause) * speed;
    return over;
}

float ui_text_fit_scroll(FontId f, float size, float sp, float x, float top, float w, float h, Color c, const char *s) {
    float tw = gfx_text_width_sp(f, size, sp, s, -1);
    if (tw <= w) { ui_text(f, size, sp, x, top, h, c, s); return tw; }
    float off = scroll_offset(s, x, top, tw - w);
    gfx_set_clip(x, top - 4, w, h + 8);
    ui_text(f, size, sp, x - off, top, h, c, s);
    gfx_clear_clip();
    return w;
}

/* ----------------------------------------------------------------- panels */

void ui_panel(float x, float y, float w, float h, float al) {
    if (al <= 0) return;
    gfx_shadow(x, y + 8, w, h, 6, 14, rgba(0, 0, 0, (u8)(0.55f * 255 * al)));
    gfx_rrect_outline(x - 3, y - 3, w + 6, h + 6, 6, 2, rgba(200, 208, 220, (u8)(0.55f * 255 * al)));
    gfx_rrect_outline(x - 1, y - 1, w + 2, h + 2, 5, 1, rgba(10, 13, 16, (u8)(255 * al)));
    /* radial-gradient(ellipse at 50% 35%, rgba(40,72,74,.92), rgba(14,22,26,.97) 72%) */
    enum { C = 8, R = 8 };
    float xy[(C + 1) * (R + 1) * 2];
    Color col[(C + 1) * (R + 1)];
    for (int j = 0; j <= R; j++)
        for (int i = 0; i <= C; i++) {
            int k = j * (C + 1) + i;
            float px = x + w * i / C, py = y + h * j / R;
            xy[k * 2] = px; xy[k * 2 + 1] = py;
            float dx = (px - (x + w * 0.5f)) / (w * 0.5f * 1.414f), dy = (py - (y + h * 0.35f)) / (h * 0.65f * 1.414f);
            float t = clamp01(sqrtf(dx * dx + dy * dy) / 0.72f);
            Color a = rgba(40, 72, 74, (u8)(0.92f * 255 * al)), b = rgba(14, 22, 26, (u8)(0.97f * 255 * al));
            col[k] = color_lerp(a, b, t);
        }
    gfx_mesh(C, R, xy, col);
    /* inset 0 0 30px rgba(0,0,0,.6): darker towards the edges */
    for (int i = 0; i < 5; i++) {
        float d = i * 5.0f;
        gfx_rrect_outline(x + d, y + d, w - 2 * d, h - 2 * d, 4, 5, rgba(0, 0, 0, (u8)(0.6f * 255 * al * (5 - i) / 5 * 0.35f)));
    }
    gfx_rrect_outline(x, y, w, h, 4, 1, rgba(255, 255, 255, (u8)(0.18f * 255 * al)));
}

void ui_ptitle(float px, float py, float pw, const char *left, const char *right, float al) {
    if (al <= 0) return;
    Color t = COL_TITLE;
    t.a = (u8)(255 * al);
    float x0 = px + 16, x1 = px + pw - 16;
    ui_text(FONT_MENU, 15, 1.6f, x0, py + 9, 15, t, left);
    if (right) {
        Color s = COL_SOFT;
        s.a = (u8)(255 * al);
        ui_text_right(FONT_MENU, 15, 1.6f, x1, py + 9, 15, s, right);
    }
    gfx_rect(px + 10, py + 31, pw - 20, 1, rgba(200, 210, 230, (u8)(0.28f * 255 * al)));
}

/* A fine square grid over part of a bar, like the system menu's buttons:
 * strongest at one end (side > 0: the right end), fading out across `span`
 * of the width. Lines sit on whole screen pixels so every one looks alike. */
static void grid_patch(float x, float y, float w, float h, float cell, float span, int side, Color c) {
    float lw = gfx_pixel_w(), lh = gfx_pixel_h();
    float sw = w * span;
    float x0 = side > 0 ? x + w - sw : x, x1 = x0 + sw;
    Color clear = c;
    clear.a = 0;
    /* horizontal lines: one gradient each */
    for (float gy = y + cell * 0.5f; gy < y + h - 1; gy += cell) {
        float yy = gfx_snap_y(gy);
        if (side > 0) gfx_rect_hgrad(x0, yy, sw, lh, clear, c);
        else gfx_rect_hgrad(x0, yy, sw, lh, c, clear);
    }
    /* vertical lines, fading with distance from the strong end */
    float start = side > 0 ? x + w - cell * 0.5f : x + cell * 0.5f;
    for (int i = 0;; i++) {
        float gx = start - side * cell * i;
        if (gx < x0 || gx > x1) break;
        float k = 1 - (side > 0 ? (x1 - gx) : (gx - x0)) / sw;
        Color lc = c;
        lc.a = (u8)(c.a * k * k);
        if (lc.a) gfx_rect(gfx_snap_x(gx), y + 1, lw, h - 2, lc);
    }
}

void ui_row(float x, float y, float w, float h, bool on, float al) {
    if (!on || al <= 0) return;
    gfx_rrect_vgrad(x, y, w, h, 3, rgba(140, 160, 180, (u8)(0.34f * 255 * al)), rgba(70, 90, 110, (u8)(0.22f * 255 * al)));
    grid_patch(x + 1, y + 1, w - 2, h - 2, 7, 0.45f, -1, rgba(200, 235, 245, (u8)(0.20f * 255 * al)));
    gfx_rrect_outline(x, y, w, h, 3, 1, rgba(215, 225, 240, (u8)(0.6f * 255 * al)));
}

void ui_radio(float x, float cy, bool on, float al) {
    Color c = rgba(0xf2, 0xf4, 0xf8, (u8)(255 * al));
    gfx_ring(x + 6.5f, cy, 6.5f, 1.5f, 0, 2 * (float)M_PI, c);
    if (on) gfx_circle(x + 6.5f, cy, 3, c);
}

void ui_button(float x, float y, float w, float h, const char *label, bool on, float al) {
    if (al <= 0) return;
    u8 A = (u8)(255 * al);
    gfx_rrect_outline(x - 3, y - 3, w + 6, h + 6, 5, 2,
                      on ? rgba(230, 200, 70, (u8)(0.45f * A)) : rgba(170, 178, 190, (u8)(0.22f * A)));
    gfx_rrect_outline(x - 1, y - 1, w + 2, h + 2, 4, 1, on ? rgba(40, 32, 4, (u8)(0.9f * A)) : rgba(10, 12, 16, (u8)(0.8f * A)));
    if (on) {
        gfx_shadow(x, y, w, h, 3, 6, rgba(240, 210, 80, (u8)(0.35f * A)));
        gfx_rrect_vgrad3(x, y, w, h, 3, rgba(255, 244, 150, (u8)(0.80f * A)), rgba(225, 195, 60, (u8)(0.55f * A)),
                         rgba(170, 140, 30, (u8)(0.62f * A)));
    } else {
        gfx_rrect_vgrad3(x, y, w, h, 3, rgba(235, 240, 248, (u8)(0.42f * A)), rgba(170, 180, 195, (u8)(0.22f * A)),
                         rgba(120, 130, 145, (u8)(0.30f * A)));
    }
    /* the fine grid at the right end, fading out to the left */
    grid_patch(x + 1, y + 1, w - 2, h - 2, 6, 0.42f, 1,
               on ? rgba(255, 252, 215, (u8)(0.42f * A)) : rgba(255, 255, 255, (u8)(0.30f * A)));
    gfx_rrect_outline(x, y, w, h, 3, 1, on ? rgba(255, 250, 200, (u8)(0.8f * A)) : rgba(235, 240, 250, (u8)(0.55f * A)));
    gfx_rect(x + 2, y + 1, w - 4, 1, on ? rgba(255, 255, 230, (u8)(0.8f * A)) : rgba(255, 255, 255, (u8)(0.5f * A)));
    Color tc = on ? rgba(0x1a, 0x16, 0x08, A) : rgba(0xf2, 0xf4, 0xf8, A);
    if (!on) ui_text_center(FONT_MENU, 14, 1.6f, x + w / 2, y + 1, h, rgba(0, 0, 0, (u8)(0.7f * A)), label);
    ui_text_center(FONT_MENU, 14, 1.6f, x + w / 2, y, h, tc, label);
}

void ui_desc(float x, float y, float w, float h, float al) {
    if (al <= 0) return;
    gfx_rrect_outline(x - 1, y - 1, w + 2, h + 2, 4, 1, rgba(11, 11, 32, (u8)(255 * al)));
    gfx_rrect_vgrad(x, y, w, h, 3, rgba(0x2a, 0x22, 0x8c, (u8)(255 * al)), rgba(0x3b, 0x2c, 0xb6, (u8)(255 * al)));
    grid_patch(x + 1, y + 1, w - 2, h - 2, 7, 0.30f, 1, rgba(190, 190, 255, (u8)(0.16f * 255 * al)));
    gfx_rrect_outline(x, y, w, h, 3, 1, rgba(175, 175, 255, (u8)(0.6f * 255 * al)));
}

void ui_thumb_frame(float x, float y, float w, float h, float al) {
    gfx_rect(x - 2, y - 2, w + 4, h + 4, rgba(190, 200, 215, (u8)(0.5f * 255 * al)));
    gfx_rect(x, y, w, h, rgba(0x0b, 0x0f, 0x12, (u8)(255 * al)));
}

void ui_gold_ring(float x, float y, float w, float h, float pulse, float al) {
    u8 A = (u8)(255 * al);
    float g = 0.55f + 0.45f * pulse;
    for (int i = 3; i >= 1; i--) {
        float s = 5 + i * 3.5f;
        gfx_rrect_outline(x - s, y - s, w + 2 * s, h + 2 * s, 4 + i * 2, 3.5f, rgba(240, 210, 80, (u8)(A * 0.16f * g)));
    }
    gfx_rect(x - 5, y - 5, w + 10, 2, rgba(0x6a, 0x5a, 0x10, A));
    gfx_rect(x - 5, y + h + 3, w + 10, 2, rgba(0x6a, 0x5a, 0x10, A));
    gfx_rect(x - 5, y - 3, 2, h + 6, rgba(0x6a, 0x5a, 0x10, A));
    gfx_rect(x + w + 3, y - 3, 2, h + 6, rgba(0x6a, 0x5a, 0x10, A));
    gfx_rect(x - 3, y - 3, w + 6, 3, rgba(0xe8, 0xcf, 0x4a, A));
    gfx_rect(x - 3, y + h, w + 6, 3, rgba(0xe8, 0xcf, 0x4a, A));
    gfx_rect(x - 3, y, 3, h, rgba(0xe8, 0xcf, 0x4a, A));
    gfx_rect(x + w, y, 3, h, rgba(0xe8, 0xcf, 0x4a, A));
}

/* ------------------------------------------------------------------ hints */

static void pill(float x, float cy, float w, float h, Color ring, float al, bool glow) {
    float y = cy - h / 2;
    if (glow) gfx_rrect(x - 3, y - 3, w + 6, h + 6, h / 2 + 3, color_alpha(ring, 0.18f));
    gfx_shadow(x, y + 1, w, h, h / 2, 2, rgba(0, 0, 0, (u8)(0.6f * 255 * al)));
    gfx_rrect_vgrad(x, y, w, h, h / 2, rgba(0x3c, 0x3c, 0x46, (u8)(255 * al)), rgba(0x08, 0x08, 0x0a, (u8)(255 * al)));
    gfx_rrect_outline(x, y, w, h, h / 2, 1.5f, ring);
}

static void stick_icon(float x, float cy, float d, float al, int dir) {
    float r = d / 2, cx = x + r;
    gfx_circle(cx, cy + 1, r + 1, rgba(0, 0, 0, (u8)(0.6f * 255 * al)));
    gfx_circle(cx, cy, r, rgba(0x4a, 0x4a, 0x52, (u8)(255 * al)));
    gfx_circle(cx, cy, r * 0.78f, rgba(0x2a, 0x2a, 0x30, (u8)(255 * al)));
    gfx_ring(cx, cy, r, 1.5f, 0, 2 * (float)M_PI, rgba(255, 255, 255, (u8)(0.18f * 255 * al)));
    float nx = cx, ny = cy, off = d * 0.16f;
    if (dir == DIR_UP) ny -= off;
    else if (dir == DIR_DOWN) ny += off;
    else if (dir == DIR_LEFT) nx -= off;
    else if (dir == DIR_RIGHT) nx += off;
    float nr = d * 0.30f;
    gfx_circle(nx, ny + 1, nr, rgba(0, 0, 0, (u8)(0.5f * 255 * al)));
    for (int i = 4; i >= 0; i--) {
        float rr = nr * (i + 1) / 5.0f;
        gfx_circle(nx, ny, rr, (i & 1) ? rgba(0x9a, 0x9b, 0xa6, (u8)(255 * al)) : rgba(0xd6, 0xd7, 0xde, (u8)(255 * al)));
    }
}

static void dots(float x, float cy, Color c) {
    for (int i = 0; i < 3; i++) gfx_circle(x + 2 + i * 4.5f, cy + 1, 1.2f, c);
}

/* layout of one hint: key part, then dots, then label */
static float hint_draw(const char *hint, float x, float cy, float al, int stick_dir, bool big, bool draw) {
    char key[8] = "";
    const char *colon = strchr(hint, ':');
    const char *label = colon ? colon + 1 : hint;
    if (colon && colon - hint < 7) { memcpy(key, hint, (size_t)(colon - hint)); key[colon - hint] = 0; }
    float fs = big ? 15 : 14, d = big ? 26 : 24;
    FontId lf = big ? FONT_ROUND : FONT_MENU;
    float sp = big ? 0 : 1.2f;
    float w = 0;
    u8 A = (u8)(255 * al);
    if (!strcmp(key, "S")) {
        if (draw) stick_icon(x, cy, d, al, stick_dir);
        w = d;
    } else if (key[0]) {
        const char *text = key;
        Color tc = rgba(0xd8, 0xda, 0xe6, A), ring = rgba(255, 255, 255, (u8)(0.14f * A));
        bool glow = false;
        if (!strcmp(key, "A")) { tc = rgba(0x5f, 0xe3, 0x9a, A); ring = rgba(95, 227, 154, (u8)(0.38f * A)); glow = true; }
        if (!strcmp(key, "B")) { tc = rgba(0xff, 0x5c, 0x7c, A); ring = rgba(255, 92, 124, (u8)(0.38f * A)); glow = true; }
        if (!strcmp(key, "Z")) { tc = rgba(0xb9, 0xa2, 0xff, A); ring = rgba(185, 162, 255, (u8)(0.38f * A)); glow = true; }
        if (!strcmp(key, "LR")) text = "L R";
        if (!strcmp(key, "DP")) text = "\x81 \x82";
        if (!strcmp(key, "ST")) text = "START";
        float tw = gfx_text_width(FONT_MENU_BOLD, 13, text, -1);
        float pw = tw + 14 < 24 ? 24 : tw + 14;
        if (draw) {
            pill(x, cy, pw, 24, ring, al, glow);
            ui_text_center(FONT_MENU_BOLD, 13, 0, x + pw / 2, cy - 12, 24, tc, text);
        }
        w = pw;
    }
    if (key[0]) {
        if (draw) dots(x + w + 2, cy, rgba(0xc9, 0xcb, 0xd6, A));
        w += 17;
    }
    Color lc = big ? rgba(255, 255, 255, A) : rgba(0xdf, 0xe4, 0xff, A);
    float lw = gfx_text_width_sp(lf, fs, sp, label, -1);
    if (draw) ui_text(lf, fs, sp, x + w, cy - fs / 2 - 1, fs, lc, label);
    return w + lw;
}

float ui_hint_width(const char *hint, bool big) { return hint_draw(hint, 0, 0, 1, -1, big, false); }

/* The controller hints along the bottom: centred, in the same place and
 * style as on the cube screen. A long row closes up, then uses smaller type. */
void ui_hints(const char *const *hints, int n, float al, int stick_dir) {
    if (al <= 0 || n <= 0) return;
    float room = gfx_width() - 40;
    for (int big = 1; big >= 0; big--) {
        float total = 0;
        for (int i = 0; i < n; i++) total += ui_hint_width(hints[i], big != 0);
        float gap = 36;
        if (n > 1 && total + gap * (n - 1) > room) gap = (room - total) / (n - 1);
        if (gap >= 16 || !big) {
            if (gap < 10) gap = 10;
            ui_hints_center(hints, n, UI_HINT_Y, gap, al, stick_dir, big != 0);
            return;
        }
    }
}

void ui_hints_center(const char *const *hints, int n, float y, float gap, float al, int stick_dir, bool big) {
    if (al <= 0 || n <= 0) return;
    float total = 0;
    for (int i = 0; i < n; i++) total += ui_hint_width(hints[i], big);
    total += gap * (n - 1);
    float x = gfx_width() / 2 - total / 2;
    for (int i = 0; i < n; i++) x += hint_draw(hints[i], x, y, al, stick_dir, big, true) + gap;
}

/* ------------------------------------------------------------- messages */

void ui_message(const char *title, const char *text, float al) {
    if (al <= 0) return;
    float w = 420, h = 150, x = UI_X + UI_W / 2 - w / 2, y = 240 - h / 2 - 20;
    gfx_fill_screen(rgba(0, 0, 0, (u8)(0.45f * 255 * al)));
    ui_panel(x, y, w, h, al);
    ui_ptitle(x, y, w, title, NULL, al);
    /* wrap the text */
    char line[128];
    const char *p = text;
    float ty = y + 48;
    while (*p && ty < y + h - 16) {
        int n = gfx_text_fit(FONT_MENU, 14, p, -1, w - 40);
        int len = (int)strlen(p);
        if (n < len) {
            int k = n;
            while (k > 0 && p[k] != ' ') k--;
            if (k > 0) n = k;
        }
        if (n <= 0) n = len < 1 ? 0 : 1;
        if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;
        memcpy(line, p, (size_t)n);
        line[n] = 0;
        ui_text(FONT_MENU, 14, 1.2f, x + 20, ty, 22, rgba(0xee, 0xf1, 0xf6, (u8)(255 * al)), line);
        ty += 24;
        p += n;
        while (*p == ' ') p++;
    }
}

void ui_spinner(float cx, float cy, float r, float t, float al) {
    for (int i = 0; i < 8; i++) {
        float a = i * (float)M_PI / 4 + t * 4;
        float k = fmodf(i / 8.0f + t * 0.8f, 1.0f);
        gfx_circle(cx + cosf(a) * r, cy + sinf(a) * r, r * 0.22f, rgba(200, 215, 255, (u8)(255 * al * (0.25f + 0.75f * k))));
    }
}

/* --------------------------------------------------------------- swirl */

#define SWIRL_START 0.0f
#define SWIRL_DUR 1.55f
#define SWIRL_STEP 0.035f

float ui_swirl_duration(int n) { return SWIRL_START + SWIRL_DUR + SWIRL_STEP * (n > 0 ? n - 1 : 0); }

float ui_swirl_begin(int i, int n, float x, float y, float w, float h, float t, float radius, float spin) {
    float s = clamp01((t - SWIRL_START - i * SWIRL_STEP) / SWIRL_DUR);
    if (s >= 1) return 1;
    /* from the middle of the screen, through a point on a ring, to the
     * final place, along one smooth curve that slows as it lands */
    float u = 1 - (1 - s) * (1 - s) * (1 - s);
    float cxs = gfx_width() / 2, cys = 207;
    float fx = x + w / 2, fy = y + h / 2;
    float a = (n > 0 ? i * 2 * (float)M_PI / n : 0) - (float)M_PI / 2;
    float rx = cxs + radius * cosf(a), ry = 215 + radius * 0.72f * sinf(a);
    float p0x = cxs - fx, p0y = cys - fy;          /* offsets from the final place */
    float rxo = rx - fx, ryo = ry - fy;
    float c1x = 2 * rxo - p0x / 2, c1y = 2 * ryo - p0y / 2;
    float ox = (1 - u) * (1 - u) * p0x + 2 * (1 - u) * u * c1x;
    float oy = (1 - u) * (1 - u) * p0y + 2 * (1 - u) * u * c1y;
    float scale = 0.08f + 0.92f * u;
    float rot = spin * (((i * 37) % 71) / 35.0f - 1.0f) * sinf((float)M_PI * u);
    gfx_set_spin(fx, fy, scale, rot, ox, oy);
    return clamp01(s / 0.12f);
}

void ui_swirl_end(void) { gfx_reset_transform(); }

/* A description bar's text: the right part in full, the left part in the
 * space left (scrolling if it doesn't fit). */
void ui_desc_texts(float x, float y, float w, float h, const char *left, const char *right, float al) {
    Color c1 = rgba(0xec, 0xeb, 0xff, (u8)(255 * al)), c2 = rgba(0xc9, 0xc6, 0xff, (u8)(255 * al));
    float rw = 0;
    if (right && right[0]) rw = ui_text_right(FONT_MENU, 13, 1.2f, x + w - 12, y, h, c2, right) + 20;
    if (left && left[0]) ui_text_fit_scroll(FONT_MENU, 13, 1.2f, x + 12, y, w - 24 - rw, h, c1, left);
}
