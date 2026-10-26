/* communities.c - the communities: Popular & new, or every community, as
 * tiles with the community's icon on a colour band. */
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
#define TILE_H 112.0f
#define GAP 14.0f

/* soft colour bands, picked from the name */
static const Color BANDS[][2] = {
    { { 0x7D, 0xC8, 0xF5, 255 }, { 0x3A, 0x7F, 0xD6, 255 } }, { { 0xB9, 0xA2, 0xFF, 255 }, { 0x6F, 0x4F, 0xD6, 255 } },
    { { 0xFF, 0xB3, 0x7A, 255 }, { 0xE8, 0x6A, 0x3A, 255 } }, { { 0x8F, 0xDD, 0xB6, 255 }, { 0x2F, 0xA6, 0x78, 255 } },
    { { 0xF5, 0x9A, 0xC0, 255 }, { 0xC9, 0x4C, 0x86, 255 } }, { { 0x9F, 0xD8, 0xFF, 255 }, { 0x29, 0xA8, 0xE0, 255 } },
    { { 0xFF, 0xD3, 0x6E, 255 }, { 0xE0, 0xA2, 0x1E, 255 } }, { { 0x8F, 0xE0, 0xE0, 255 }, { 0x2A, 0xA6, 0xA6, 255 } },
};

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

static int cols(void) { return UI_W > 700 ? 4 : 3; }

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

static void tile(const JCommunity *cm, float x, float y, float w, bool hot, float al) {
    unsigned hsh = 0;
    for (const char *p = cm->name; *p; p++) hsh = hsh * 31 + (unsigned char)*p;
    const Color *band = BANDS[hsh % (sizeof(BANDS) / sizeof(BANDS[0]))];
    cv_card(x, y, w, TILE_H, false, al);
    float bh = 62;
    float xy[8] = { x, y, x + w, y, x + w, y + bh, x, y + bh };
    Color c4[4] = { color_alpha(band[0], al), color_alpha(color_lerp(band[0], band[1], 0.5f), al), color_alpha(band[1], al),
                    color_alpha(color_lerp(band[0], band[1], 0.5f), al) };
    gfx_quad_colors(xy, c4);
    float is = 46;
    if (cm->icon[0]) {
        gfx_rect(x + w / 2 - is / 2 - 2, y + bh / 2 - is / 2 - 2, is + 4, is + 4, rgba(255, 255, 255, (u8)(200 * al)));
        img_draw_cover(cm->icon, IMG_SMALL, x + w / 2 - is / 2, y + bh / 2 - is / 2, is, is, al);
    }
    ui_text_fit_scroll(FONT_CUBE, 13, 0, x + 10, y + bh + 6, w - 20, 20, color_alpha(CV_INK, al), cm->name);
    ui_text_fit_scroll(FONT_ROUND, 11, 0, x + 10, y + bh + 26, w - 20, 16, color_alpha(CV_INK2, al), cm->followers);
    if (hot) ui_gold_ring(x, y, w, TILE_H, 0.5f + 0.5f * sinf(g_time * 4), al);
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
        float tw = (w - GAP * (nc - 1)) / nc;
        cv_list_begin(x, y, w, bottom);
        float top = y + 6 - scroll;
        int col = 0;
        float yy = top;
        for (int i = 0; i < page.ncomms; i++) {
            /* Popular & New: a heading for each part */
            if (tab == 0 && page.npopular > 0 && (i == 0 || i == page.npopular)) {
                if (col) { yy += TILE_H + GAP; col = 0; }
                if (yy > y - 30 && yy < bottom)
                    ui_text(FONT_MENU, 13, 1.4f, x + 2, yy, 22, rgba(0xc9, 0xd2, 0xee, 255), i == 0 ? "Popular" : "New");
                yy += 28;
            }
            float tx = x + col * (tw + GAP), ty = yy;
            item_top[i] = ty - y + scroll;
            item_bot[i] = item_top[i] + TILE_H;
            if (ty + TILE_H > y - 10 && ty < bottom + 10) {
                float vy = ty < y ? y : ty, vb = ty + TILE_H > bottom ? bottom : ty + TILE_H;
                bool hot = vb > vy && ptr_hot(ID_TILE + i, tx, vy, tw, vb - vy);
                tile(&page.comms[i], tx, ty, tw, hot, 1);
            }
            if (++col == nc) { col = 0; yy += TILE_H + GAP; }
        }
        if (col) yy += TILE_H + GAP;
        content_h = yy - top;
        cv_list_end(content_h - scroll > bottom - y + 4, 1);
    }
    const char *h[] = { "S:Pointer", "LR:Tab", "B:Back", "A:Open" };
    ui_hints(h, 4, 1, input_stick_dir());
}

const Screen screen_communities = { enter, update, draw, NULL };
