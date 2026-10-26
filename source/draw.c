/* draw.c - the drawing pad: a black-and-white 320 x 120 drawing (Juxt's
 * size), drawn with the pen while A is held. Pen sizes, eraser, undo,
 * clear, a feeling and spoilers, then post. */
#include "app.h"
#include "compose.h"
#include "cvui.h"
#include "pointer.h"
#include "input.h"
#include "sound.h"
#include "juxt.h"
#include "pixels.h"
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PW 320
#define PH 120
#define UNDO 10

static PostTarget target;
static uint8_t bits[PW * PH];
static uint8_t *undo[UNDO];
static int nundo;
static int pen = 1;            /* 0, 1, 2: small, medium, large */
static bool eraser;
static int feeling;
static bool spoiler;
static JResult send;
static GfxImage tex;
static bool tex_ok, dirty;
static bool stroke;
static float last_x, last_y;
static bool drawn;             /* anything on it */
static float discard_until;
/* where the canvas is on screen (set while drawing the screen) */
static float cvx, cvy, cvw, cvh;

#define ID_TAB 10
#define ID_FEEL 30
#define ID_SPOIL 40
#define ID_POST 41
#define ID_PEN 50     /* 50..52 */
#define ID_ERASER 53
#define ID_UNDO 54
#define ID_CLEAR 55
#define ID_CANVAS 60

static const float PEN_R[3] = { 0.6f, 1.6f, 3.2f };

static void upload(void) {
    if (!tex_ok) {
        uint16_t *t = memalign(32, PW * PH * 2);
        if (!t) return;
        tex_ok = gfx_image_init(&tex, t, PW, PH, PW, PH, GFX_RGB565);
        if (!tex_ok) return;
        GX_InitTexObjLOD(&tex.obj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
        dirty = true;
    }
    if (!dirty) return;
    uint16_t *t = tex.data;
    uint16_t black = rgb565(0x14, 0x14, 0x1F), white = rgb565(255, 255, 255), dot = rgb565(0xC9, 0xD2, 0xE6);
    for (int y = 0; y < PH; y++)
        for (int x = 0; x < PW; x++) {
            uint16_t c = bits[y * PW + x] ? black : ((x % 10 == 5 && y % 10 == 5) ? dot : white);
            t[tex_index(PW, x, y)] = c;
        }
    gfx_image_touch(&tex);
    dirty = false;
}

static void push_undo(void) {
    if (nundo == UNDO) {
        uint8_t *old = undo[0];
        memmove(undo, undo + 1, sizeof(undo[0]) * (UNDO - 1));
        undo[UNDO - 1] = old;
        nundo--;
    }
    if (!undo[nundo]) undo[nundo] = malloc(PW * PH);
    if (!undo[nundo]) return;
    memcpy(undo[nundo], bits, PW * PH);
    nundo++;
}

static void do_undo(void) {
    if (!nundo) { sound_play(SFX_ERROR); return; }
    memcpy(bits, undo[--nundo], PW * PH);
    dirty = true;
    sound_play(SFX_CANCEL);
}

static void dab(float x, float y) {
    float r = PEN_R[pen] * (eraser ? 2.2f : 1);
    int x0 = (int)floorf(x - r), x1 = (int)ceilf(x + r), y0 = (int)floorf(y - r), y1 = (int)ceilf(y + r);
    for (int yy = y0; yy <= y1; yy++) {
        if (yy < 0 || yy >= PH) continue;
        for (int xx = x0; xx <= x1; xx++) {
            if (xx < 0 || xx >= PW) continue;
            float dx = xx + 0.5f - x, dy = yy + 0.5f - y;
            if (dx * dx + dy * dy <= r * r + 0.3f) bits[yy * PW + xx] = eraser ? 0 : 1;
        }
    }
    dirty = true;
    if (!eraser) drawn = true;
}

static void line(float x0, float y0, float x1, float y1) {
    float d = hypotf(x1 - x0, y1 - y0);
    int n = (int)(d * 2) + 1;
    for (int i = 0; i <= n; i++) dab(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n);
}

static void enter(const char *arg, bool back) {
    if (back) return;
    post_target_parse(&target, arg);
    memset(bits, 0, sizeof(bits));
    nundo = 0;
    pen = 1;
    eraser = false;
    feeling = 0;
    spoiler = false;
    drawn = false;
    dirty = true;
    stroke = false;
    send.state = JS_IDLE;
}

static void leave(void) {
    ptr_confine(0, 0, 0, 0);
    for (int i = 0; i < UNDO; i++) { free(undo[i]); undo[i] = NULL; }
    if (tex_ok) { gfx_image_free(&tex); tex_ok = false; }
}

static void post(void) {
    if (send.state == JS_LOADING) return;
    bool any = false;
    for (int i = 0; i < PW * PH && !any; i++) any = bits[i] != 0;
    if (!any) { app_toast("Draw something first."); sound_play(SFX_ERROR); return; }
    sound_play(SFX_CONFIRM);
    juxt_post_drawing(&send, target.cid, target.reply ? target.post_id : NULL, bits, feeling, spoiler);
}

static void update(void) {
    if (send.state == JS_LOADING) return;
    if (send.state == JS_READY) {
        send.state = JS_IDLE;
        g_posted = true;
        app_toast(target.reply ? "Your comment was posted." : "Your drawing is up!");
        app_back();
        return;
    }
    if (send.state == JS_FAILED) {
        send.state = JS_IDLE;
        sound_play(SFX_ERROR);
        app_toast(send.msg[0] ? send.msg : "That didn't post. Try again.");
    }
    /* drawing: A held on the canvas */
    float sc = cvw / PW;
    float mx = (ptr_x() - cvx) / sc, my = (ptr_y() - cvy) / sc;
    if (!stroke && input_pressed(BTN_A) && ptr_over() == ID_CANVAS) {
        push_undo();
        stroke = true;
        last_x = mx;
        last_y = my;
        dab(mx, my);
        ptr_confine(cvx, cvy, cvw, cvh);
        return;
    }
    if (stroke) {
        if (input_held(BTN_A)) {
            line(last_x, last_y, mx, my);
            last_x = mx;
            last_y = my;
        } else {
            stroke = false;
            ptr_confine(0, 0, 0, 0);
        }
        return;
    }
    int c = ptr_clicked();
    if (c == ID_TAB) {
        sound_play(SFX_SELECT);
        char arg[96];
        post_target_arg(&target, arg, sizeof(arg));
        app_swap(SCR_COMPOSE, arg);
        return;
    }
    if (c >= ID_PEN && c < ID_PEN + 3) { pen = c - ID_PEN; eraser = false; sound_play(SFX_SELECT); }
    else if (c == ID_ERASER || input_pressed(BTN_X)) { eraser = !eraser || c == ID_ERASER; sound_play(SFX_SELECT); }
    else if (c == ID_UNDO || input_pressed(BTN_Z)) do_undo();
    else if (c == ID_CLEAR) { push_undo(); memset(bits, 0, sizeof(bits)); dirty = true; sound_play(SFX_CANCEL); }
    else if (c >= ID_FEEL && c < ID_FEEL + 6) { feeling = c - ID_FEEL; sound_play(SFX_SELECT); }
    else if (c == ID_SPOIL) { spoiler = !spoiler; sound_play(SFX_SELECT); }
    else if (c == ID_POST || input_pressed(BTN_START)) post();
    else if (input_pressed(BTN_L)) { pen = pen > 0 ? pen - 1 : 0; sound_play(SFX_SELECT); }
    else if (input_pressed(BTN_R)) { pen = pen < 2 ? pen + 1 : 2; sound_play(SFX_SELECT); }
    else if (input_pressed(BTN_B)) {
        if (drawn && g_time > discard_until) {
            discard_until = g_time + 3;
            sound_play(SFX_BACK);
            app_toast("Press B again to throw this drawing away.");
            return;
        }
        sound_play(SFX_BACK);
        app_back();
    }
}

/* a white tool box with something drawn in it */
static bool tool_box(int id, float x, float y, float w, float h, bool on) {
    bool hot = ptr_hot(id, x, y, w, h);
    gfx_rect(x - 1, y - 1, w + 2, h + 2, rgba(0, 0, 0, 100));
    gfx_rect(x, y, w, h, rgba(255, 255, 255, 255));
    if (on) ui_gold_ring(x, y, w, h, 0.5f + 0.5f * sinf(g_time * 4), 1);
    else if (hot) gfx_rrect_outline(x, y, w, h, 0, 2, rgba(0x29, 0xA8, 0xE0, 255));
    return hot;
}

static float pill_btn(int id, float x, float y, float h, const char *label, bool primary) {
    float w = gfx_text_width(FONT_ROUND, 13, label, -1) + 36;
    bool hot = ptr_hot(id, x, y, w, h);
    cv_pill(x, y, w, h, label, primary ? rgba(0x29, 0xA8, 0xE0, 255) : rgba(255, 255, 255, 255),
            primary ? rgba(255, 255, 255, 255) : rgba(0x4A, 0x51, 0x70, 255), hot, 1);
    return w;
}

/* the pen sizes and eraser from x; returns where they end */
static float tools(float x, float y, float th) {
    ui_text(FONT_MENU, 12, 1.2f, x, y, th, COL_SOFT, "Pen");
    x += 34;
    for (int i = 0; i < 3; i++) {
        tool_box(ID_PEN + i, x, y, th, th, !eraser && pen == i);
        float r = i == 0 ? 2 : i == 1 ? 4 : 7;
        gfx_circle(x + th / 2, y + th / 2, r, rgba(0x14, 0x14, 0x1F, 255));
        x += th + 8;
    }
    float ew = gfx_text_width(FONT_ROUND, 12, "Eraser", -1) + 24;
    tool_box(ID_ERASER, x, y, ew, th, eraser);
    ui_text_center(FONT_ROUND, 12, 0, x + ew / 2, y, th, rgba(0x4A, 0x51, 0x70, 255), "Eraser");
    return x + ew;
}

static void draw(float open) {
    (void)open;
    cv_background(1);
    char title[96];
    post_target_title(&target, title, sizeof(title));
    cv_title(title, CV_T_BLUE, NULL, 1);
    compose_tabs(1, ID_TAB);
    bool wide = UI_W > 700;
    cvw = wide ? 700 : 580;
    cvh = cvw * PH / PW;
    float total = wide ? cvw + 60 : cvw;
    float x0 = UI_W / 2 - total / 2;
    cvx = x0;
    cvy = 52;
    /* the canvas */
    upload();
    gfx_rect(cvx - 3, cvy - 3, cvw + 6, cvh + 6, rgba(200, 208, 220, 140));
    gfx_rect(cvx - 1, cvy - 1, cvw + 2, cvh + 2, rgba(10, 13, 16, 255));
    if (tex_ok) gfx_image_draw(&tex, cvx, cvy, cvw, cvh, rgba(255, 255, 255, 255));
    else gfx_rect(cvx, cvy, cvw, cvh, rgba(255, 255, 255, 255));
    bool on_canvas = ptr_hot(ID_CANVAS, cvx, cvy, cvw, cvh);
    if (on_canvas || stroke) ptr_pen(PEN_R[pen] * (eraser ? 2.2f : 1) * cvw / PW);
    float th = wide ? 42 : 48;
    float r1 = cvy + cvh + 16;
    float x = tools(x0, r1, th);
    (void)x;
    float right = x0 + total;
    if (wide) {
        compose_feelings(cvx + cvw + 12, cvy + 18, 48, 34, (cvh - 18 - 6 * 34) / 5, true, feeling, ID_FEEL, 1);
        ui_text_center(FONT_MENU, 11, 1.0f, cvx + cvw + 12 + 24, cvy, 16, COL_SOFT, "Feeling");
        float pw = gfx_text_width(FONT_ROUND, 13, "Post drawing", -1) + 36;
        float px = right - pw;
        pill_btn(ID_POST, px, r1, th, "Post drawing", true);
        const char *sp = spoiler ? "Spoilers: on" : "Spoilers: off";
        float sw = gfx_text_width_sp(FONT_MENU, 12, 1.2f, sp, -1) + 16;
        float sx = px - 8 - sw;
        if (ptr_hot(ID_SPOIL, sx, r1, sw, th)) gfx_rect(sx, r1, sw, th, rgba(140, 160, 180, 50));
        ui_text(FONT_MENU, 12, 1.2f, sx + 8, r1, th, spoiler ? CV_T_ORANGE : COL_SOFT, sp);
        float cw = gfx_text_width(FONT_ROUND, 13, "Clear", -1) + 36, uw = gfx_text_width(FONT_ROUND, 13, "Undo", -1) + 36;
        pill_btn(ID_CLEAR, sx - 10 - cw, r1, th, "Clear", false);
        pill_btn(ID_UNDO, sx - 10 - cw - 8 - uw, r1, th, "Undo", false);
    } else {
        float cw = gfx_text_width(FONT_ROUND, 13, "Clear", -1) + 36, uw = gfx_text_width(FONT_ROUND, 13, "Undo", -1) + 36;
        pill_btn(ID_CLEAR, right - cw, r1, th, "Clear", false);
        pill_btn(ID_UNDO, right - cw - 8 - uw, r1, th, "Undo", false);
        float r2 = r1 + th + 12;
        ui_text(FONT_MENU, 12, 1.2f, x0, r2, th, COL_SOFT, "Feeling");
        compose_feelings(x0 + 64, r2 + (th - 34) / 2, 38, 34, 8, false, feeling, ID_FEEL, 1);
        float pw = gfx_text_width(FONT_ROUND, 13, "Post drawing", -1) + 36;
        float px = right - pw;
        pill_btn(ID_POST, px, r2, th, "Post drawing", true);
        const char *sp = spoiler ? "Spoilers: on" : "Spoilers: off";
        float sw = gfx_text_width_sp(FONT_MENU, 12, 1.2f, sp, -1) + 16;
        float sx = px - 8 - sw;
        if (ptr_hot(ID_SPOIL, sx, r2, sw, th)) gfx_rect(sx, r2, sw, th, rgba(140, 160, 180, 50));
        ui_text(FONT_MENU, 12, 1.2f, sx + 8, r2, th, spoiler ? CV_T_ORANGE : COL_SOFT, sp);
    }
    const char *h[] = { "S:Pen", "A:Draw", "X:Eraser", "LR:Size", "Z:Undo", "ST:Post", "B:Back" };
    ui_hints(h, 7, 1, input_stick_dir());
    compose_sending_draw(send.state == JS_LOADING ? 1 : 0);
}

const Screen screen_draw = { enter, update, draw, leave };
