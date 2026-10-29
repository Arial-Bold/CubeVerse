/* pointer.c - see pointer.h. The pointer is CubeBrowser's arrow, drawn
 * from shapes. */
#include "pointer.h"
#include "settings.h"
#include "input.h"
#include "sound.h"
#include "gfx.h"
#include "ui.h"
#include "m3d.h"
#include <malloc.h>
#include <math.h>
#include <string.h>

#define NHOT 160

typedef struct { int id; float x, y, w, h; } Hot;
static Hot hots[2][NHOT];
static int nhot[2];
static int cur;                 /* list being filled this frame */
static float px = 320, py = 240;
static int over = -1, last_over = -1;
static bool shown = true;
static float cx0, cy0, cw, ch;  /* confinement */
static float speed;
static int style, pen_r;      /* 0 the arrow, 1 the drawing pen (a ring the pen's size) */
static float clampf_(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void ptr_pen(float radius) { style = 1; pen_r = (int)(radius * 100); }

void ptr_init(void) { px = gfx_width() / 2; py = 240; }

void ptr_show(bool on) { shown = on; }
bool ptr_shown(void) { return shown; }
float ptr_x(void) { return px; }
float ptr_y(void) { return py; }
void ptr_set(float x, float y) { px = x; py = y; }
void ptr_confine(float x, float y, float w, float h) { cx0 = x; cy0 = y; cw = w; ch = h; }
bool ptr_held(void) { return shown && input_held(BTN_A); }

static int hit(const Hot *list, int n, float x, float y) {
    for (int i = n - 1; i >= 0; i--)   /* the last drawn is on top */
        if (x >= list[i].x && x < list[i].x + list[i].w && y >= list[i].y && y < list[i].y + list[i].h) return list[i].id;
    return -1;
}

/* the D-pad: hop to the nearest thing in that direction */
static void hop(int dir) {
    const Hot *list = hots[cur];
    int n = nhot[cur];
    float best = 1e9f, bx = px, by = py;
    for (int i = 0; i < n; i++) {
        float hx = list[i].x + list[i].w / 2, hy = list[i].y + list[i].h / 2;
        if (list[i].id == over && hit(list, n, px, py) == list[i].id) continue;
        float dx = hx - px, dy = hy - py, along, side;
        switch (dir) {
        case DIR_UP: along = -dy; side = dx; break;
        case DIR_DOWN: along = dy; side = dx; break;
        case DIR_LEFT: along = -dx; side = dy; break;
        default: along = dx; side = dy; break;
        }
        if (along < 8) continue;
        float score = along + fabsf(side) * 2.5f;
        if (score < best) { best = score; bx = hx; by = hy; }
    }
    if (best < 1e9f) { px = bx; py = by; return; }
    /* nothing further that way: going left or right, wrap round to the far
     * end of the same row (on the keyboard, right from P comes back to Q) */
    if (dir != DIR_LEFT && dir != DIR_RIGHT) return;
    int on = hit(list, n, px, py);
    float row_h = 24;
    for (int i = 0; i < n; i++) if (list[i].id == on) row_h = list[i].h;
    float far = 0;
    bool found = false;
    for (int i = 0; i < n; i++) {
        float hx = list[i].x + list[i].w / 2, hy = list[i].y + list[i].h / 2;
        if (fabsf(hy - py) > row_h * 0.5f || list[i].id == on) continue;
        float d = dir == DIR_RIGHT ? px - hx : hx - px;   /* how far back the other way */
        if (d > far) { far = d; bx = hx; by = hy; found = true; }
    }
    if (found) { px = bx; py = by; }
}

void ptr_update(void) {
    if (!shown) return;
    float sx = input_stick_x(), sy = input_stick_y();
    float m = sqrtf(sx * sx + sy * sy);
    if (m > 0.05f) {
        /* gentle near the middle of the stick, fast at the edge, scaled by
         * the Pointer speed setting: 1 as slow as it always was, 10 twice
         * the old top speed, evenly spaced between */
        float k = 0.48f + (g_set.pointer_speed - 1) * ((3.3f - 0.48f) / 9.0f);
        float target = (120 + 520 * m * m) * k;
        speed += (target - speed) * clamp01(g_dt * 10);
        px += sx / m * speed * m * g_dt;
        py += sy / m * speed * m * g_dt;
    } else {
        speed = 0;
    }
    if (input_pressed(BTN_UP)) hop(DIR_UP);
    if (input_pressed(BTN_DOWN)) hop(DIR_DOWN);
    if (input_pressed(BTN_LEFT)) hop(DIR_LEFT);
    if (input_pressed(BTN_RIGHT)) hop(DIR_RIGHT);
    float W = gfx_width();
    if (cw > 0) {
        if (px < cx0) px = cx0;
        if (px > cx0 + cw - 1) px = cx0 + cw - 1;
        if (py < cy0) py = cy0;
        if (py > cy0 + ch - 1) py = cy0 + ch - 1;
    }
    if (px < 4) px = 4;
    if (px > W - 4) px = W - 4;
    if (py < 4) py = 4;
    if (py > 470) py = 470;
    /* what's under the hand, from the last frame's areas */
    int prev = over;
    over = hit(hots[cur], nhot[cur], px, py);
    (void)prev;
    if (over >= 0) last_over = over;
    if (over < 0) last_over = -1;
}

void ptr_frame_begin(void) {
    style = 0;
    cur ^= 1;
    nhot[cur] = 0;
}

bool ptr_hot(int id, float x, float y, float w, float h) {
    if (nhot[cur] < NHOT) {
        Hot *hh = &hots[cur][nhot[cur]++];
        hh->id = id; hh->x = x; hh->y = y; hh->w = w; hh->h = h;
    }
    return shown && over == id;
}

int ptr_over(void) { return shown ? over : -1; }

int ptr_clicked(void) {
    if (!shown || !input_pressed(BTN_A)) return -1;
    return over;
}

float ptr_scroll(float top, float bottom) {
    (void)top; (void)bottom;
    return input_cstick_y() * 600 * g_dt;   /* only the C-stick scrolls (not the pointer at the edges) */
}

/* ------------------------------------------------------------- drawing */

void ptr_draw(float al) {
    if (!shown || al <= 0) return;
    if (style == 1) {
        float r = pen_r / 100.0f;
        if (r < 3) r = 3;
        Color blue = rgba(0x29, 0xA8, 0xE0, (u8)(255 * al));
        gfx_ring(px, py, r + 1, 2, 0, 6.2832f, rgba(255, 255, 255, (u8)(200 * al)));
        gfx_ring(px, py, r, 1.6f, 0, 6.2832f, blue);
        gfx_rect(px - 0.8f, py - r - 7, 1.6f, 5, blue);
        gfx_rect(px - 0.8f, py + r + 2, 1.6f, 5, blue);
        gfx_rect(px - r - 7, py - 0.8f, 5, 1.6f, blue);
        gfx_rect(px + r + 2, py - 0.8f, 5, 1.6f, blue);
        return;
    }
    /* CubeBrowser's arrow: a shadow, a purple outline, a white fill and a
     * gold glint; it shrinks a little while A is held, rings gold over
     * something it can press, and bobs gently when the stick is let go */
    static float press, hover, idle;
    float k = clampf_(g_dt * 12);
    press += ((input_held(BTN_A) ? 1.0f : 0.0f) - press) * k;
    hover += ((over >= 0 ? 1.0f : 0.0f) - hover) * clampf_(g_dt * 8);
    idle = (fabsf(input_stick_x()) + fabsf(input_stick_y()) > 0.05f) ? 0 : idle + g_dt;
    float x = px, y = py + (idle > 0.5f ? sinf(g_time * 3.0f) * 1.2f : 0);
    float s = 1.0f - press * 0.15f;
    Color gold = rgba(255, 214, 92, 255);
    if (hover > 0.01f) {
        float pulse = 0.5f + 0.5f * sinf(g_time * 6.0f);
        gfx_circle(x, y, 9 + 3 * pulse, rgba(gold.r, gold.g, gold.b, (u8)(115 * hover * al)));
    }
    for (int pass = 0; pass < 3; pass++) {
        float ox = pass == 0 ? 3 : 0, oy = pass == 0 ? 4 : 0, grow = pass == 2 ? 0 : 2.2f;
        Color c = pass == 0 ? rgba(8, 4, 30, (u8)(120 * al)) : pass == 1 ? rgba(70, 50, 160, (u8)(255 * al))
                                                                         : rgba(255, 255, 255, (u8)(255 * al));
        float bx = x + ox, by = y + oy;
        gfx_tri(bx - grow * 0.6f, by - grow * 1.6f, bx - grow * 0.6f, by + (22 + grow * 1.4f) * s,
                bx + (17 + grow * 1.7f) * s, by + (16 + grow * 0.6f) * s, c);
        gfx_line(bx + 7 * s, by + 15 * s, bx + 12 * s, by + 26 * s, 5 + grow * 1.8f, c);
    }
}
