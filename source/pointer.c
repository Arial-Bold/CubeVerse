/* pointer.c - see pointer.h. The hand is drawn from shapes (the design's
 * pearl-white hand with a thick indigo outline), not a picture. */
#include "pointer.h"
#include "input.h"
#include "sound.h"
#include "gfx.h"
#include "ui.h"
#include "m3d.h"
#include <math.h>

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
static int style, pen_r;   /* 0 the hand, 1 the drawing pen (a ring the pen's size) */

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
    if (best < 1e9f) { px = bx; py = by; }
}

void ptr_update(void) {
    if (!shown) return;
    float sx = input_stick_x(), sy = input_stick_y();
    float m = sqrtf(sx * sx + sy * sy);
    if (m > 0.05f) {
        /* gentle near the middle of the stick, fast at the edge */
        float target = 120 + 520 * m * m;
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
    if (over >= 0 && over != prev && over != last_over) sound_play(SFX_HOVER);
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
    float d = input_cstick_y() * 600 * g_dt;
    if (shown && cw <= 0) {
        float edge = 26;
        if (py > bottom - edge && input_stick_y() > 0.3f) d += (py - (bottom - edge)) / edge * 420 * g_dt;
        if (py < top + edge && input_stick_y() < -0.3f) d -= ((top + edge) - py) / edge * 420 * g_dt;
    }
    return d;
}

/* ------------------------------------------------------------- drawing */

/* The hand in a 26 x 32 box, tip at (9.5, 1). */
static void hand_shapes(float ox, float oy, float s, bool outline, Color fill_top, Color fill_bot, float al) {
    float grow = outline ? 1.2f : 0;
    Color o = rgba(0x24, 0x1C, 0x5C, (u8)(255 * al));
#define R_(x, y, w, h, r)                                                                                         \
    do {                                                                                                          \
        float X = ox + ((x) - grow) * s, Y = oy + ((y) - grow) * s, Wd = ((w) + 2 * grow) * s, Ht = ((h) + 2 * grow) * s; \
        if (outline) gfx_rrect(X, Y, Wd, Ht, ((r) + grow) * s, o);                                                 \
        else gfx_rrect_vgrad(X, Y, Wd, Ht, (r) * s,                                                               \
                             color_lerp(fill_top, fill_bot, clamp01(((y) - 1) / 30.0f)),                          \
                             color_lerp(fill_top, fill_bot, clamp01(((y) + (h) - 1) / 30.0f)));                  \
    } while (0)
    R_(7, 1, 5, 18, 2.5f);          /* the pointing finger */
    R_(11.8f, 8.6f, 4.7f, 10, 2.35f);
    R_(16.2f, 10.2f, 4.4f, 9, 2.2f);
    R_(20.2f, 12.4f, 3.8f, 8, 1.9f);
    R_(6.8f, 14.5f, 17.2f, 14.9f, 4.0f);   /* the palm */
    /* the thumb, turned 42 degrees about (8,18) */
    gfx_set_spin(ox + 8 * s, oy + 18 * s, 1, 42, 0, 0);
    R_(1.2f, 15.6f, 10, 4.8f, 2.4f);
    gfx_reset_transform();
#undef R_
}

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
    float s = 0.95f;
    float ox = px - 9.5f * s, oy = py - 1 * s;
    /* soft shadow */
    Color sh = rgba(0, 0, 0, (u8)(0.45f * 255 * al));
    gfx_rrect(ox + 8 * s + 2, oy + 2 + 3, 5 * s, 18 * s, 2.5f * s, sh);
    gfx_rrect(ox + 6.8f * s + 2, oy + 14.5f * s + 3, 17.2f * s, 14.9f * s, 4 * s, sh);
    /* faint blue glow, then outline, then the pearl fill */
    gfx_rrect(ox + 4 * s, oy - 1, 22 * s, 32 * s, 8 * s, rgba(175, 195, 255, (u8)(0.10f * 255 * al)));
    hand_shapes(ox, oy, s, true, rgba(0, 0, 0, 0), rgba(0, 0, 0, 0), al);
    hand_shapes(ox, oy, s, false, rgba(0xFF, 0xFF, 0xFF, (u8)(255 * al)), rgba(0xB4, 0xBA, 0xD6, (u8)(255 * al)), al);
    /* creases between the fingers, and the thumb's */
    Color cr = rgba(0x24, 0x1C, 0x5C, (u8)(0.75f * 255 * al));
    gfx_line(ox + 12 * s, oy + 11.4f * s, ox + 12 * s, oy + 15.6f * s, 0.9f * s, cr);
    gfx_line(ox + 16.35f * s, oy + 13 * s, ox + 16.35f * s, oy + 16.4f * s, 0.9f * s, cr);
    gfx_line(ox + 20.35f * s, oy + 15 * s, ox + 20.35f * s, oy + 17.8f * s, 0.9f * s, cr);
    gfx_line(ox + 9.4f * s, oy + 20.6f * s, ox + 10.4f * s, oy + 22.8f * s, 0.9f * s, cr);
    /* gloss */
    gfx_rrect(ox + 8.1f * s, oy + 2.6f * s, 1.3f * s, 11 * s, 0.65f * s, rgba(255, 255, 255, (u8)(255 * al)));
    gfx_rrect(ox + 12.9f * s, oy + 10 * s, 1 * s, 4 * s, 0.5f * s, rgba(255, 255, 255, (u8)(0.8f * 255 * al)));
    gfx_rrect(ox + 17.2f * s, oy + 11.6f * s, 0.9f * s, 3.4f * s, 0.45f * s, rgba(255, 255, 255, (u8)(0.8f * 255 * al)));
}
