/* main.c - CubeVerse: Juxtaposition (Pretendo Network's Miiverse) on the
 * Nintendo GameCube.
 *
 * Start-up, the main loop, and moving between screens. */
#include "app.h"
#include "gfx.h"
#include "ui.h"
#include "cvui.h"
#include "input.h"
#include "pointer.h"
#include "settings.h"
#include "storage.h"
#include "sound.h"
#include "menumusic.h"
#include "update.h"
#include "applog.h"
#include "net.h"
#include "juxt.h"
#include "imgcache.h"
#include "dolload.h"
#include "thr.h"
#include "m3d.h"
#include <strings.h>
#include <stdio.h>
#include <string.h>
#ifdef GEKKO
#include <ogc/lwp_watchdog.h>
#endif

int g_home_section;
int g_unread = -1;
bool g_posted;

static const Screen *screens[SCR_COUNT];
static ScreenId cur = SCR_SPLASH, next_scr;
static char next_arg[96];
static bool next_back;
static float fade = 0;
static int fading;
static float opened;
static char toast[160];
static float toast_t = -1;

/* where we've been, for B */
#define HIST 16
static struct { ScreenId s; char arg[96]; } hist[HIST];
static int nhist;
static char cur_arg[96];

ScreenId app_screen(void) { return cur; }
const Screen *app_screen_def(ScreenId s) { return screens[s]; }

static void switch_now(ScreenId s, const char *arg, bool back) {
    if (screens[cur]->leave) screens[cur]->leave();
    cur = s;
    snprintf(cur_arg, sizeof(cur_arg), "%s", arg ? arg : "");
    opened = 0;
    screens[cur]->enter(cur_arg, back);
}

static void start_fade(ScreenId s, const char *arg, bool back) {
    if (fading) return;
    next_scr = s;
    snprintf(next_arg, sizeof(next_arg), "%s", arg ? arg : "");
    next_back = back;
    fading = 1;
    fade = 0;
}

void app_go(ScreenId s, const char *arg) {
    if (fading) return;
    if (cur != SCR_SPLASH && cur != SCR_HOME) {
        if (nhist == HIST) { memmove(hist, hist + 1, sizeof(hist[0]) * (HIST - 1)); nhist--; }
        hist[nhist].s = cur;
        snprintf(hist[nhist].arg, sizeof(hist[nhist].arg), "%s", cur_arg);
        nhist++;
    } else {
        nhist = 0;
    }
    start_fade(s, arg, false);
}

void app_swap(ScreenId s, const char *arg) { start_fade(s, arg, false); }

void app_replace(ScreenId s, const char *arg) {
    nhist = 0;
    switch_now(s, arg, false);
}

void app_back(void) {
    if (fading) return;
    if (nhist > 0) {
        nhist--;
        start_fade(hist[nhist].s, hist[nhist].arg, true);
    } else {
        start_fade(SCR_HOME, "", true);
    }
}

void app_home(int section) {
    g_home_section = section;
    nhist = 0;
    start_fade(SCR_HOME, "", true);
}

void app_toast(const char *msg) {
    snprintf(toast, sizeof(toast), "%s", msg);
    toast_t = 0;
}

static void draw_toast(void) {
    if (toast_t < 0) return;
    float a = clamp01(toast_t / 0.2f) * clamp01((3.6f - toast_t) / 0.4f);
    if (a <= 0) return;
    float w = gfx_text_width_sp(FONT_MENU, 14, 1.2f, toast, -1) + 40;
    if (w > gfx_width() - 40) w = gfx_width() - 40;
    float x = gfx_width() / 2 - w / 2, y = 22;   /* clear of the top edge (TVs cut a little off) */
    ui_desc(x, y, w, 34, a);
    ui_text_fit_scroll(FONT_MENU, 14, 1.2f, x + 20, y, w - 40, 34, rgba(0xec, 0xeb, 0xff, (u8)(255 * a)), toast);
}

const char *app_exit_label(void) {
    static char buf[64];
    if (g_set.exit_action == EXIT_RESTART) return "Restart";
    const char *name = strrchr(g_set.swiss_dol, '/');
    name = name ? name + 1 : g_set.swiss_dol;
    if (!strncasecmp(name, "swiss", 5) || !strcasecmp(name, "boot.dol") || !strcasecmp(name, "ipl.dol") || !name[0])
        return "Exit to Launcher";
    snprintf(buf, sizeof(buf), "Exit to %.40s", name);
    char *dot = strrchr(buf, '.');
    if (dot && !strcasecmp(dot, ".dol")) *dot = 0;
    return buf;
}

bool app_exit(void) {
    char err[256];
    settings_save();
    if (g_set.exit_action == EXIT_RESTART) {
        sound_shutdown();
        system_restart();
        return false;
    }
    if (!dol_check(g_set.swiss_dol, err, sizeof(err))) {
        const char *pick = NULL;
        static char found[48][256];
        if (dol_check("sd:/boot.dol", err, sizeof(err))) pick = "sd:/boot.dol";
        int n = !pick && storage_ok() ? dol_find("sd:/", found, 48) : 0;
        for (int i = 0; i < n && !pick; i++) {
            const char *name = strrchr(found[i], '/');
            name = name ? name + 1 : found[i];
            if (!strncasecmp(name, "swiss", 5) && dol_check(found[i], err, sizeof(err))) pick = found[i];
        }
        if (!pick) {
            app_toast("No program to start was found. Choose one in Settings.");
            sound_play(SFX_ERROR);
            return false;
        }
        snprintf(g_set.swiss_dol, sizeof(g_set.swiss_dol), "%s", pick);
        settings_save();
    }
    if (!dol_launch(g_set.swiss_dol, err, sizeof(err))) {
        app_toast(err);
        sound_play(SFX_ERROR);
        return false;
    }
    return true;
}

#ifdef GEKKO
static u64 last_tick;
static float frame_seconds(void) {
    u64 now = gettime();
    float dt = last_tick ? ticks_to_microsecs(now - last_tick) / 1e6f : 1 / 60.0f;
    last_tick = now;
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.001f) dt = 1 / 60.0f;
    return dt;
}
#else
static float frame_seconds(void) { return 1 / 60.0f; }
#endif

/* the unread count, asked for every minute or so while signed in */
static JResult unread_res;
static float unread_next = 6;

static void unread_tick(void) {
    if (unread_res.state == JS_READY) { g_unread = unread_res.count; unread_res.state = JS_IDLE; }
    else if (unread_res.state == JS_FAILED) unread_res.state = JS_IDLE;
    if (g_time >= unread_next && juxt_signed_in() && net_state() == NET_UP && unread_res.state == JS_IDLE) {
        unread_next = g_time + 60;
        juxt_unread(&unread_res);
    }
}

void app_init(void) {
    bool sd = storage_init();
    settings_load();
    gfx_init(g_set.video_mode, g_set.widescreen != 0, g_set.full_width != 0);
    gfx_set_margin(g_set.margin / 100.0f);
    input_init();
    sound_init();
    menumusic_init();
    ptr_init();
    if (!sd) app_log("no SD card");
    net_start();
    juxt_init();
    img_init();

    screens[SCR_SPLASH] = &screen_splash;
    screens[SCR_HOME] = &screen_home;
    screens[SCR_SIGNIN] = &screen_signin;
    screens[SCR_FEED] = &screen_feed;
    screens[SCR_COMMUNITIES] = &screen_communities;
    screens[SCR_COMMUNITY] = &screen_community;
    screens[SCR_POST] = &screen_post;
    screens[SCR_COMPOSE] = &screen_compose;
    screens[SCR_DRAW] = &screen_draw;
    screens[SCR_USER] = &screen_user;
    screens[SCR_NEWS] = &screen_news;
    cur = SCR_SPLASH;
    screens[cur]->enter("", false);
}

void app_frame(void) {
    g_dt = frame_seconds();
    g_time += g_dt;
    opened += g_dt;
    if (toast_t >= 0) { toast_t += g_dt; if (toast_t > 4) toast_t = -1; }
    input_update();
    juxt_poll();
    img_tick();
    unread_tick();
    menumusic_update();
    app_log_flush();
    update_poll();

    if (fading == 1) {
        fade += g_dt / 0.14f;
        if (fade >= 1) {
            fade = 1;
            switch_now(next_scr, next_arg, next_back);
            fading = 2;
        }
    } else if (fading == 2) {
        fade -= g_dt / 0.18f;
        if (fade <= 0) { fade = 0; fading = 0; }
    } else {
        ptr_update();
        screens[cur]->update();
    }

    gfx_begin_frame();
    ui_frame_begin();
    ui_text_scroll_tick();
    ptr_frame_begin();
    screens[cur]->draw(opened);
    if (cur != SCR_SPLASH && cur != SCR_HOME && !settings_panel_active() && !update_panel_active()) ptr_draw(1);
    draw_toast();
    if (fade > 0) gfx_fill_screen(rgba(0, 0, 0, (u8)(255 * clamp01(fade))));
    gfx_end_frame();
}

#ifndef PC_TEST
int main(int argc, char **argv) {
    storage_set_program(argc > 0 ? argv[0] : NULL);
    app_init();
    update_set_program(storage_program_file());      /* the .dol an update replaces */
    if (storage_ok()) update_remove_old();           /* this version started: the one it replaced can go */
    for (;;) app_frame();
    return 0;
}
#endif
