/* settings_panel.c - START on the home cubes: screen, sound, account and exit
 * settings. The list scrolls; the bar at the bottom says what the chosen
 * setting does. */
#include "app.h"
#include "settings.h"
#include "juxt.h"
#include "gfx.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "storage.h"
#include "dolload.h"
#include "update.h"
#include "version.h"
#include "m3d.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#ifdef GEKKO
#include <gccore.h>
#endif

enum { S_SCREEN, S_WIDTH, S_MARGIN, S_VIDEO, S_INTRO, S_POINTER, S_SOUNDS, S_VOLUME, S_MENU_MUSIC, S_MUSIC, S_ACCOUNT,
       S_UPDATE, S_UPDATE_AUTO, S_EXIT_ACTION, S_EXIT_DOL, S_EXIT, SN };
static const char *const labels[SN] = { "Screen", "Picture width", "Overscan", "Resolution", "Opening animation", "Pointer speed",
                                        "Menu sounds", "Sound volume", "Menu music", "Music volume", "Pretendo account",
                                        "Software update", "Check for updates at start", "Exit button",
                                        "Program to start", "Exit now" };
static const char *const help[SN] = {
    "4:3 for most TVs, 16:9 for widescreen TVs set to stretch the picture.",
    "Full uses the whole width of the TV picture (704 pixels). Standard is 640.",
    "Moves everything in from the edges if your TV cuts them off (overscan). The gold corners show where the picture ends.",
    "480p needs component or digital video cables. Auto picks it when they're connected.",
    "Turn off to go straight to the home cubes when CubeVerse starts.",
    "How fast the pointer moves with the stick. Lower is slower and easier to place.",
    "The little sounds when you move around the menus.",
    "How loud the menu sounds are.",
    "CubeVerse's own mellow background music.",
    "How loud the background music is.",
    "The Pretendo Network ID CubeVerse uses for Juxt. Press A twice to sign out (or A to sign in).",
    "Press A to see if there's a new version of CubeVerse on GitHub, and what's new in it.",
    "A quick check for a new version when CubeVerse starts (it uses the network adapter, if there is one).",
    "What B on the home cubes (and Exit now) does: start another program, or restart the console.",
    "The program Exit starts. boot.dol is usually your launcher. Left and right choose from the .dol files on the card.",
    "Press A twice to leave CubeVerse now.",
};

static bool active;
static int srow, top;
static float t;
static int exit_confirm;

/* the .dol files on the card, for "Program to start" */
#define MAX_DOLS 48
static char dols[MAX_DOLS][256];
static int ndols;
static bool dols_scanned;

static void scan_dols(void) {
    dols_scanned = true;
    ndols = storage_ok() ? dol_find("sd:/", dols, MAX_DOLS) : 0;
    /* keep the current choice in the list even if it isn't on the card */
    bool have = false;
    for (int i = 0; i < ndols; i++) if (!strcasecmp(dols[i], g_set.swiss_dol)) have = true;
    if (!have && ndols < MAX_DOLS && g_set.swiss_dol[0]) {
        memmove(dols[1], dols[0], sizeof(dols[0]) * (size_t)ndols);
        snprintf(dols[0], sizeof(dols[0]), "%s", g_set.swiss_dol);
        ndols++;
    }
}

bool settings_panel_active(void) { return active; }
void settings_panel_open(void) {
    active = true;
    t = 0;
    srow = 0;
    top = 0;
    exit_confirm = 0;
    dols_scanned = false;
}

static void clampi(int *v, int lo, int hi) { if (*v < lo) *v = lo; if (*v > hi) *v = hi; }

static void change(int d) {
    switch (srow) {
    case S_SCREEN: g_set.widescreen = !g_set.widescreen; gfx_set_widescreen(g_set.widescreen != 0); break;
    case S_WIDTH: g_set.full_width = !g_set.full_width; gfx_apply_video_mode(g_set.video_mode, g_set.full_width != 0); break;
    case S_MARGIN: g_set.margin += d; clampi(&g_set.margin, 0, 12); gfx_set_margin(g_set.margin / 100.0f); break;
    case S_VIDEO:
        g_set.video_mode = (g_set.video_mode + d + 3) % 3;
        if (!gfx_apply_video_mode(g_set.video_mode, g_set.full_width != 0)) {
            app_toast("480p needs component (or digital) video cables.");
        }
        break;
    case S_INTRO: g_set.intro = !g_set.intro; break;
    case S_SOUNDS: g_set.sounds = !g_set.sounds; break;
    case S_POINTER: g_set.pointer_speed += d; clampi(&g_set.pointer_speed, 1, 10); break;
    case S_VOLUME: g_set.volume += d; clampi(&g_set.volume, 0, 10); break;
    case S_MENU_MUSIC: g_set.menu_music = !g_set.menu_music; break;
    case S_MUSIC: g_set.music_volume += d; clampi(&g_set.music_volume, 0, 10); break;
    case S_UPDATE_AUTO: g_set.update_auto = !g_set.update_auto; break;
    case S_EXIT_ACTION: g_set.exit_action = !g_set.exit_action; break;
    case S_EXIT_DOL: {
        if (!dols_scanned) scan_dols();
        if (ndols == 0) { sound_play(SFX_ERROR); app_toast("No .dol files were found on the SD card."); return; }
        int cur = -1;
        for (int i = 0; i < ndols; i++) if (!strcasecmp(dols[i], g_set.swiss_dol)) cur = i;
        cur = cur < 0 ? 0 : (cur + d + ndols) % ndols;
        snprintf(g_set.swiss_dol, sizeof(g_set.swiss_dol), "%s", dols[cur]);
        break;
    }
    default: return;
    }
    sound_play(SFX_HOVER);
}

#define VISIBLE 10   /* rows shown at once */

void settings_panel_update(void) {
    t += g_dt;
    int d = input_dir();
    int old = srow;
    if (d == DIR_UP && srow > 0) srow--;
    else if (d == DIR_DOWN && srow < SN - 1) srow++;
    else if (d == DIR_LEFT) change(-1);
    else if (d == DIR_RIGHT) change(1);
    if (srow != old) { exit_confirm = 0; sound_play(SFX_HOVER); }
    if (srow < top) top = srow;
    if (srow >= top + VISIBLE) top = srow - VISIBLE + 1;
    if (input_pressed(BTN_A)) {
        if (srow == S_ACCOUNT) {
            if (!juxt_signed_in()) {
                sound_play(SFX_SELECT);
                active = false;
                app_go(SCR_SIGNIN, "");
            } else if (++exit_confirm >= 2) {
                exit_confirm = 0;
                juxt_sign_out();
                g_unread = -1;
                sound_play(SFX_CONFIRM);
                app_toast("Signed out. Your session was removed from the SD card.");
            } else sound_play(SFX_SELECT);
        } else if (srow == S_UPDATE) {
            sound_play(SFX_SELECT);
            update_panel_open(true);
        } else if (srow == S_EXIT) {
            if (++exit_confirm >= 2) { exit_confirm = 0; app_exit(); }
            else sound_play(SFX_SELECT);
        } else {
            change(1);
        }
    }
    if (input_pressed(BTN_B) || input_pressed(BTN_START)) {
        sound_play(SFX_CANCEL);
        settings_save();
        active = false;
    }
}

static void value_text(int i, char *out, size_t cap) {
    const char *dol = strrchr(g_set.swiss_dol, '/');
    switch (i) {
    case S_SCREEN: snprintf(out, cap, "%s", g_set.widescreen ? "16:9" : "4:3"); break;
    case S_WIDTH: snprintf(out, cap, "%s", g_set.full_width ? "Full (704)" : "Standard (640)"); break;
    case S_MARGIN: snprintf(out, cap, "%d%%", g_set.margin); break;
    case S_VIDEO: snprintf(out, cap, "%s", g_set.video_mode == 0 ? "Auto" : g_set.video_mode == 1 ? "480i" : "480p"); break;
    case S_INTRO: snprintf(out, cap, "%s", g_set.intro ? "On" : "Off"); break;
    case S_SOUNDS: snprintf(out, cap, "%s", g_set.sounds ? "On" : "Off"); break;
    case S_POINTER: snprintf(out, cap, "%d", g_set.pointer_speed); break;
    case S_VOLUME: snprintf(out, cap, "%d", g_set.volume); break;
    case S_MENU_MUSIC: snprintf(out, cap, "%s", g_set.menu_music ? "On" : "Off"); break;
    case S_MUSIC: snprintf(out, cap, "%d", g_set.music_volume); break;
    case S_ACCOUNT:
        if (!juxt_signed_in()) snprintf(out, cap, "Sign in");
        else if (exit_confirm && srow == S_ACCOUNT) snprintf(out, cap, "Press A again to sign out");
        else snprintf(out, cap, "%.40s", juxt_my_name());
        break;
    case S_UPDATE: {
        UpdateState us = update_state();
        if (us == UPD_AVAILABLE) snprintf(out, cap, "\x84 %s available", update_latest_version());
        else if (us == UPD_INSTALLED) snprintf(out, cap, "Restart to finish");
        else if (us == UPD_INSTALLING) snprintf(out, cap, "Installing...");
        else if (us == UPD_CHECKING) snprintf(out, cap, "Checking...");
        else if (us == UPD_LATEST) snprintf(out, cap, "Up to date");
        else snprintf(out, cap, "Check now");
        break;
    }
    case S_UPDATE_AUTO: snprintf(out, cap, "%s", g_set.update_auto ? "On" : "Off"); break;
    case S_EXIT_ACTION: snprintf(out, cap, "%s", g_set.exit_action == EXIT_RESTART ? "Restart console" : "Start a program"); break;
    case S_EXIT_DOL: snprintf(out, cap, "%s", dol ? dol + 1 : g_set.swiss_dol); break;
    case S_EXIT: snprintf(out, cap, "%s", exit_confirm ? "Press A again" : app_exit_label()); break;
    }
}

void settings_panel_draw(void) {
    float a = ease_out(clamp01(t / 0.2f));
    float fx, fy, fw, fh;
    gfx_full_area(&fx, &fy, &fw, &fh);
    gfx_rect(fx, fy, fw, fh, rgba(0, 0, 0, (u8)(150 * a)));
    if (srow == S_MARGIN) {
        /* show where the picture's edges are: they should all be visible */
        Color g = rgba(0xe8, 0xcf, 0x4a, (u8)(255 * a));
        float L = 40, W = gfx_width();
        gfx_rect(0, 0, L, 3, g); gfx_rect(0, 0, 3, L, g);
        gfx_rect(W - L, 0, L, 3, g); gfx_rect(W - 3, 0, 3, L, g);
        gfx_rect(0, 477, L, 3, g); gfx_rect(0, 480 - L, 3, L, g);
        gfx_rect(W - L, 477, L, 3, g); gfx_rect(W - 3, 480 - L, 3, L, g);
    }
    float X = UI_X, pw = 480, px = X + UI_W / 2 - pw / 2, py = 22, ph = 392;
    ui_panel(px, py, pw, ph, a);
    ui_ptitle(px, py, pw, "Settings", "CubeVerse " CV_VERSION, a);
    Color c = rgba(0xee, 0xf1, 0xf6, (u8)(255 * a)), gold = rgba(0xff, 0xd8, 0x6a, (u8)(255 * a));
    for (int k = 0; k < VISIBLE && top + k < SN; k++) {
        int i = top + k;
        float y = py + 40 + k * 29;
        ui_row(px + 8, y, pw - 16, 27, i == srow, a);
        ui_text(FONT_MENU, 14, 1.4f, px + 20, y, 27, c, labels[i]);
        char v[260];
        value_text(i, v, sizeof(v));
        bool arrows = i != S_ACCOUNT && i != S_EXIT && i != S_UPDATE;
        float right = px + pw - 20;
        Color vc = (i == S_EXIT || i == S_ACCOUNT) && i == srow && exit_confirm ? gold : c;
        if (!arrows) {
            ui_text_right(FONT_MENU, 14, 1.4f, right, y, 27, vc, v);
            continue;
        }
        /* value between arrows; a long one (a file name) scrolls */
        float aw = gfx_text_width_sp(FONT_MENU, 14, 1.4f, "\x82", -1);
        float vw = gfx_text_width_sp(FONT_MENU, 14, 1.4f, v, -1), room = pw - 230;
        ui_text_right(FONT_MENU, 14, 1.4f, right, y, 27, vc, "\x82");
        float vx = right - aw - 10 - (vw < room ? vw : room);
        ui_text_fit_scroll(FONT_MENU, 14, 1.4f, vx, y, room, 27, vc, v);
        ui_text(FONT_MENU, 14, 1.4f, vx - 10 - aw, y, 27, vc, "\x81");
    }
    /* more above / below */
    Color soft = rgba(0xc9, 0xd2, 0xee, (u8)(200 * a));
    float mx = px + pw / 2;
    if (top > 0) gfx_tri(mx - 6, py + 38, mx + 6, py + 38, mx, py + 33, soft);
    if (top + VISIBLE < SN) {
        float yb = py + 40 + VISIBLE * 29 + 1;
        gfx_tri(mx - 6, yb, mx + 6, yb, mx, yb + 5, soft);
    }
    /* what the chosen setting does */
    float dy = py + ph - 10 - 44;
    ui_desc(px + 10, dy, pw - 20, 44, a);
    Color dc = rgba(0xec, 0xeb, 0xff, (u8)(255 * a));
    /* two lines at most: wrap at a space */
    const char *s = help[srow];
    char l1[160], l2[160];
    snprintf(l1, sizeof(l1), "%s", s);
    l2[0] = 0;
    float maxw = pw - 44;
    if (gfx_text_width_sp(FONT_MENU, 13, 1.2f, l1, -1) > maxw) {
        int cut = 0;
        for (int k = 0; s[k]; k++)
            if (s[k] == ' ' && gfx_text_width_sp(FONT_MENU, 13, 1.2f, s, k) <= maxw) cut = k;
        if (cut > 0) {
            snprintf(l1, sizeof(l1), "%.*s", cut, s);
            snprintf(l2, sizeof(l2), "%s", s + cut + 1);
        }
    }
    if (l2[0]) {
        ui_text(FONT_MENU, 13, 1.2f, px + 22, dy + 4, 18, dc, l1);
        ui_text_fit_scroll(FONT_MENU, 13, 1.2f, px + 22, dy + 22, maxw, 18, dc, l2);
    } else {
        ui_text(FONT_MENU, 13, 1.2f, px + 22, dy, 44, dc, l1);
    }
    const char *h[] = { "S:Select", "B:Close", "A:Change" };
    ui_hints(h, 3, a, input_stick_dir());
}
