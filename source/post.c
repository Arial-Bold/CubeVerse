/* post.c - one post, big, with its comments under it. */
#include "app.h"
#include "cvui.h"
#include "postlist.h"
#include "pointer.h"
#include "input.h"
#include "sound.h"
#include "juxt.h"
#include <stdio.h>
#include <string.h>

static JPage page;
static PostList list;
static char pid[40];

#define ID_REPLY 20
#define ID_LIST 1000

static void load(void) {
    juxt_load(&page, JP_POST, pid);
    pl_reset_scroll(&list);
}

static void enter(const char *arg, bool back) {
    if (!back) snprintf(pid, sizeof(pid), "%s", arg);
    if (!back || page.state != JS_READY || g_posted) {
        pl_init(&list, &page, 1, ID_LIST);
        list.big_first = true;
        load();
    }
    g_posted = false;
    pl_empty_text = "This post couldn't be found.";
}

static void reply(void) {
    if (page.state != JS_READY || page.nposts < 1) return;
    if (!juxt_signed_in()) { app_toast("Sign in to comment."); sound_play(SFX_ERROR); return; }
    sound_play(SFX_SELECT);
    JPost *p = &page.posts[0];
    char arg[96];
    snprintf(arg, sizeof(arg), "r|%s|%s|%.40s", p->id, p->community_id, p->community);
    app_go(SCR_COMPOSE, arg);
}

static void update(void) {
    int c = ptr_clicked();
    if (c == ID_REPLY || input_pressed(BTN_Y)) { reply(); return; }
    if (input_pressed(BTN_B)) { sound_play(SFX_BACK); app_back(); return; }
    if (page.state == JS_FAILED && input_pressed(BTN_A)) { sound_play(SFX_SELECT); load(); return; }
    /* clicking the post itself does nothing (it's already open) */
    if (c == ID_LIST) return;
    if (page.state == JS_READY && page.nposts > 0 && pl_over(&list) < 0 && input_pressed(BTN_X)) { pl_yeah(&list, 0); return; }
    pl_update(&list);
}

static void draw(float open) {
    (void)open;
    cv_background(1);
    char right[48] = "";
    const char *title = "Post";
    if (page.state == JS_READY && page.nposts > 0) {
        title = page.posts[0].community[0] ? page.posts[0].community : "Post";
        snprintf(right, sizeof(right), "%d comment%s", page.posts[0].replies, page.posts[0].replies == 1 ? "" : "s");
    }
    cv_title(title, CV_T_BLUE, right, 1);
    float x = cv_side(), w = UI_W - 2 * x;
    pl_draw(&list, x, 52, w, CV_BOTTOM, 1);
    const char *h[] = { "S:Pointer", "X:Yeah!", "Y:Comment", "Z:Their page", "B:Back" };
    ui_hints(h, 5, 1, input_stick_dir());
}

const Screen screen_post = { enter, update, draw, NULL };
