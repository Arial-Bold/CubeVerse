/* communities.c - the communities: Popular & new, or every community, in
 * two columns of rows: the community's square icon on the left, its name
 * and followers beside it, dashed lines between the rows. */
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

static const char *const TABS[2] = { "Popular & New", "All" };
static JPage page;
static int tab;
static float scroll, target, content_h;
static float item_top[JPAGE_COMMUNITIES], item_bot[JPAGE_COMMUNITIES];

#define ID_TAB 10
#define ID_TILE 1000
#define ROW_H 76.0f       /* one community */
#define COL_GAP 22.0f
#define HEAD_H 34.0f      /* a section's heading bar */

static void load(void) {
    juxt_load(&page, tab ? JP_COMMUNITIES_ALL : JP_COMMUNITIES, "");
    scroll = target = 0;
}

static void enter(const char *arg, bool back) {
    (void)arg;
    if (!back || page.state != JS_READY) {
        if (!back) tab = 0;
        load();
    }
}

static int cols(void) { return 2; }

static void set_tab(int t) {
    t = (t + 2) % 2;
    if (t == tab) return;
    tab = t;
    sound_play(SFX_SELECT);
    load();
}

static void update(void) {
    int c = ptr_clicked();
    if (c >= ID_TAB && c < ID_TAB + 2) { set_tab(c - ID_TAB); return; }
    if (input_pressed(BTN_L)) { set_tab(tab - 1); return; }
    if (input_pressed(BTN_R)) { set_tab(tab + 1); return; }
    if (input_pressed(BTN_B)) { sound_play(SFX_BACK); app_back(); return; }
    if (page.state == JS_FAILED && input_pressed(BTN_A)) { sound_play(SFX_SELECT); load(); return; }
    if (page.state != JS_READY) return;
    if (c >= ID_TILE && c < ID_TILE + page.ncomms) {
        sound_play(SFX_SELECT);
        JCommunity *cm = &page.comms[c - ID_TILE];
        char arg[96];
        snprintf(arg, sizeof(arg), "%s|%.60s", cm->id, cm->name);
        app_go(SCR_COMMUNITY, arg);
        return;
    }
    float view = CV_BOTTOM - 86;
    target += ptr_scroll(86, CV_BOTTOM);
    int o = ptr_over() - ID_TILE;
    if (o >= 0 && o < page.ncomms && (input_pressed(BTN_UP) || input_pressed(BTN_DOWN))) {
        if (item_top[o] - target < 0) target = item_top[o] - 8;
        else if (item_bot[o] - target > view) target = item_bot[o] - view + 8;
    }
    float maxs = content_h - view + 24;
    if (maxs < 0) maxs = 0;
    if (target < 0) target = 0;
    if (target > maxs) target = maxs;
    scroll += (target - scroll) * clamp01(g_dt * 12);
}

/* a row: the icon framed on the left, the name and followers beside it */
static void row(const JCommunity *cm, float x, float y, float w, bool hot, float al) {
    float is = ROW_H - 18, ix = x + 4, iy = y + 9;
    gfx_rect(ix + 2, iy + 3, is, is, rgba(0, 0, 0, (u8)(110 * al)));                  /* shadow */
    gfx_rect(ix - 2, iy - 2, is + 4, is + 4, rgba(0xdf, 0xe4, 0xee, (u8)(235 * al)));  /* frame, 2 wide */
    if (cm->icon[0]) img_draw_cover(cm->icon, IMG_SMALL, ix, iy, is, is, al);
    else gfx_rect_vgrad(ix, iy, is, is, rgba(0x7D, 0xC8, 0xF5, (u8)(255 * al)), rgba(0x3A, 0x7F, 0xD6, (u8)(255 * al)));
    float tx = ix + is + 14, tw = x + w - tx - 4;
    ui_text_fit_scroll(FONT_CUBE, 16, 0, tx, iy + 1, tw, 24, rgba(0xF2, 0xF5, 0xFF, (u8)(255 * al)), cm->name);
    /* followers in a soft bar, as on the Wii U */
    if (cm->followers[0]) {
        float bw = gfx_text_width(FONT_ROUND, 12, cm->followers, -1) + 24;
        if (bw < 110) bw = 110;
        if (bw > tw) bw = tw;
        float by = iy + is - 22;
        gfx_rect(tx, by, bw, 20, rgba(0x8a, 0x94, 0xa8, (u8)(110 * al)));
        ui_text_fit_scroll(FONT_ROUND, 12, 0, tx + 10, by, bw - 16, 20, rgba(0xdf, 0xe4, 0xee, (u8)(255 * al)), cm->followers);
    }
    if (hot) ui_gold_ring(x - 4, y + 3, w + 8, ROW_H - 6, 0.5f + 0.5f * sinf(g_time * 4), al);
}

/* a dashed line between rows */
static void dashes(float x, float y, float w, float al) {
    for (float d = x; d < x + w; d += 10) gfx_rect(d, y, (d + 6 < x + w ? 6 : x + w - d), 2, rgba(200, 210, 230, (u8)(60 * al)));
}

/* a section's heading: a band of colour with the name on it */
static void heading(const char *name, float x, float y, float w, float al) {
    float h = HEAD_H - 8;
    float xy[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
    Color c4[4] = { rgba(0x2F, 0xA6, 0xE0, (u8)(230 * al)), rgba(0x2F, 0xA6, 0xE0, (u8)(70 * al)),
                    rgba(0x1D, 0x6A, 0xA6, (u8)(70 * al)), rgba(0x1D, 0x6A, 0xA6, (u8)(230 * al)) };
    gfx_quad_colors(xy, c4);
    ui_text(FONT_CUBE, 15, 0.4f, x + 12, y, h, rgba(255, 255, 255, (u8)(255 * al)), name);
}

static void draw(float open) {
    (void)open;
    cv_background(1);
    char right[48] = "";
    if (page.state == JS_READY) snprintf(right, sizeof(right), "%d communit%s", page.ncomms, page.ncomms == 1 ? "y" : "ies");
    cv_title("Communities", CV_T_BLUE, right, 1);
    cv_tabs(CV_L + 10, 52, TABS, 2, tab, ID_TAB, 1);
    float x = CV_L + 18, w = UI_W - 2 * x, y = 86, bottom = CV_BOTTOM;
    if (page.state == JS_LOADING || page.state == JS_IDLE) cv_center_note(x, y, w, bottom - y, "Loading", NULL, true, 1);
    else if (page.state == JS_FAILED)
        cv_center_note(x, y, w, bottom - y, "Juxt couldn't be reached", page.error[0] ? page.error : "Press A to try again.", false, 1);
    else if (page.ncomms == 0) cv_center_note(x, y, w, bottom - y, "No communities", "Juxt didn't list any communities.", false, 1);
    else {
        int nc = cols();
        float cw = (w - COL_GAP * (nc - 1)) / nc;
        cv_list_begin(x, y, w, bottom);
        float top = y + 4 - scroll;
        int col = 0;
        float yy = top;
        for (int i = 0; i < page.ncomms; i++) {
            /* Popular & New: a heading for each part */
            if (tab == 0 && page.npopular > 0 && (i == 0 || i == page.npopular)) {
                if (col) { yy += ROW_H; col = 0; }
                if (yy > y - HEAD_H && yy < bottom) heading(i == 0 ? "Popular Communities" : "New Communities", x, yy, w, 1);
                yy += HEAD_H;
            }
            float tx = x + col * (cw + COL_GAP), ty = yy;
            item_top[i] = ty - y + scroll;
            item_bot[i] = item_top[i] + ROW_H;
            if (ty + ROW_H > y - 10 && ty < bottom + 10) {
                float vy = ty < y ? y : ty, vb = ty + ROW_H > bottom ? bottom : ty + ROW_H;
                bool hot = vb > vy && ptr_hot(ID_TILE + i, tx - 4, vy, cw + 8, vb - vy);
                row(&page.comms[i], tx, ty, cw, hot, 1);
                /* the line under it, unless the next thing is a heading or the end */
                int sec_end = tab == 0 && page.npopular > 0 && i < page.npopular ? page.npopular : page.ncomms;
                bool last_row = i - col + nc >= sec_end;
                if (!last_row) dashes(tx, ty + ROW_H - 1, cw, 1);
            }
            if (++col == nc) { col = 0; yy += ROW_H; }
        }
        if (col) yy += ROW_H;
        content_h = yy - top;
        cv_list_end(content_h - scroll > bottom - y + 4, 1);
    }
    const char *h[] = { "S:Pointer", "LR:Tab", "B:Back", "A:Open" };
    ui_hints(h, 4, 1, input_stick_dir());
}

const Screen screen_communities = { enter, update, draw, NULL };
