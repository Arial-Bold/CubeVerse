/* home.c - the home screen: four glass cubes on a ring, one for each part
 * of CubeVerse (Activity, Communities, Notifications, My page). The stick
 * turns the ring; the cube at the front is bigger, the ones beside it
 * smaller and the one at the back smaller still and dimmer. Each cube drifts
 * to its own rhythm. */
#include "app.h"
#include "home.h"
#include "vcube.h"
#include "cvui.h"
#include "logo2d.h"
#include "input.h"
#include "sound.h"
#include "settings.h"
#include "menumusic.h"
#include "update.h"
#include "juxt.h"
#include "imgcache.h"
#include "m3d.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const NAMES[4] = { "Activity", "Communities", "Notifications", "My page" };
static const char *const HINTS[4] = { "Posts from people and communities you follow", "Find a community and join in",
                                      "Yeahs, comments and new followers", "Your posts, Yeahs and followers" };
static const Color TINTS[4] = { { 86, 194, 47, 255 }, { 47, 166, 224, 255 }, { 245, 134, 42, 255 }, { 125, 91, 214, 255 } };
/* each cube's own drift and icon rhythm */
static const float SWAY_PERIOD[4] = { 8.0f, 9.4f, 7.3f, 8.8f }, SWAY_OFF[4] = { 0, 3.1f, 5.2f, 1.7f };
static const float WOB_PERIOD[4] = { 4.4f, 4.9f, 4.0f, 4.6f }, WOB_OFF[4] = { 0, 1.3f, 2.6f, 0.8f };

#define CUBE_UNIT (124.0f / 180.0f)   /* a 124px cube (the mockup's was 112) */

static float ring;        /* where the ring has turned to (cube at the front, fractional while turning) */
static int target;        /* the cube being turned to */
static float turn_t = -1, turn_from;   /* time into the current turn (-1: not turning) */
#define TURN_S 0.5f
static float exit_armed_until;
static float shown_t;     /* seconds on this screen */
static bool from_splash;

float home_center_y(void) { return 212; }

void home_slot(float theta_deg, float *dx, float *dy, float *scale, float *alpha) {
    float t = theta_deg * (float)M_PI / 180;
    float c = cosf(t), s = sinf(t);
    float front = (1 + c) / 2;   /* 1 at the front, 0 at the back */
    *dx = 182 * s;
    *dy = -37 * (1 - c);
    *scale = (0.52f + 0.78f * powf(front, 1.83f)) * CUBE_UNIT;
    *alpha = 0.5f + 0.5f * powf(front, 0.47f);
}

void home_cube_setup(VCube *v, int i, float theta, float time) {
    float dx, dy, sc, al;
    home_slot(theta, &dx, &dy, &sc, &al);
    vcube_init(v);
    v->cx = gfx_width() / 2 + dx;
    v->cy = home_center_y() + dy;
    v->scale = sc;
    v->alpha = al;
    v->tint = TINTS[i];
    v->icon = i;
    v->icon_alpha = 1;
    v->time = time * 8.0f / SWAY_PERIOD[i] + SWAY_OFF[i];
    v->icon_time = time + i * 1.5f;
    v->wob_time = time * 4.4f / WOB_PERIOD[i] + WOB_OFF[i];
}

static int front_cube(void) { return ((int)lroundf(ring) % 4 + 4) % 4; }

static void home_enter(const char *arg, bool back) {
    (void)back;
    from_splash = arg && !strcmp(arg, "splash");
    ring = target = g_home_section;
    turn_t = -1;
    shown_t = 0;
    /* the music begins just after the opening's sound has faded away (it's
     * all but silent from about 12 s), so the two never run into each other */
    float delay = from_splash ? 0.5f : 0.2f;
    if (g_intro_sound_end >= 0 && g_intro_sound_end + 0.5f - g_time > delay) delay = g_intro_sound_end + 0.5f - g_time;
    menumusic_start(delay);
    if (from_splash && g_set.update_auto && update_state() == UPD_NONE) update_request_check(true);
}

static void open_section(int i) {
    sound_play(SFX_OPEN);
    g_home_section = i;
    if (i != 1 && !juxt_signed_in()) { app_go(SCR_SIGNIN, ""); return; }
    switch (i) {
    case 0: app_go(SCR_FEED, ""); break;
    case 1: app_go(SCR_COMMUNITIES, ""); break;
    case 2: app_go(SCR_NEWS, ""); break;
    default: app_go(SCR_USER, ""); break;
    }
}

static void home_update(void) {
    shown_t += g_dt;
    if (update_panel_active()) { update_panel_update(); return; }
    if (settings_panel_active()) { settings_panel_update(); return; }
    static bool told;
    if (!told && update_state() == UPD_AVAILABLE && update_was_quiet()) {
        told = true;
        char m[96];
        snprintf(m, sizeof(m), "CubeVerse %s is available. Press Z to see what's new.", update_latest_version());
        app_toast(m);
    }
    /* turning. A push starts a turn at once, even in the middle of one: the
     * ring sets off again from wherever it is (the easing starts quickly and
     * lands softly, so chained turns run together smoothly). Holding the
     * stick keeps turning, one cube after another. */
    if (turn_t >= 0) {
        turn_t += g_dt;
        float k = ease_bezier(clamp01(turn_t / TURN_S), 0.25f, 0.6f, 0.3f, 1);
        ring = turn_from + (target - turn_from) * k;
        if (turn_t >= TURN_S) { ring = (float)target; turn_t = -1; }
    }
    {
        static int held_dir = DIR_NONE;
        int sd = input_stick_dir();
        int d = DIR_NONE;
        if ((sd == DIR_LEFT || sd == DIR_RIGHT) && sd != held_dir) d = sd;   /* a new push of the stick */
        else if ((sd == DIR_LEFT || sd == DIR_RIGHT) && sd == held_dir && (turn_t < 0 || turn_t > TURN_S * 0.6f)) d = sd;   /* held */
        held_dir = sd;
        if (d == DIR_NONE && sd == DIR_NONE) {
            int pd = input_dir();   /* the D-pad (with its own repeat) */
            if (pd == DIR_LEFT || pd == DIR_RIGHT) d = pd;
        }
        if (d == DIR_RIGHT || d == DIR_LEFT) {
            turn_from = ring;
            target += d == DIR_RIGHT ? 1 : -1;
            if (target - turn_from > 1.6f) target = (int)lroundf(turn_from) + 1;    /* never more than a cube and a bit ahead */
            if (turn_from - target > 1.6f) target = (int)lroundf(turn_from) - 1;
            turn_t = 0;
            sound_play(SFX_TURN_SIDE);
            return;
        }
    }
    /* A opens the cube being turned to, even before it has quite arrived */
    if (turn_t >= 0) {
        if (input_pressed(BTN_A)) { ring = (float)target; turn_t = -1; open_section(front_cube()); }
        return;
    }
    if (input_pressed(BTN_A)) open_section(front_cube());
    else if (input_pressed(BTN_START)) { sound_play(SFX_SELECT); settings_panel_open(); }
    else if (input_pressed(BTN_Z) && (update_state() == UPD_AVAILABLE || update_state() == UPD_INSTALLED ||
                                      update_state() == UPD_INSTALLING)) {
        sound_play(SFX_SELECT);
        update_panel_open(false);
    } else if (input_pressed(BTN_B)) {
        if (g_time < exit_armed_until) { exit_armed_until = 0; app_exit(); }
        else {
            exit_armed_until = g_time + 3;
            sound_play(SFX_BACK);
            char msg[96];
            const char *l = app_exit_label();
            if (g_set.exit_action == EXIT_RESTART) snprintf(msg, sizeof(msg), "Press B again to restart the console");
            else snprintf(msg, sizeof(msg), "Press B again to %c%s", l[0] | 0x20, l + 1);
            app_toast(msg);
        }
    }
}

void home_draw_header(float al) {
    if (al <= 0) return;
    logo_mark_draw(26, 22, 34, al);
    ui_text(FONT_CUBE, 21, 0.2f, 26 + logo_mark_width(34) + 8, 20, 38, rgba(0xEE, 0xF0, 0xFF, (u8)(255 * al)), "CubeVerse");
    float rx = gfx_width() - 22;
    if (juxt_signed_in()) {
        const char *nm = juxt_my_name();
        float tw = gfx_text_width(FONT_ROUND, 17, nm, -1);
        ui_text(FONT_ROUND, 17, 0, rx - tw, 22, 36, rgba(0xEE, 0xF0, 0xFF, (u8)(255 * al)), nm);
        cv_face(juxt_my_mii(), nm, rx - tw - 12 - 36, 22, 36, al);
    } else {
        ui_text_right(FONT_ROUND, 13, 0, rx, 22, 36, rgba(0xB9, 0xBE, 0xD6, (u8)(255 * al)), "Not signed in");
    }
}

void home_draw_labels(float ring_pos, float al) {
    /* the front cube's name and what it's for, crossfading as the ring turns */
    int a = (int)floorf(ring_pos), b = a + 1;
    float f = ring_pos - a;
    for (int k = 0; k < 2; k++) {
        int idx = ((k ? b : a) % 4 + 4) % 4;
        float w = k ? clamp01((f - 0.55f) / 0.45f) : clamp01(1 - f / 0.45f);
        if (w <= 0) continue;
        u8 A = (u8)(255 * w * al);
        ui_text_glow(FONT_CUBE, 20, 0, gfx_width() / 2 - gfx_text_width(FONT_CUBE, 20, NAMES[idx], -1) / 2, 352, 24,
                     rgba(255, 255, 255, A), rgba(175, 195, 255, A), 4, NAMES[idx]);
        char hint[120];
        if (idx == 2 && g_unread > 0) snprintf(hint, sizeof(hint), "%s \x7f %d new", HINTS[idx], g_unread);
        else snprintf(hint, sizeof(hint), "%s", HINTS[idx]);
        ui_text_center(FONT_ROUND, 12, 0, gfx_width() / 2, 378, 16, rgba(0xB9, 0xBE, 0xD6, A), hint);
    }
}

void home_draw_cubes(float ring_pos, float time, float al, int skip_mask) {
    /* back to front */
    int order[4];
    float depth[4];
    for (int i = 0; i < 4; i++) {
        order[i] = i;
        depth[i] = cosf((i - ring_pos) * 90 * (float)M_PI / 180);
    }
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++)
            if (depth[order[j]] < depth[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int n = 0; n < 4; n++) {
        int i = order[n];
        if (skip_mask & (1 << i)) continue;
        VCube v;
        home_cube_setup(&v, i, (i - ring_pos) * 90, time);
        v.alpha *= al;
        vcube_draw(&v);
    }
}

void home_draw_hints(float al) {
    int d = input_stick_dir();
    if (d != DIR_LEFT && d != DIR_RIGHT) {
        float c = fmodf(g_time, 3.0f);
        d = c < 0.4f ? -1 : c < 1.1f ? DIR_RIGHT : c < 1.6f ? -1 : c < 2.3f ? DIR_LEFT : -1;
    }
    char exit_hint[72];
    snprintf(exit_hint, sizeof(exit_hint), "B:%s", app_exit_label());
    UpdateState us = update_state();
    bool upd = us == UPD_AVAILABLE || us == UPD_INSTALLING || us == UPD_INSTALLED;
    const char *h[] = { "S:Turn", "A:Open", "ST:Settings", exit_hint, us == UPD_INSTALLED ? "Z:Restart" : "Z:Update" };
    ui_hints(h, upd ? 5 : 4, al, d);
}

static void home_draw(float open) {
    (void)open;
    float t = g_time;
    cv_background(1);
    home_draw_cubes(ring, t, 1, 0);
    home_draw_header(1);
    home_draw_labels(ring, 1);
    float hints = settings_panel_active() || update_panel_active() ? 0 : 1;
    home_draw_hints(hints);
    UpdateState us = update_state();
    if ((us == UPD_AVAILABLE || us == UPD_INSTALLING || us == UPD_INSTALLED) && hints > 0) {
        char tag[64];
        snprintf(tag, sizeof(tag), us == UPD_INSTALLED ? "\x84 CubeVerse %s is installed" : us == UPD_INSTALLING ? "\x84 Installing CubeVerse %s" : "\x84 CubeVerse %s is available",
                 update_latest_version());
        float tw = gfx_text_width_sp(FONT_MENU_BOLD, 13, 1.2f, tag, -1) + 28;
        float tx = gfx_width() / 2 - tw / 2, ty = 66;
        gfx_rect(tx, ty, tw, 24, rgba(40, 32, 6, 200));
        gfx_rrect_outline(tx, ty, tw, 24, 0, 1.5f, rgba(0xe8, 0xcf, 0x4a, 220));
        ui_text_center(FONT_MENU_BOLD, 13, 1.2f, tx + tw / 2, ty, 24, rgba(0xf6, 0xe0, 0x7a, 255), tag);
    }
    if (settings_panel_active()) settings_panel_draw();
    if (update_panel_active()) update_panel_draw();
}

const Screen screen_home = { home_enter, home_update, home_draw, NULL };
