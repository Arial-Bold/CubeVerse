/* user.c - a person's page (or My page): their Mii, name, bio and numbers,
 * a Follow button, and their posts or Yeahs as tiles. */
#include "app.h"
#include "cvui.h"
#include "postlist.h"
#include "pointer.h"
#include "imgcache.h"
#include "input.h"
#include "sound.h"
#include "juxt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const TABS[2] = { "Posts", "Yeahs" };
static JPage page;
static PostList list;
static int tab;
static char who[24];          /* pid, or "" for me */
static JResult follow;

#define ID_TAB 10
#define ID_FOLLOW 20
#define ID_LIST 1000

static bool mine(void) { return !who[0] || (juxt_my_pid() && atoi(who) == juxt_my_pid()); }

static void load(void) {
    juxt_load(&page, tab ? JP_USER_YEAHS : JP_USER_POSTS, who[0] ? who : "me");
    pl_reset_scroll(&list);
}

static void enter(const char *arg, bool back) {
    if (!back) {
        snprintf(who, sizeof(who), "%s", arg ? arg : "");
        if (who[0] && juxt_my_pid() && atoi(who) == juxt_my_pid()) who[0] = 0;
        tab = 0;
    }
    if (!back || page.state != JS_READY || g_posted) {
        pl_init(&list, &page, UI_W > 700 ? 4 : 3, ID_LIST);
        list.show_community = true;
        load();
    }
    g_posted = false;
    pl_empty_text = tab ? "No Yeahs yet." : "No posts yet.";
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
        if (c - ID_TAB != tab) { tab = c - ID_TAB; sound_play(SFX_SELECT); load(); pl_empty_text = tab ? "No Yeahs yet." : "No posts yet."; }
        return;
    }
    if (input_pressed(BTN_L) || input_pressed(BTN_R)) {
        tab ^= 1;
        sound_play(SFX_SELECT);
        load();
        pl_empty_text = tab ? "No Yeahs yet." : "No posts yet.";
        return;
    }
    if (c == ID_FOLLOW && page.can_follow && !mine() && follow.state != JS_LOADING) {
        if (!juxt_signed_in()) { app_toast("Sign in to follow people."); sound_play(SFX_ERROR); return; }
        page.following = !page.following;
        sound_play(page.following ? SFX_CONFIRM : SFX_CANCEL);
        juxt_follow_user(&follow, page.pid ? page.pid : atoi(who));
        return;
    }
    if (input_pressed(BTN_B)) { sound_play(SFX_BACK); app_back(); return; }
    if (page.state == JS_FAILED && input_pressed(BTN_A)) { sound_play(SFX_SELECT); load(); return; }
    pl_update(&list);
}

static void stat_box(float x, float y, const char *s) {
    /* "123 followers" -> big number over a small word */
    char num[24], word[40];
    int n = (int)strcspn(s, " ");
    snprintf(num, sizeof(num), "%.*s", n < 23 ? n : 23, s);
    snprintf(word, sizeof(word), "%s", s[n] ? s + n + 1 : "");
    float w = 76;
    gfx_rect_vgrad(x, y, w, 44, rgba(140, 160, 180, 56), rgba(70, 90, 110, 36));
    gfx_rrect_outline(x, y, w, 44, 0, 1, rgba(215, 225, 240, 90));
    ui_text_center(FONT_CUBE, 16, 0, x + w / 2, y + 4, 20, COL_TEXT, num);
    ui_text_center(FONT_MENU, 10, 1.0f, x + w / 2, y + 24, 14, COL_SOFT, word);
}

static void draw(float open) {
    (void)open;
    cv_background(1);
    bool me = mine();
    cv_title(me ? "My page" : (page.title[0] ? page.title : "Profile"), CV_T_PURPLE, page.subtitle, 1);
    float x = CV_L + 18, w = UI_W - 2 * x;
    /* header */
    float hy = 54;
    const char *name = page.title[0] ? page.title : (me ? juxt_my_name() : "");
    cv_face(page.icon[0] ? page.icon : (me ? juxt_my_mii() : ""), name, x, hy, 64, 1);
    float sx = x + w;
    if (page.stat2[0]) { sx -= 76; stat_box(sx, hy + 10, page.stat2); sx -= 6; }
    if (page.stat1[0]) { sx -= 76; stat_box(sx, hy + 10, page.stat1); sx -= 6; }
    if (page.can_follow && !me) {
        const char *fl = page.following ? "Following" : "Follow";
        float fw = gfx_text_width(FONT_ROUND, 13, fl, -1) + 28;
        sx -= fw + 6;
        bool hf = ptr_hot(ID_FOLLOW, sx, hy + 20, fw, 26);
        cv_pill(sx, hy + 20, fw, 26, fl, page.following ? rgba(0x7D, 0x5B, 0xD6, 255) : rgba(255, 255, 255, 255),
                page.following ? rgba(255, 255, 255, 255) : CV_INK, hf, 1);
        sx -= 6;
    }
    float tx = x + 78, tw = sx - 10 - tx;
    ui_text_fit_scroll(FONT_CUBE, 20, 0, tx, hy + 4, tw, 26, rgba(255, 255, 255, 255), name);
    ui_text_fit_scroll(FONT_MENU, 13, 1.0f, tx, hy + 34, tw, 22, rgba(0xdf, 0xe4, 0xee, 255),
                       page.desc[0] ? page.desc : (page.state == JS_READY ? "" : " "));
    cv_tabs(CV_L + 10, 130, TABS, 2, tab, ID_TAB, 1);
    pl_draw(&list, x, 164, w, CV_BOTTOM, 1);
    if (page.can_follow && !me) {
        const char *h[] = { "S:Pointer", "LR:Tab", "X:Yeah!", "B:Back", "A:Open" };
        ui_hints(h, 5, 1, input_stick_dir());
    } else {
        const char *h[] = { "S:Pointer", "LR:Tab", "X:Yeah!", "B:Back", "A:Open" };
        ui_hints(h, 5, 1, input_stick_dir());
    }
}

const Screen screen_user = { enter, update, draw, NULL };
