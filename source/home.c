/* home.c - the home screen: four glass cubes on a ring, one for each part
 * of CubeVerse (Activity, Communities, Notifications, My page). The stick
 * turns the ring; the cube at the front is bigger, the ones beside it
 * smaller and the one at the back smaller still and dimmer. Each cube drifts
 * to its own rhythm.
 *
 * Three little secrets: spin the ring round and round (20 turns the same
 * way, quickly) and the cubes get dizzy; leave the controller alone for five
 * minutes and they doze off, with blocky Z's floating up, and wake with a
 * yawn; poke the logo with Y ten times and its three cubes tumble out and
 * bounce away. */
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
#include <stdlib.h>
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

/* ------------------------------------------------------------ easter eggs */

#define DIZZY_TURNS 20      /* turns the same way, each within DIZZY_GAP of the last */
#define DIZZY_GAP 1.0f
#define DIZZY_S 4.5f
#define SLEEP_AFTER 300.0f  /* seconds without a touch */
#define POKES 10            /* Y presses on the logo, each within POKE_GAP of the last */
#define POKE_GAP 2.5f

static int spin_dir, spin_count;
static float spin_last = -10, dizzy_t;          /* dizzy_t: seconds of dizziness left */
static float idle_t, sleepy;                     /* sleepy: 0 awake .. 1 fast asleep */
static float zzz_next;
typedef struct { float x, y, t, life; bool on; } Zz;
static Zz zz[6];
static int pokes;
static float poke_last = -10, wiggle_t = -1;
typedef struct { float x, y, vx, vy, ang, spin, size; Color c; bool on; } Tumbler;
static Tumbler tumblers[3];
static float logo_gone_t = -1;                   /* the logo's cubes are out: seconds since */

static void egg_turned(int dir) {
    if (dir == spin_dir && g_time - spin_last < DIZZY_GAP) spin_count++;
    else spin_count = 1;
    spin_dir = dir;
    spin_last = g_time;
    if (spin_count >= DIZZY_TURNS && dizzy_t <= 0) {
        dizzy_t = DIZZY_S;
        spin_count = 0;
        sound_play(SFX_DIZZY);
    }
}

static float dizzy_amount(void) {
    if (dizzy_t <= 0) return 0;
    float in = clamp01((DIZZY_S - dizzy_t) / 0.4f), out = clamp01(dizzy_t / 1.5f);
    return in * out;
}

static void poke_logo(void) {
    if (logo_gone_t >= 0) return;
    if (g_time - poke_last < POKE_GAP) pokes++;
    else pokes = 1;
    poke_last = g_time;
    wiggle_t = 0;
    sound_play(SFX_POKE);
    if (pokes >= POKES) {
        pokes = 0;
        logo_gone_t = 0;
        sound_play(SFX_WHEE);
        /* the three cubes pop out of the mark and tumble off to the right */
        static const Color C[3] = { { 0xF2, 0xF5, 0xFF, 255 }, { 0x9C, 0xE0, 0x7E, 255 }, { 0x8E, 0xD1, 0xFF, 255 } };
        for (int i = 0; i < 3; i++) {
            Tumbler *b = &tumblers[i];
            b->on = true;
            b->x = 30 + i * 14;
            b->y = 34;
            b->vx = 110 + i * 55;
            b->vy = -260 - i * 40;
            b->ang = 0;
            b->spin = 4 + i * 2.5f;
            b->size = i == 1 ? 18 : 14;
            b->c = C[i];
        }
    }
}

static void eggs_update(bool touched) {
    if (dizzy_t > 0) dizzy_t -= g_dt;
    /* sleep */
    if (touched) {
        if (sleepy > 0.5f) sound_play(SFX_YAWN);
        idle_t = 0;
    } else {
        idle_t += g_dt;
    }
    float after = SLEEP_AFTER;
#ifdef PC_TEST
    if (getenv("CV_SLEEP_AFTER")) after = (float)atof(getenv("CV_SLEEP_AFTER"));   /* (screenshots) */
#endif
    float want = idle_t > after ? 1 : 0;
    sleepy += (want - sleepy) * clamp01(g_dt * (want > sleepy ? 0.6f : 4.0f));
    if (sleepy > 0.6f && g_time > zzz_next) {
        zzz_next = g_time + 1.5f;
        for (int i = 0; i < 6; i++)
            if (!zz[i].on) { zz[i] = (Zz){ gfx_width() / 2 + 34, home_center_y() - 70, 0, 4.0f, true }; break; }
    }
    for (int i = 0; i < 6; i++)
        if (zz[i].on && (zz[i].t += g_dt) > zz[i].life) zz[i].on = false;
    /* the logo's cubes */
    if (wiggle_t >= 0 && (wiggle_t += g_dt) > 0.35f) wiggle_t = -1;
    if (logo_gone_t >= 0) {
        logo_gone_t += g_dt;
        float floor_y = 50, W = gfx_width();
        bool any = false;
        for (int i = 0; i < 3; i++) {
            Tumbler *b = &tumblers[i];
            if (!b->on) continue;
            b->vy += 900 * g_dt;
            b->x += b->vx * g_dt;
            b->y += b->vy * g_dt;
            b->ang += b->spin * g_dt;
            if (b->y > floor_y - b->size / 2 && b->vy > 0) {
                b->y = floor_y - b->size / 2;
                if (b->vy > 120) sound_play(SFX_BOING);
                b->vy *= -0.62f;
                b->spin *= 0.8f;
            }
            if (b->x > W + 40) b->on = false;
            else any = true;
        }
        if (!any && logo_gone_t > 6) logo_gone_t = -1;   /* the logo has grown back */
    }
}

/* a small smiling cube, turned by ang */
static void draw_tumbler(const Tumbler *b, float al) {
    float c = cosf(b->ang), sn = sinf(b->ang), h = b->size / 2;
    float q[8];
    static const float K[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int k = 0; k < 4; k++) {
        float x = K[k][0] * h, y = K[k][1] * h;
        q[k * 2] = b->x + x * c - y * sn;
        q[k * 2 + 1] = b->y + x * sn + y * c;
    }
    Color top = color_alpha(b->c, al), bot = rgba((u8)(b->c.r * 0.75f), (u8)(b->c.g * 0.75f), (u8)(b->c.b * 0.75f), (u8)(255 * al));
    Color c4[4] = { top, top, bot, bot };
    gfx_quad_colors(q, c4);
    /* eyes and a smile */
    Color ink = rgba(0x2A, 0x2F, 0x45, (u8)(255 * al));
    float pts[3][2] = { { -0.4f, -0.25f }, { 0.4f, -0.25f }, { 0, 0.35f } };
    for (int k = 0; k < 3; k++) {
        float x = pts[k][0] * h, y = pts[k][1] * h;
        float px = b->x + x * c - y * sn, py = b->y + x * sn + y * c;
        float r = k < 2 ? b->size * 0.09f : b->size * 0.12f;
        gfx_rect(px - r, py - r, r * 2, r * 2, ink);
    }
}

/* a blocky "Z", made of small squares */
static void draw_z(float x, float y, float cell, Color c) {
    /* ###
     *  #
     * #
     * ### */
    for (int i = 0; i < 3; i++) gfx_rect(x + i * cell, y, cell - 1, cell - 1, c);
    gfx_rect(x + 1 * cell, y + cell, cell - 1, cell - 1, c);
    gfx_rect(x + 0 * cell, y + 2 * cell, cell - 1, cell - 1, c);
    for (int i = 0; i < 3; i++) gfx_rect(x + i * cell, y + 3 * cell, cell - 1, cell - 1, c);
}

static void eggs_draw(float al) {
    /* Z's from the front cube */
    for (int i = 0; i < 6; i++) {
        if (!zz[i].on) continue;
        float k = zz[i].t / zz[i].life;
        float a = clamp01(k / 0.15f) * clamp01((1 - k) / 0.35f) * sleepy * al;
        float x = zz[i].x + k * 40 + sinf(zz[i].t * 2.2f) * 8, y = zz[i].y - k * 110;
        float cell = 6 + k * 4;
        draw_z(x, y, cell, rgba(0xDD, 0xE6, 0xFF, (u8)(235 * a)));
    }
    /* dizzy: little stars circling over the front cube */
    float d = dizzy_amount();
    if (d > 0) {
        float cx = gfx_width() / 2, cy = home_center_y() - 104;
        for (int i = 0; i < 5; i++) {
            float a = g_time * 4.2f + i * 6.2832f / 5;
            float x = cx + cosf(a) * 60, y = cy + sinf(a) * 14;
            float s = 4 + 2 * (0.5f + 0.5f * sinf(a));
            Color c = rgba(0xFF, 0xE0, 0x6A, (u8)(255 * d * al));
            gfx_rect(x - s / 2, y - s * 1.5f, s, s * 3, c);
            gfx_rect(x - s * 1.5f, y - s / 2, s * 3, s, c);
        }
    }
    for (int i = 0; i < 3; i++) if (tumblers[i].on) draw_tumbler(&tumblers[i], al);
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
    {
        bool touched = input_any() || fabsf(input_stick_x()) > 0.3f || fabsf(input_stick_y()) > 0.3f ||
                       fabsf(input_cstick_x()) > 0.3f || fabsf(input_cstick_y()) > 0.3f;
        bool was_asleep = sleepy > 0.3f;
        eggs_update(touched);
        if (was_asleep && touched) { idle_t = 0; return; }   /* the first touch only wakes them */
    }
    if (input_pressed(BTN_Y)) poke_logo();
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
            egg_turned(d);
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
    float lw = wiggle_t >= 0 ? sinf(wiggle_t / 0.35f * 3.1416f) * 4 : 0;
    float la = logo_gone_t < 0 ? 1 : clamp01((logo_gone_t - 4.5f) / 1.5f);   /* grows back after a while */
    if (la > 0) logo_mark_draw(26, 22 - lw, 34, al * la);
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
        float dz = dizzy_amount();
        if (dz > 0 || sleepy > 0.001f) {
            /* dizzy: wobbling and swaying off balance; asleep: sunk a little and dim */
            v.sway = (1 - sleepy) * (1 - dz);
            v.ax = VC_BASE_X + dz * 14 * sinf(g_time * 7.0f + i) + sleepy * 8;
            v.ay = VC_BASE_Y + dz * 18 * sinf(g_time * 5.3f + i * 1.7f);
            v.az = VC_BASE_Z + dz * 16 * sinf(g_time * 6.1f + i * 2.3f);
            v.cx += dz * 12 * sinf(g_time * 3.1f + i);
            v.cy += sleepy * 26 + dz * 6 * sinf(g_time * 4.4f + i);
            v.alpha *= 1 - 0.45f * sleepy;
            v.icon_alpha *= 1 - 0.6f * sleepy;
        }
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
    eggs_draw(1);
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
