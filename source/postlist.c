/* postlist.c - see postlist.h. Card sizes follow the CubeVerse mockups. */
#include "postlist.h"
#include "app.h"
#include "cvui.h"
#include "pointer.h"
#include "imgcache.h"
#include "input.h"
#include "sound.h"
#include "m3d.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const char *pl_empty_text = "There are no posts here yet.";

void pl_init(PostList *pl, JPage *pg, int cols, int id_base) {
    memset(pl, 0, sizeof(*pl));
    pl->pg = pg;
    pl->cols = cols;
    pl->id_base = id_base;
    pl->yeah_idx = -1;
    pl->last_over = -1;
}

void pl_reset_scroll(PostList *pl) { pl->scroll = pl->target = 0; }

/* ------------------------------------------------------------- one card */

#define PAD 14.0f
#define FACE 42.0f
#define TEXT_SIZE 14.0f
#define LINE 20.0f
#define PAINT_W 240.0f
#define PAINT_H 90.0f
#define SHOT_W 200.0f
#define SHOT_H 120.0f
#define FOOT 22.0f

static void yeah_label(const JPost *p, char *out, size_t cap) { snprintf(out, cap, "Yeah! %d", p->yeahs); }

float pl_card(const JPost *p, float x, float y, float w, bool hot, bool yeah_hot, bool show_community, bool big,
              bool draw, float al) {
    float face = big ? 44 : FACE;
    float cx = x + PAD + face + 12, cw = w - (cx - x) - PAD;
    float cy = y + 12 + 22;
    /* body */
    float body = 0;
    bool spoiler_hidden = p->spoiler && !big;
    int max_lines = big ? 14 : 3;
    int lines = 0;
    if (p->removed) lines = 1;
    else if (spoiler_hidden) lines = 1;
    else if (p->text[0]) lines = cv_wrap(FONT_ROUND, TEXT_SIZE, 0, p->text, 0, 0, cw, LINE, max_lines, CV_INK, false);
    body += lines * LINE;
    bool paint = !spoiler_hidden && !p->removed && p->painting[0];
    bool shot = !spoiler_hidden && !p->removed && p->shot[0];
    float pw = big ? 320 : PAINT_W, ph = big ? 120 : PAINT_H;
    if (pw > cw) { ph = ph * cw / pw; pw = cw; }
    if (paint) body += (body > 0 ? 6 : 0) + ph + 2;
    float sw = big ? 320 : SHOT_W, sh = big ? 192 : SHOT_H;
    if (sw > cw) { sh = sh * cw / sw; sw = cw; }
    if (shot) body += (body > 0 ? 6 : 0) + sh;
    float h = 12 + 22 + body + 8 + FOOT + 12;
    if (h < 12 + face + 12) h = 12 + face + 12;
    if (!draw) return h;

    cv_card(x, y, w, h, hot, al);
    cv_face(p->mii, p->author, x + PAD, y + 12, face, al);
    Color ink = color_alpha(CV_INK, al), ink2 = color_alpha(CV_INK2, al);
    char right[120];
    if (show_community && p->community[0]) snprintf(right, sizeof(right), "%s \x7f %s", p->community, p->when);
    else snprintf(right, sizeof(right), "%s", p->when);
    float rw = gfx_text_width(FONT_ROUND, 11, right, -1);
    if (rw > cw * 0.55f) rw = cw * 0.55f;
    ui_text_fit_scroll(FONT_ROUND, 11, 0, x + w - PAD - rw, y + 12, rw, 18, ink2, right);
    ui_text_fit_scroll(FONT_CUBE, 14, 0, cx, y + 11, cw - rw - 10, 20, ink, p->author);
    float by = cy;
    if (p->removed) {
        ui_text(FONT_ROUND, 13, 0, cx, by, LINE, ink2, "This post was removed.");
        by += LINE;
    } else if (spoiler_hidden) {
        ui_text(FONT_ROUND, 13, 0, cx, by, LINE, color_alpha(CV_YEAH, al), "Spoilers! Open the post to see it.");
        by += LINE;
    } else if (lines) {
        cv_wrap(FONT_ROUND, TEXT_SIZE, 0, p->text, cx, by, cw, LINE, max_lines, ink, true);
        by += lines * LINE;
    }
    if (paint) {
        if (by > cy) by += 6;
        gfx_rect(cx - 1, by - 1, pw + 2, ph + 2, rgba(0xCB, 0xD3, 0xE6, (u8)(255 * al)));
        gfx_rect(cx, by, pw, ph, rgba(255, 255, 255, (u8)(255 * al)));
        img_draw_fit(p->painting, IMG_PAINTING, cx, by, pw, ph, al);
        by += ph + 2;
    }
    if (shot) {
        if (by > cy) by += 6;
        img_draw_cover(p->shot, IMG_SHOT, cx, by, sw, sh, al);
        by += sh;
    }
    /* footer */
    float fy = y + h - 12 - FOOT;
    char yl[32];
    yeah_label(p, yl, sizeof(yl));
    float pw2 = cv_pill(cx, fy, 0, FOOT, yl, p->yeahed ? CV_YEAH : rgba(0xEE, 0xF1, 0xF8, 255),
                        p->yeahed ? rgba(255, 255, 255, 255) : rgba(0x4A, 0x51, 0x70, 255), yeah_hot, al);
    char rep[32];
    snprintf(rep, sizeof(rep), p->replies == 1 ? "1 comment" : "%d comments", p->replies);
    ui_text(FONT_ROUND, 12, 0, cx + pw2 + 10, fy, FOOT, ink2, rep);
    return h;
}

void pl_card_yeah_box(const JPost *p, float x, float y, float w, bool show_community, bool big, float *bx, float *by,
                      float *bw, float *bh) {
    float h = pl_card(p, x, y, w, false, false, show_community, big, false, 1);
    float face = big ? 44 : FACE;
    char yl[32];
    yeah_label(p, yl, sizeof(yl));
    *bx = x + PAD + face + 12;
    *by = y + h - 12 - FOOT;
    *bw = gfx_text_width(FONT_ROUND, 13, yl, -1) + 28;
    *bh = FOOT;
}

/* ------------------------------------------------------------- a tile */

#define TILE_H 150.0f

static void tile(const JPost *p, float x, float y, float w, bool hot, float al) {
    cv_card(x, y, w, TILE_H, hot, al);
    Color ink = color_alpha(CV_INK, al), ink2 = color_alpha(CV_INK2, al);
    cv_face(p->mii, p->author, x + 10, y + 10, 28, al);
    float tw = gfx_text_width(FONT_ROUND, 10, p->when, -1);
    ui_text(FONT_ROUND, 10, 0, x + w - 10 - tw, y + 10, 28, ink2, p->when);
    ui_text_fit_scroll(FONT_CUBE, 13, 0, x + 46, y + 10, w - 56 - tw - 6, 28, ink, p->author);
    float bx = x + 10, by = y + 46, bw = w - 20, bh = 70;
    if (p->removed) ui_text(FONT_ROUND, 12, 0, bx, by, 20, ink2, "This post was removed.");
    else if (p->spoiler) ui_text(FONT_ROUND, 12, 0, bx, by, 20, color_alpha(CV_YEAH, al), "Spoilers!");
    else if (p->painting[0]) {
        gfx_rect(bx - 1, by - 1, bw + 2, bh + 2, rgba(0xCB, 0xD3, 0xE6, (u8)(255 * al)));
        gfx_rect(bx, by, bw, bh, rgba(255, 255, 255, (u8)(255 * al)));
        img_draw_fit(p->painting, IMG_PAINTING, bx, by, bw, bh, al);
    } else if (p->shot[0] && !p->text[0]) {
        img_draw_cover(p->shot, IMG_SHOT, bx, by, bw, bh, al);
    } else {
        cv_wrap(FONT_ROUND, 13, 0, p->text, bx, by, bw, 18, 4, ink, true);
    }
    char foot[64];
    snprintf(foot, sizeof(foot), "Yeah! %d \x7f %d comment%s", p->yeahs, p->replies, p->replies == 1 ? "" : "s");
    ui_text(FONT_ROUND, 11, 0, bx, y + TILE_H - 26, 18, p->yeahed ? color_alpha(CV_YEAH, al) : ink2, foot);
}

/* ------------------------------------------------------------- the list */

int pl_over(const PostList *pl) {
    int o = ptr_over();
    if (o < pl->id_base || o >= pl->id_base + 4 * PL_MAX) return -1;
    int i = (o - pl->id_base) / 4;
    return i < pl->pg->nposts ? i : -1;
}

void pl_yeah(PostList *pl, int i) {
    JPost *p = &pl->pg->posts[i];
    if (pl->yeah.state == JS_LOADING) return;
    if (!juxt_signed_in()) { app_toast("Sign in to give Yeahs."); sound_play(SFX_ERROR); return; }
    if (p->pid == juxt_my_pid() && juxt_my_pid()) { app_toast("You can't Yeah your own post."); sound_play(SFX_ERROR); return; }
    p->yeahed = !p->yeahed;
    p->yeahs += p->yeahed ? 1 : -1;
    if (p->yeahs < 0) p->yeahs = 0;
    sound_play(p->yeahed ? SFX_CONFIRM : SFX_CANCEL);
    pl->yeah_idx = i;
    juxt_yeah(&pl->yeah, p->id);
}

void pl_update(PostList *pl) {
    JPage *pg = pl->pg;
    /* a Yeah coming back */
    if (pl->yeah.state == JS_READY || pl->yeah.state == JS_FAILED) {
        int i = pl->yeah_idx;
        if (i >= 0 && i < pg->nposts) {
            JPost *p = &pg->posts[i];
            if (pl->yeah.state == JS_READY) {
                /* Juxt answers with the new count (the Yeah itself stays as shown) */
                if (pl->yeah.count >= 0) p->yeahs = pl->yeah.count;
            } else {
                p->yeahed = !p->yeahed;
                p->yeahs += p->yeahed ? 1 : -1;
                app_toast(pl->yeah.msg[0] ? pl->yeah.msg : "That Yeah didn't go through.");
            }
        }
        pl->yeah.state = JS_IDLE;
    }
    if (pg->state != JS_READY) return;
    /* scrolling */
    float view = pl->view_bottom - pl->view_top;
    float maxs = pl->content_h - view + 24;
    if (maxs < 0) maxs = 0;
    pl->target += ptr_scroll(pl->view_top, pl->view_bottom);
    /* a D-pad hop onto a post that's partly hidden brings it into view */
    int o = pl_over(pl);
    if (o >= 0 && o != pl->last_over && (input_pressed(BTN_UP) || input_pressed(BTN_DOWN))) {
        float t = pl->item_top[o], b = pl->item_bot[o];
        if (t - pl->target < 0) pl->target = t - 8;
        else if (b - pl->target > view) pl->target = b - view + 8;
    }
    pl->last_over = o;
    if (pl->target < 0) pl->target = 0;
    if (pl->target > maxs) pl->target = maxs;
    float before = pl->scroll;
    pl->scroll += (pl->target - pl->scroll) * clamp01(g_dt * 12);
    /* keep the hand on the same spot of the page while it scrolls under it */
    (void)before;
    /* more */
    if (pg->next[0] && !pg->more_loading && pl->content_h - pl->scroll < view + 300) juxt_load_more(pg);
    /* clicks */
    int c = ptr_clicked();
    if (c >= pl->id_base && c < pl->id_base + 4 * PL_MAX) {
        int i = (c - pl->id_base) / 4, part = (c - pl->id_base) % 4;
        if (i < pg->nposts) {
            JPost *p = &pg->posts[i];
            if (part == 1) pl_yeah(pl, i);
            else if (part == 2) { sound_play(SFX_SELECT); char a[16]; snprintf(a, sizeof(a), "%d", p->pid); app_go(SCR_USER, a); }
            else { sound_play(SFX_SELECT); app_go(SCR_POST, p->id); }
        }
    }
    if (o >= 0) {
        if (input_pressed(BTN_X)) pl_yeah(pl, o);
        else if (input_pressed(BTN_Z) && pg->posts[o].pid) {
            sound_play(SFX_SELECT);
            char a[16];
            snprintf(a, sizeof(a), "%d", pg->posts[o].pid);
            app_go(SCR_USER, a);
        }
    }
}

void pl_draw(PostList *pl, float x, float y, float w, float bottom, float al) {
    JPage *pg = pl->pg;
    pl->view_top = y;
    pl->view_bottom = bottom;
    if (pg->state == JS_LOADING || pg->state == JS_IDLE) {
        cv_center_note(x, y, w, bottom - y, "Loading", NULL, true, al);
        return;
    }
    if (pg->state == JS_FAILED) {
        cv_center_note(x, y, w, bottom - y, "Juxt couldn't be reached", pg->error[0] ? pg->error : "Press A to try again.", false, al);
        return;
    }
    int n = pl->big_first ? pg->nposts : pg->nposts - pl->first;
    if (n <= 0) {
        if (pl->first == 0) cv_center_note(x, y, w, bottom - y, "Nothing here yet", pl_empty_text, false, al);
        pl->content_h = 0;
        return;
    }
    cv_list_begin(x, y, w, bottom);
    float yy = y - pl->scroll + 6;
    int over = pl_over(pl);
    if (pl->cols <= 1) {
        for (int i = pl->big_first ? 0 : pl->first; i < pg->nposts; i++) {
            JPost *p = &pg->posts[i];
            bool big = pl->big_first && i == 0;
            if (pl->big_first && i == 1) {
                /* the comments under the post */
                if (yy > y - 30 && yy < bottom)
                    ui_text(FONT_MENU, 13, 1.4f, x + 4, yy, 22, rgba(0xc9, 0xd2, 0xee, (u8)(255 * al)), "Comments");
                yy += 28;
            }
            float h = pl_card(p, x, yy, w, false, false, pl->show_community, big, false, al);
            pl->item_top[i] = yy - y + pl->scroll;
            pl->item_bot[i] = pl->item_top[i] + h;
            if (yy + h > y - 20 && yy < bottom + 20) {
                float vy = yy < y ? y : yy, vb = yy + h > bottom ? bottom : yy + h;
                bool hot = false, yhot = false;
                if (vb > vy) {
                    hot = ptr_hot(pl->id_base + i * 4, x, vy, w, vb - vy);
                    float bx, by, bw, bh;
                    pl_card_yeah_box(p, x, yy, w, pl->show_community, big, &bx, &by, &bw, &bh);
                    if (by >= y && by + bh <= bottom) yhot = ptr_hot(pl->id_base + i * 4 + 1, bx, by, bw, bh);
                    float fy = yy + 12;
                    if (fy >= y && fy + 42 <= bottom) ptr_hot(pl->id_base + i * 4 + 2, x + PAD, fy, big ? 44 : FACE, big ? 44 : FACE);
                }
                pl_card(p, x, yy, w, hot || over == i, yhot, pl->show_community, big, true, al);
            }
            yy += h + 12;
        }
    } else {
        int cols = pl->cols;
        float gap = 14, tw = (w - gap * (cols - 1)) / cols;
        int k = 0;
        for (int i = pl->first; i < pg->nposts; i++, k++) {
            float tx = x + (k % cols) * (tw + gap), ty = yy + (k / cols) * (TILE_H + gap);
            pl->item_top[i] = ty - y + pl->scroll;
            pl->item_bot[i] = pl->item_top[i] + TILE_H;
            if (ty + TILE_H > y - 20 && ty < bottom + 20) {
                float vy = ty < y ? y : ty, vb = ty + TILE_H > bottom ? bottom : ty + TILE_H;
                bool hot = vb > vy && ptr_hot(pl->id_base + i * 4, tx, vy, tw, vb - vy);
                tile(&pg->posts[i], tx, ty, tw, hot, al);
            }
        }
        yy += ((k + cols - 1) / cols) * (TILE_H + gap);
    }
    pl->content_h = yy - (y - pl->scroll);
    bool more = pl->content_h - pl->scroll > bottom - y + 4 || pg->next[0];
    if (pg->more_loading && yy < bottom) ui_spinner(x + w / 2, yy + 20, 10, g_time, al);
    cv_list_end(more, al);
}
