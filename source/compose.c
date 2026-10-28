/* compose.c - writing a post or a comment: the text, a feeling, spoilers,
 * and the on-screen keyboard. */
#include "app.h"
#include "compose.h"
#include "cvui.h"
#include "keyboard.h"
#include "pointer.h"
#include "input.h"
#include "sound.h"
#include "juxt.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_TEXT 280

/* ---------------------------------------------------------- shared parts */

void post_target_parse(PostTarget *t, const char *arg) {
    memset(t, 0, sizeof(*t));
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", arg ? arg : "");
    char *f[4] = { 0 };
    int n = 0;
    char *p = buf;
    while (n < 4) {
        f[n++] = p;
        char *bar = strchr(p, '|');
        if (!bar) break;
        *bar = 0;
        p = bar + 1;
    }
    if (n >= 1 && f[0][0] == 'r') {
        t->reply = true;
        if (n > 1) snprintf(t->post_id, sizeof(t->post_id), "%s", f[1]);
        if (n > 2) snprintf(t->cid, sizeof(t->cid), "%s", f[2]);
        if (n > 3) snprintf(t->name, sizeof(t->name), "%s", f[3]);
    } else {
        if (n > 1) snprintf(t->cid, sizeof(t->cid), "%s", f[1]);
        if (n > 2) snprintf(t->name, sizeof(t->name), "%s", f[2]);
    }
}

void post_target_title(const PostTarget *t, char *out, size_t cap) {
    const char *what = t->reply ? "Comment" : "New post";
    if (t->name[0]) snprintf(out, cap, "%s \x7f %s", what, t->name);
    else snprintf(out, cap, "%s", what);
}

void post_target_arg(const PostTarget *t, char *out, size_t cap) {
    if (t->reply) snprintf(out, cap, "r|%s|%s|%.40s", t->post_id, t->cid, t->name);
    else snprintf(out, cap, "c|%s|%.60s", t->cid, t->name);
}

void compose_sending_draw(float al) {
    if (al <= 0) return;
    gfx_fill_screen(rgba(0, 0, 0, (u8)(0.5f * 255 * al)));
    float w = 300, h = 110, x = UI_W / 2 - w / 2, y = 240 - h / 2 - 20;
    cv_card(x, y, w, h, false, al);
    ui_spinner(x + w / 2, y + 40, 12, g_time, al);
    ui_text_center(FONT_ROUND, 15, 0, x + w / 2, y + 66, 24, color_alpha(CV_INK, al), "Posting...");
}

void compose_tabs(int on, int base) {
    static const char *const T[2] = { "Text", "Drawing" };
    float w0 = gfx_text_width_sp(FONT_MENU, 13, 1.4f, T[0], -1) + 24, w1 = gfx_text_width_sp(FONT_MENU, 13, 1.4f, T[1], -1) + 24;
    cv_tabs(UI_W - CV_L - 16 - (w0 + w1 + 6), CV_TOP + 4, T, 2, on, base, 1);
}

void compose_feelings(float x, float y, float bw, float bh, float gap, bool column, int on, int base, float al) {
    for (int i = 0; i < 6; i++) {
        float fx = column ? x : x + i * (bw + gap), fy = column ? y + i * (bh + gap) : y;
        bool hot = ptr_hot(base + i, fx, fy, bw, bh);
        gfx_rect(fx - 1, fy - 1, bw + 2, bh + 2, rgba(0, 0, 0, (u8)(0.4f * 255 * al)));
        gfx_rect(fx, fy, bw, bh, i == on ? color_alpha(CV_YEAH, al) : rgba(255, 255, 255, (u8)(255 * al)));
        cv_feeling(i, fx + bw / 2, fy + bh / 2, 8.5f, i == on ? rgba(255, 255, 255, 255) : rgba(0x4A, 0x51, 0x70, 255), al);
        if (hot) ui_gold_ring(fx, fy, bw, bh, 0.5f + 0.5f * sinf(g_time * 4), al);
    }
}

/* the feelings as a grid of bigger buttons, each face with its name under it */
void compose_feelings_grid(float x, float y, int cols, float bw, float bh, float gap, int on, int base, float al) {
    static const char *const NAMES[6] = { "Normal", "Happy", "Wink", "Surprised", "Frustrated", "Puzzled" };
    for (int i = 0; i < 6; i++) {
        float fx = x + (i % cols) * (bw + gap), fy = y + (i / cols) * (bh + gap);
        bool hot = ptr_hot(base + i, fx, fy, bw, bh);
        bool sel = i == on;
        gfx_rect(fx, fy + 2, bw, bh, rgba(0, 0, 0, (u8)(0.45f * 255 * al)));
        gfx_rect(fx, fy, bw, bh, sel ? color_alpha(CV_YEAH, al) : rgba(255, 255, 255, (u8)(255 * al)));
        Color ink = sel ? rgba(255, 255, 255, 255) : rgba(0x4A, 0x51, 0x70, 255);
        cv_feeling(i, fx + bw / 2, fy + bh / 2 - 6, bh * 0.27f, ink, al);
        ui_text_center(FONT_MENU_BOLD, 9, 0.6f, fx + bw / 2, fy + bh - 15, 12,
                       sel ? rgba(255, 255, 255, (u8)(255 * al)) : rgba(0x6A, 0x71, 0x90, (u8)(255 * al)), NAMES[i]);
        if (hot) ui_gold_ring(fx, fy, bw, bh, 0.5f + 0.5f * sinf(g_time * 4), al);
    }
}

/* ---------------------------------------------------------- the screen */

static PostTarget target;
static char text[MAX_TEXT + 1];
static Kbd kbd;
static int feeling;
static bool spoiler;
static JResult send;
static float discard_until;

#define ID_TAB 10
#define ID_FEEL 30
#define ID_SPOIL 40
#define ID_POST 41
#define ID_KEYS 100

static void enter(const char *arg, bool back) {
    if (back) return;
    post_target_parse(&target, arg);
    text[0] = 0;
    kbd_init(&kbd, text, sizeof(text));
    kbd.ok_label = "Post";
    feeling = 0;
    spoiler = false;
    send.state = JS_IDLE;
}

static void post(void) {
    if (send.state == JS_LOADING) return;
    if (!text[0]) { app_toast("Write something first."); sound_play(SFX_ERROR); return; }
    sound_play(SFX_CONFIRM);
    juxt_post_text(&send, target.cid, target.reply ? target.post_id : NULL, text, feeling, spoiler);
}

static void update(void) {
    if (send.state == JS_LOADING) return;
    if (send.state == JS_READY) {
        send.state = JS_IDLE;
        g_posted = true;
        app_toast(target.reply ? "Your comment was posted." : "Your post is up!");
        app_back();
        return;
    }
    if (send.state == JS_FAILED) {
        send.state = JS_IDLE;
        sound_play(SFX_ERROR);
        app_toast(send.msg[0] ? send.msg : "That didn't post. Try again.");
    }
    int c = ptr_clicked();
    if (c == ID_TAB + 1) {
        sound_play(SFX_SELECT);
        char arg[96];
        post_target_arg(&target, arg, sizeof(arg));
        app_swap(SCR_DRAW, arg);
        return;
    }
    if (c >= ID_FEEL && c < ID_FEEL + 6) { feeling = c - ID_FEEL; sound_play(SFX_SELECT); return; }
    if (c == ID_SPOIL) { spoiler = !spoiler; sound_play(SFX_SELECT); return; }
    if (c == ID_POST || input_pressed(BTN_START)) { post(); return; }
    if (input_pressed(BTN_B)) {
        if (text[0] && g_time > discard_until) {
            discard_until = g_time + 3;
            sound_play(SFX_BACK);
            app_toast("Press B again to throw this away.");
            return;
        }
        sound_play(SFX_BACK);
        app_back();
        return;
    }
    kbd_update(&kbd, c, ID_KEYS);
    if (kbd.ok) { kbd.ok = false; post(); }
}

/* the text, wrapped, with the cursor; scrolled so the cursor's line shows */
static void draw_text(float x, float y, float w, float h, float al) {
    cv_card(x, y, w, h, false, al);
    gfx_rrect_outline(x, y, w, h, 0, 2, rgba(0x29, 0xA8, 0xE0, (u8)(255 * al)));
    char disp[MAX_TEXT + 4];
    int cur = kbd.cursor;
    snprintf(disp, sizeof(disp), "%.*s%c%s", cur, text, fmodf(g_time, 1.0f) < 0.6f ? '|' : ' ', text + cur);
    float tw = w - 24, line = 20;
    int max_lines = (int)((h - 16) / line);
    /* start at a line break so the cursor's line is within max_lines */
    int start = 0;
    char pre[MAX_TEXT + 4];
    for (;;) {
        snprintf(pre, sizeof(pre), "%.*s", cur + 1 - start, disp + start);
        int lines = cv_wrap(FONT_ROUND, 14, 0, pre, 0, 0, tw, line, 99, CV_INK, false);
        if (lines <= max_lines) break;
        /* skip one wrapped line */
        int n = (int)strcspn(disp + start, "\n");
        int k = gfx_text_fit(FONT_ROUND, 14, disp + start, n, tw);
        if (k < n) { int b = k; while (b > 0 && disp[start + b] != ' ') b--; if (b > 0) k = b; }
        if (k <= 0) k = 1;
        start += k;
        while (disp[start] == ' ' || disp[start] == '\n') start++;
    }
    if (!text[0] && fmodf(g_time, 1.0f) >= 0.6f)
        ui_text(FONT_ROUND, 14, 0, x + 12, y + 8, line, color_alpha(CV_INK2, al), "Write something...");
    cv_wrap(FONT_ROUND, 14, 0, disp + start, x + 12, y + 8, tw, line, max_lines, color_alpha(CV_INK, al), true);
}

static void draw(float open) {
    (void)open;
    cv_background(1);
    char title[96];
    post_target_title(&target, title, sizeof(title));
    cv_title(title, CV_T_BLUE, NULL, 1);
    compose_tabs(0, ID_TAB);
    float x = cv_side(), w = UI_W - 2 * x;
    draw_text(x, 54, w, 88, 1);
    /* the count, in the text box's corner */
    char cnt[16];
    snprintf(cnt, sizeof(cnt), "%d / %d", (int)strlen(text), MAX_TEXT);
    ui_text_right(FONT_ROUND, 11, 0, x + w - 10, 54 + 88 - 20, 16, CV_INK2, cnt);
    /* feelings at the left, in two columns, and the spoilers switch under them */
    float fy = 168, bw = 54, bh = 48, fg = 6;
    ui_text(FONT_MENU_BOLD, 11, 1.2f, x, fy - 18, 16, COL_SOFT, "Feeling");
    compose_feelings_grid(x, fy, 2, bw, bh, fg, feeling, ID_FEEL, 1);
    const char *sp = spoiler ? "Spoilers: on" : "Spoilers: off";
    float sy = fy + 3 * (bh + fg) + 6, sw = 2 * bw + fg;
    bool hs = ptr_hot(ID_SPOIL, x, sy, sw, 28);
    if (hs) gfx_rect(x, sy, sw, 28, rgba(140, 160, 180, 50));
    ui_text(FONT_MENU_BOLD, 11, 1.2f, x + 4, sy, 26, spoiler ? CV_T_ORANGE : COL_SOFT, sp);
    gfx_rect(x, sy + 26, sw, 2, spoiler ? CV_T_ORANGE : rgba(60, 70, 100, 255));
    /* the keyboard beside them (its OK key posts) */
    float kx = x + sw + 12;
    kbd_draw(&kbd, kx, 150, x + w - kx, 34, 5, ID_KEYS, 1);
    const char *h[] = { "A:Type", "X:Delete", "Y:Space", "LR:Cursor", "ST:Post", "B:Cancel" };
    ui_hints(h, 6, 1, -1);
    compose_sending_draw(send.state == JS_LOADING ? 1 : 0);
}

const Screen screen_compose = { enter, update, draw, NULL };
