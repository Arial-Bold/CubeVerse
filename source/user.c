/* user.c - a person's page (or My page): their Mii, name, bio and numbers,
 * a Follow button, and their posts or Yeahs as tiles, or their followers
 * (also opened by clicking the followers box). */
#include "app.h"
#include "cvui.h"
#include "postlist.h"
#include "pointer.h"
#include "imgcache.h"
#include "input.h"
#include "sound.h"
#include "juxt.h"
#include "m3d.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const TABS[3] = { "Posts", "Yeahs", "Followers" };
#define NTABS 3
static JPage page;
static PostList list;
static int tab;
static char who[24];          /* pid, or "" for me */
static JResult follow;

#define ID_TAB 10
#define ID_FOLLOW 20
#define ID_FOLLOWERS 21
#define ID_LIST 1000
#define ID_PERSON 3000
#define ROW_H 60.0f

/* the followers list: its own scrolling */
static float pscroll, ptarget, pcontent_h;

static bool mine(void) { return !who[0] || (juxt_my_pid() && atoi(who) == juxt_my_pid()); }

static const char *empty_text(void) { return tab == 2 ? "No followers yet." : tab ? "No Yeahs yet." : "No posts yet."; }

static void load(void) {
    juxt_load(&page, tab == 2 ? JP_USER_FOLLOWERS : tab ? JP_USER_YEAHS : JP_USER_POSTS, who[0] ? who : "me");
    pl_reset_scroll(&list);
    pscroll = ptarget = 0;
    pl_empty_text = empty_text();
}

static void set_tab(int t) {
    t = (t + NTABS) % NTABS;
    if (t == tab) return;
    tab = t;
    sound_play(SFX_SELECT);
    load();
}

static void enter(const char *arg, bool back) {
    if (!back) {
        snprintf(who, sizeof(who), "%s", arg ? arg : "");
        if (who[0] && juxt_my_pid() && atoi(who) == juxt_my_pid()) who[0] = 0;
        tab = 0;
    }
    if (!back || page.state != JS_READY || g_posted) {
        pl_init(&list, &page, 2, ID_LIST);   /* two columns */
        list.show_community = true;
        load();
    }
    g_posted = false;
    pl_empty_text = empty_text();
}

static void update(void) {
    if (follow.state == JS_READY) { page.following = follow.on; follow.state = JS_IDLE; }
    else if (follow.state == JS_FAILED) {
        page.following = !page.following;
        app_toast(follow.msg[0] ? follow.msg : "That didn't go through.");
        follow.state = JS_IDLE;
    }
    int c = ptr_clicked();
    if (c >= ID_TAB && c < ID_TAB + NTABS) { set_tab(c - ID_TAB); return; }
    if (c == ID_FOLLOWERS) { set_tab(2); return; }
    if (input_pressed(BTN_L)) { set_tab(tab - 1); return; }
    if (input_pressed(BTN_R)) { set_tab(tab + 1); return; }
    if (c == ID_FOLLOW && page.can_follow && !mine() && follow.state != JS_LOADING) {
        if (!juxt_signed_in()) { app_toast("Sign in to follow people."); sound_play(SFX_ERROR); return; }
        page.following = !page.following;
        sound_play(page.following ? SFX_CONFIRM : SFX_CANCEL);
        juxt_follow_user(&follow, page.pid ? page.pid : atoi(who));
        return;
    }
    if (input_pressed(BTN_B)) { sound_play(SFX_BACK); app_back(); return; }
    if (page.state == JS_FAILED && input_pressed(BTN_A)) { sound_play(SFX_SELECT); load(); return; }
    if (tab == 2) {
        /* a follower: open their page */
        if (page.state == JS_READY && c >= ID_PERSON && c < ID_PERSON + page.ncomms) {
            sound_play(SFX_SELECT);
            app_go(SCR_USER, page.comms[c - ID_PERSON].id);
            return;
        }
        float view = CV_BOTTOM - 140;
        ptarget += ptr_scroll(140, CV_BOTTOM);
        float maxs = pcontent_h - view + 16;
        if (maxs < 0) maxs = 0;
        if (ptarget < 0) ptarget = 0;
        if (ptarget > maxs) ptarget = maxs;
        pscroll += (ptarget - pscroll) * clamp01(g_dt * 12);
        return;
    }
    pl_update(&list);
}

/* the followers: two columns of rows, a Mii face and the name */
static void draw_people(float x, float y, float w, float bottom) {
    if (page.state == JS_LOADING || page.state == JS_IDLE) { cv_center_note(x, y, w, bottom - y, "Loading", NULL, true, 1); return; }
    if (page.state == JS_FAILED) {
        cv_center_note(x, y, w, bottom - y, "Juxt couldn't be reached", page.error[0] ? page.error : "Press A to try again.", false, 1);
        return;
    }
    if (page.ncomms == 0) { cv_center_note(x, y, w, bottom - y, empty_text(), NULL, false, 1); return; }
    float gap = 22, cw = (w - gap) / 2;
    cv_list_begin(x, y, w, bottom);
    float top = y + 4 - pscroll;
    for (int i = 0; i < page.ncomms; i++) {
        float tx = x + (i % 2) * (cw + gap), ty = top + (i / 2) * ROW_H;
        if (ty + ROW_H < y - 10 || ty > bottom + 10) continue;
        const JCommunity *pp = &page.comms[i];
        float vy = ty < y ? y : ty, vb = ty + ROW_H > bottom ? bottom : ty + ROW_H;
        bool hot = vb > vy && ptr_hot(ID_PERSON + i, tx - 4, vy, cw + 8, vb - vy);
        float is = ROW_H - 16;
        gfx_rect(tx + 4, ty + 8 - 2, is + 4, is + 4, rgba(0xdf, 0xe4, 0xee, 235));   /* frame, 2 wide */
        cv_face(pp->icon, pp->name, tx + 6, ty + 8, is, 1);
        ui_text_fit_scroll(FONT_CUBE, 16, 0, tx + is + 20, ty, cw - is - 24, ROW_H, rgba(0xF2, 0xF5, 0xFF, 255), pp->name);
        if (i + 2 < page.ncomms)
            for (float d = tx; d < tx + cw; d += 10) gfx_rect(d, ty + ROW_H - 1, (d + 6 < tx + cw ? 6 : tx + cw - d), 2, rgba(200, 210, 230, 60));
        if (hot) ui_gold_ring(tx - 4, ty + 3, cw + 8, ROW_H - 6, 0.5f + 0.5f * sinf(g_time * 4), 1);
    }
    pcontent_h = ((page.ncomms + 1) / 2) * ROW_H + 8;
    cv_list_end(pcontent_h - pscroll > bottom - y + 4, 1);
}

/* the box's width: 70, or wider for a long value ("United States") */
static float stat_w(const char *s) {
    const char *bar = strchr(s, '|');
    int n = bar ? (int)(bar - s) : (int)strcspn(s, " ");
    float w = gfx_text_width(FONT_CUBE, 14, s, n) + 16;
    return w < 70 ? 70 : w > 150 ? 150 : w;
}

static void stat_box(float x, float y, const char *s, int id) {
    /* "123 followers" or "US|country" -> a value over a small word, in a short box */
    char num[32], word[40];
    const char *bar = strchr(s, '|');
    int n = bar ? (int)(bar - s) : (int)strcspn(s, " ");
    snprintf(num, sizeof(num), "%.*s", n < 31 ? n : 31, s);
    snprintf(word, sizeof(word), "%s", s[n] ? s + n + 1 : "");
    float w = stat_w(s), h = 36;
    bool hot = id && ptr_hot(id, x, y, w, h);
    gfx_rect_vgrad(x, y, w, h, rgba(140, 160, 180, hot ? 90 : 56), rgba(70, 90, 110, 36));
    gfx_rrect_outline(x, y, w, h, 0, 2, rgba(215, 225, 240, 90));
    if (gfx_text_width(FONT_CUBE, 14, num, -1) <= w - 8) ui_text_center(FONT_CUBE, 14, 0, x + w / 2, y + 2, 18, COL_TEXT, num);
    else ui_text_fit_scroll(FONT_CUBE, 14, 0, x + 4, y + 2, w - 8, 18, COL_TEXT, num);
    ui_text_center(FONT_MENU, 9, 1.0f, x + w / 2, y + 20, 13, COL_SOFT, word);
    if (hot) ui_gold_ring(x, y, w, h, 0.5f + 0.5f * sinf(g_time * 4), 1);
}

static void draw(float open) {
    (void)open;
    cv_background(1);
    bool me = mine();
    cv_title(me ? "My page" : (page.title[0] ? page.title : "Profile"), CV_T_PURPLE, page.subtitle, 1);
    float x = CV_L + 18, w = UI_W - 2 * x;
    /* header: one short row, with room below the title's line */
    float hy = 60, hh = 36;
    const char *name = page.title[0] ? page.title : (me ? juxt_my_name() : "");
    cv_face(page.icon[0] ? page.icon : (me ? juxt_my_mii() : ""), name, x, hy, hh, 1);
    float sx = x + w;
    if (page.stat2[0]) { sx -= stat_w(page.stat2); stat_box(sx, hy, page.stat2, 0); sx -= 6; }
    if (page.stat1[0]) { sx -= stat_w(page.stat1); stat_box(sx, hy, page.stat1, ID_FOLLOWERS); sx -= 6; }
    if (page.can_follow && !me) {
        const char *fl = page.following ? "Following" : "Follow";
        float fw = gfx_text_width(FONT_ROUND, 13, fl, -1) + 28;
        sx -= fw + 6;
        bool hf = ptr_hot(ID_FOLLOW, sx, hy + 6, fw, 24);
        cv_pill(sx, hy + 6, fw, 24, fl, page.following ? rgba(0x7D, 0x5B, 0xD6, 255) : rgba(255, 255, 255, 255),
                page.following ? rgba(255, 255, 255, 255) : CV_INK, hf, 1);
        sx -= 6;
    }
    float tx = x + hh + 10, tw = sx - 10 - tx;
    ui_text_fit_scroll(FONT_CUBE, 16, 0, tx, hy, tw, 20, rgba(255, 255, 255, 255), name);
    ui_text_fit_scroll(FONT_MENU, 11, 1.0f, tx, hy + 20, tw, 16, rgba(0xdf, 0xe4, 0xee, 255),
                       page.desc[0] ? page.desc : (page.state == JS_READY ? "" : " "));
    cv_tabs(CV_L + 10, 106, TABS, NTABS, tab, ID_TAB, 1);
    if (tab == 2) draw_people(x, 140, w, CV_BOTTOM);
    else pl_draw(&list, x, 140, w, CV_BOTTOM, 1);
    if (page.can_follow && !me) {
        const char *h[] = { "S:Pointer", "LR:Tab", "B:Back", "A:Open" };
        ui_hints(h, 4, 1, input_stick_dir());
    } else {
        const char *h[] = { "S:Pointer", "LR:Tab", "B:Back", "A:Open" };
        ui_hints(h, 4, 1, input_stick_dir());
    }
}

const Screen screen_user = { enter, update, draw, NULL };
