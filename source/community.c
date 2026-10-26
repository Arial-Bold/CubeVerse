/* community.c - one community: its icon, description, Follow and Post
 * buttons, and its posts (newest or most popular) as tiles. */
#include "app.h"
#include "cvui.h"
#include "postlist.h"
#include "pointer.h"
#include "imgcache.h"
#include "input.h"
#include "sound.h"
#include "juxt.h"
#include <stdio.h>
#include <string.h>

static const char *const TABS[2] = { "New posts", "Popular" };
static JPage page;
static PostList list;
static int tab;
static char cid[24], cname[64];
static JResult follow;

#define ID_TAB 10
#define ID_FOLLOW 20
#define ID_POST 21
#define ID_LIST 1000

static void load(void) {
    juxt_load(&page, tab ? JP_COMMUNITY_HOT : JP_COMMUNITY_NEW, cid);
    pl_reset_scroll(&list);
}

static void enter(const char *arg, bool back) {
    if (!back) {
        const char *bar = strchr(arg, '|');
        snprintf(cid, sizeof(cid), "%.*s", bar ? (int)(bar - arg) : (int)strlen(arg), arg);
        snprintf(cname, sizeof(cname), "%s", bar ? bar + 1 : "");
        tab = 0;
    }
    if (!back || page.state != JS_READY || g_posted) {
        pl_init(&list, &page, UI_W > 700 ? 3 : 2, ID_LIST);
        load();
    }
    g_posted = false;
    pl_empty_text = "Nobody has posted here yet. Press Y to be the first.";
}

static void new_post(void) {
    if (!juxt_signed_in()) { app_toast("Sign in to post."); sound_play(SFX_ERROR); return; }
    sound_play(SFX_SELECT);
    char arg[96];
    snprintf(arg, sizeof(arg), "c|%s|%.60s", cid, page.title[0] ? page.title : cname);
    app_go(SCR_COMPOSE, arg);
}

static void toggle_follow(void) {
    if (!juxt_signed_in()) { app_toast("Sign in to follow communities."); sound_play(SFX_ERROR); return; }
    if (follow.state == JS_LOADING) return;
    page.following = !page.following;
    sound_play(page.following ? SFX_CONFIRM : SFX_CANCEL);
    juxt_follow_community(&follow, cid);
}

static void update(void) {
    if (follow.state == JS_READY) { page.following = follow.on; follow.state = JS_IDLE; }
    else if (follow.state == JS_FAILED) {
        page.following = !page.following;
        app_toast(follow.msg[0] ? follow.msg : "That didn't go through.");
        follow.state = JS_IDLE;
    }
    int c = ptr_clicked();
    if (c >= ID_TAB && c < ID_TAB + 2) {
        if (c - ID_TAB != tab) { tab = c - ID_TAB; sound_play(SFX_SELECT); load(); }
        return;
    }
    if (input_pressed(BTN_L) || input_pressed(BTN_R)) { tab ^= 1; sound_play(SFX_SELECT); load(); return; }
    if (c == ID_FOLLOW) { toggle_follow(); return; }
    if (c == ID_POST || input_pressed(BTN_Y)) { new_post(); return; }
    if (input_pressed(BTN_B)) { sound_play(SFX_BACK); app_back(); return; }
    if (page.state == JS_FAILED && input_pressed(BTN_A)) { sound_play(SFX_SELECT); load(); return; }
    pl_update(&list);
}

static void draw(float open) {
    (void)open;
    cv_background(1);
    const char *name = page.title[0] ? page.title : cname;
    cv_title(name[0] ? name : "Community", CV_T_BLUE, page.stat1, 1);
    float x = CV_L + 18, w = UI_W - 2 * x;
    /* header: icon, description, buttons */
    float hy = 52;   /* one short row (half the old height) */
    if (page.icon[0]) img_draw_cover(page.icon, IMG_SMALL, x, hy, 22, 22, 1);
    else gfx_rect(x, hy, 22, 22, rgba(0x2F, 0xA6, 0xE0, 255));
    float bx = x + w;
    float pw = gfx_text_width(FONT_ROUND, 13, "+ Post here", -1) + 28;
    bx -= pw;
    bool hp = ptr_hot(ID_POST, bx, hy, pw, 22);
    cv_pill(bx, hy, pw, 22, "+ Post here", rgba(0x29, 0xA8, 0xE0, 255), rgba(255, 255, 255, 255), hp, 1);
    const char *fl = page.following ? "Following" : "Follow";
    float fw = gfx_text_width(FONT_ROUND, 13, fl, -1) + 28;
    bx -= fw + 8;
    bool hf = ptr_hot(ID_FOLLOW, bx, hy, fw, 22);
    if (page.following) {
        gfx_rect_vgrad(bx, hy, fw, 22, rgba(140, 160, 180, 90), rgba(70, 90, 110, 60));
        gfx_rrect_outline(bx, hy, fw, 22, 0, 2, rgba(215, 225, 240, 150));
        ui_text_center(FONT_ROUND, 13, 0, bx + fw / 2, hy, 22, rgba(0xdf, 0xe4, 0xee, 255), fl);
        if (hf) ui_gold_ring(bx, hy, fw, 22, 0.5f, 1);
    } else {
        cv_pill(bx, hy, fw, 22, fl, rgba(255, 255, 255, 255), CV_INK, hf, 1);
    }
    const char *desc = page.desc[0] ? page.desc : (page.state == JS_READY ? "" : " ");
    ui_text_fit_scroll(FONT_MENU, 12, 1.0f, x + 32, hy, bx - 12 - (x + 32), 22, rgba(0xdf, 0xe4, 0xee, 255), desc);
    cv_tabs(CV_L + 10, 80, TABS, 2, tab, ID_TAB, 1);
    pl_draw(&list, x, 114, w, CV_BOTTOM, 1);
    const char *h[] = { "S:Pointer", "LR:Tab", "X:Yeah!", "Y:Post", "Z:Their page", "B:Back", "A:Open" };
    ui_hints(h, 7, 1, input_stick_dir());
}

const Screen screen_community = { enter, update, draw, NULL };
