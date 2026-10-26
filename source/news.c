/* news.c - Notifications: Yeahs, comments and new followers. */
#include "app.h"
#include "cvui.h"
#include "pointer.h"
#include "imgcache.h"
#include "input.h"
#include "sound.h"
#include "juxt.h"
#include "m3d.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static JPage page;
static float scroll, target, content_h;
static float item_top[JPAGE_NEWS], item_bot[JPAGE_NEWS];

#define ID_ROW 1000
#define ROW_H 58.0f
#define GAP 9.0f

static void load(void) {
    juxt_load(&page, JP_NEWS, "");
    scroll = target = 0;
}

static void enter(const char *arg, bool back) {
    (void)arg;
    if (!back || page.state != JS_READY) load();
}

static void open_link(const char *link) {
    const char *p;
    if ((p = strstr(link, "/posts/"))) {
        char id[40];
        snprintf(id, sizeof(id), "%.*s", (int)strcspn(p + 7, "/?#"), p + 7);
        sound_play(SFX_SELECT);
        app_go(SCR_POST, id);
    } else if ((p = strstr(link, "/users/"))) {
        char id[24];
        snprintf(id, sizeof(id), "%.*s", (int)strcspn(p + 7, "/?#"), p + 7);
        sound_play(SFX_SELECT);
        app_go(SCR_USER, id);
    } else {
        sound_play(SFX_ERROR);
    }
}

static void update(void) {
    if (page.state == JS_READY && g_unread) g_unread = 0;   /* opening the list reads them */
    if (input_pressed(BTN_B)) { sound_play(SFX_BACK); app_back(); return; }
    if (page.state == JS_FAILED && input_pressed(BTN_A)) { sound_play(SFX_SELECT); load(); return; }
    if (page.state != JS_READY) return;
    int c = ptr_clicked();
    if (c >= ID_ROW && c < ID_ROW + page.nnews) { open_link(page.news[c - ID_ROW].link); return; }
    float view = CV_BOTTOM - 52;
    target += ptr_scroll(52, CV_BOTTOM);
    int o = ptr_over() - ID_ROW;
    if (o >= 0 && o < page.nnews && (input_pressed(BTN_UP) || input_pressed(BTN_DOWN))) {
        if (item_top[o] - target < 0) target = item_top[o] - 8;
        else if (item_bot[o] - target > view) target = item_bot[o] - view + 8;
    }
    float maxs = content_h - view + 24;
    if (maxs < 0) maxs = 0;
    if (target < 0) target = 0;
    if (target > maxs) target = maxs;
    scroll += (target - scroll) * clamp01(g_dt * 12);
    if (page.next[0] && !page.more_loading && content_h - scroll < view + 200) juxt_load_more(&page);
}

static void draw(float open) {
    (void)open;
    cv_background(1);
    char right[32] = "";
    if (g_unread > 0) snprintf(right, sizeof(right), "%d new", g_unread);
    cv_title("Notifications", CV_T_ORANGE, right, 1);
    float x = cv_side(), w = UI_W - 2 * x, y = 52, bottom = CV_BOTTOM;
    if (page.state == JS_LOADING || page.state == JS_IDLE) cv_center_note(x, y, w, bottom - y, "Loading", NULL, true, 1);
    else if (page.state == JS_FAILED)
        cv_center_note(x, y, w, bottom - y, "Juxt couldn't be reached", page.error[0] ? page.error : "Press A to try again.", false, 1);
    else if (page.nnews == 0)
        cv_center_note(x, y, w, bottom - y, "No notifications", "When someone gives you a Yeah, comments or follows you, it shows up here.", false, 1);
    else {
        cv_list_begin(x, y, w, bottom);
        float yy = y + 6 - scroll;
        for (int i = 0; i < page.nnews; i++) {
            JNews *n = &page.news[i];
            item_top[i] = yy - y + scroll;
            item_bot[i] = item_top[i] + ROW_H;
            if (yy + ROW_H > y - 10 && yy < bottom + 10) {
                float vy = yy < y ? y : yy, vb = yy + ROW_H > bottom ? bottom : yy + ROW_H;
                bool hot = vb > vy && ptr_hot(ID_ROW + i, x, vy, w, vb - vy);
                cv_card(x, yy, w, ROW_H, hot, 1);
                cv_face(n->icon, n->text, x + 12, yy + 11, 36, 1);
                cv_wrap(FONT_ROUND, 13, 0, n->text, x + 60, yy + 8, w - 84, 19, 1, CV_INK, true);
                ui_text(FONT_ROUND, 11, 0, x + 60, yy + 30, 18, CV_INK2, n->when);
                gfx_circle(x + w - 16, yy + ROW_H / 2, 3, rgba(0xC9, 0xD2, 0xE6, 255));
            }
            yy += ROW_H + GAP;
        }
        if (page.more_loading && yy < bottom) ui_spinner(x + w / 2, yy + 16, 10, g_time, 1);
        content_h = yy - (y + 6 - scroll);
        cv_list_end(content_h - scroll > bottom - y + 4 || page.next[0], 1);
    }
    const char *h[] = { "S:Pointer", "B:Back", "A:Go to it" };
    ui_hints(h, 3, 1, input_stick_dir());
}

const Screen screen_news = { enter, update, draw, NULL };
