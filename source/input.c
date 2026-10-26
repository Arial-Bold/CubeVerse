/* input.c - see input.h. */
#include "input.h"
#include <math.h>
#ifdef GEKKO
#include <gccore.h>
#endif

static uint32_t held, prev, pressed_now, injected;
#ifndef GEKKO
static uint32_t test_held;
static float test_sx, test_sy, test_tl, test_tr;
void input_test_state(uint32_t h, float x, float y, float l, float r) { test_held = h; test_sx = x; test_sy = y; test_tl = l; test_tr = r; }
#endif
static float sx, sy, cx, cy, tl, tr;
static int repeat_frames[16];
static int dir_frames, last_dir = DIR_NONE;

#define REPEAT_DELAY 22
#define REPEAT_RATE 6

static float axis(int v) {
    float f = v / 72.0f;
    float dz = 0.22f;
    if (fabsf(f) < dz) return 0;
    f = (f > 0 ? f - dz : f + dz) / (1.0f - dz);
    if (f > 1) f = 1;
    if (f < -1) f = -1;
    return f;
}

void input_init(void) {
#ifdef GEKKO
    PAD_Init();
#endif
}

void input_update(void) {
    prev = held;
    held = 0;
    sx = sy = cx = cy = tl = tr = 0;
#ifdef GEKKO
    PAD_ScanPads();
    for (int p = 0; p < 4; p++) {
        held |= PAD_ButtonsHeld(p);
        float x = axis(PAD_StickX(p)), y = -axis(PAD_StickY(p));
        if (fabsf(x) > fabsf(sx)) sx = x;
        if (fabsf(y) > fabsf(sy)) sy = y;
        x = axis(PAD_SubStickX(p)); y = -axis(PAD_SubStickY(p));
        if (fabsf(x) > fabsf(cx)) cx = x;
        if (fabsf(y) > fabsf(cy)) cy = y;
        float l = PAD_TriggerL(p) / 200.0f, r = PAD_TriggerR(p) / 200.0f;
        if (l > tl) tl = l > 1 ? 1 : l;
        if (r > tr) tr = r > 1 ? 1 : r;
    }
#else
    held = test_held; sx = test_sx; sy = test_sy; tl = test_tl; tr = test_tr;
#endif
    pressed_now = (held & ~prev) | injected;
    injected = 0;
    for (int b = 0; b < 16; b++) {
        if (held & (1u << b)) repeat_frames[b]++;
        else repeat_frames[b] = 0;
    }
    int d = input_stick_dir();
    if (d == DIR_NONE) {
        if (held & BTN_UP) d = DIR_UP;
        else if (held & BTN_DOWN) d = DIR_DOWN;
        else if (held & BTN_LEFT) d = DIR_LEFT;
        else if (held & BTN_RIGHT) d = DIR_RIGHT;
    }
    if (d != last_dir) dir_frames = 0;
    else if (d != DIR_NONE) dir_frames++;
    last_dir = d;
}

bool input_pressed(uint32_t b) { return (pressed_now & b) != 0; }
bool input_held(uint32_t b) { return (held & b) != 0; }

bool input_repeat(uint32_t b) {
    if (pressed_now & b) return true;
    for (int i = 0; i < 16; i++) {
        if (!(b & (1u << i))) continue;
        int f = repeat_frames[i];
        if (f > REPEAT_DELAY && (f - REPEAT_DELAY) % REPEAT_RATE == 0) return true;
    }
    return false;
}

int input_stick_dir(void) {
    float ax = fabsf(sx), ay = fabsf(sy);
    if (ax < 0.45f && ay < 0.45f) return DIR_NONE;
    if (ax > ay) return sx > 0 ? DIR_RIGHT : DIR_LEFT;
    return sy > 0 ? DIR_DOWN : DIR_UP;
}

int input_dir(void) {
    static const uint32_t pad[4] = { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT };
    for (int i = 0; i < 4; i++) if (pressed_now & pad[i]) return i;
    if (last_dir == DIR_NONE) return DIR_NONE;
    if (dir_frames == 0) return last_dir;
    if (dir_frames > REPEAT_DELAY && (dir_frames - REPEAT_DELAY) % REPEAT_RATE == 0) return last_dir;
    return DIR_NONE;
}

float input_stick_x(void) { return sx; }
float input_stick_y(void) { return sy; }
float input_cstick_x(void) { return cx; }
float input_cstick_y(void) { return cy; }
float input_trigger_l(void) { return tl; }
float input_trigger_r(void) { return tr; }
bool  input_any(void) { return pressed_now != 0; }
void  input_inject(uint32_t p) { injected |= p; }
