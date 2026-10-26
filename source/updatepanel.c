/* updatepanel.c - the Software Update window: what's new in the latest
 * version on GitHub, and buttons to install it, restart into it or close.
 * Opened from Settings, or with Z on the cube when an update was found. */
#include "app.h"
#include "update.h"
#include "version.h"
#include "gfx.h"
#include "ui.h"
#include "input.h"
#include "sound.h"
#include "m3d.h"
#include <stdio.h>
#include <string.h>

static bool active;
static float t;
static int button;          /* 0 = left button, 1 = right */
static float scroll;        /* notes scroll, in lines */

bool update_panel_active(void) { return active; }

void update_panel_open(bool check_now) {
    active = true;
    t = 0;
    button = 0;
    scroll = 0;
    UpdateState s = update_state();
    if (check_now && (s == UPD_NONE || s == UPD_LATEST || s == UPD_CHECK_FAILED)) update_request_check(false);
}

/* the two buttons for each state (NULL: none) */
static void buttons(const char **l, const char **r) {
    *l = *r = NULL;
    switch (update_state()) {
    case UPD_NONE:
    case UPD_CHECKING: *r = "Close"; break;
    case UPD_LATEST: *l = "Check Again"; *r = "Close"; break;
    case UPD_CHECK_FAILED: *l = "Try Again"; *r = "Close"; break;
    case UPD_AVAILABLE: *l = "Install"; *r = "Later"; break;
    case UPD_INSTALLING: *r = "Stop"; break;
    case UPD_INSTALLED: *l = "Restart Now"; *r = "Later"; break;
    case UPD_FAILED: *l = "Try Again"; *r = "Close"; break;
    }
}

static const char *button_help(const char *b) {
    if (!b) return "";
    if (!strcmp(b, "Install")) return "Downloads the new version from GitHub and puts it in place of this one. Photos and settings stay as they are.";
    if (!strcmp(b, "Restart Now")) return "Starts the new version of CubeVerse now.";
    if (!strcmp(b, "Stop")) return "Stops the download. Nothing is changed.";
    if (!strcmp(b, "Check Again") || !strcmp(b, "Try Again")) return "Asks GitHub again for the newest version.";
    if (!strcmp(b, "Later")) return "Closes this window. Settings > Software update brings it back.";
    return "Closes this window.";
}

#define NOTE_LINES 64
static char lines[NOTE_LINES][128];
static int nlines;
static char wrapped_for[64];

/* Wraps the release notes into lines that fit the box. */
static void wrap_notes(float maxw) {
    const char *s = update_notes();
    char key[64];
    snprintf(key, sizeof(key), "%s/%d", update_latest_version(), (int)strlen(s));
    if (!strcmp(key, wrapped_for)) return;
    snprintf(wrapped_for, sizeof(wrapped_for), "%s", key);
    nlines = 0;
    while (*s && nlines < NOTE_LINES) {
        size_t para = strcspn(s, "\n");
        if (para == 0) { lines[nlines++][0] = 0; s++; continue; }
        const char *p = s, *end = s + para;
        while (p < end && nlines < NOTE_LINES) {
            int fit = 0, last_space = -1;
            int n = (int)(end - p);
            while (fit < n && fit < 126) {
                if (gfx_text_width_sp(FONT_MENU, 13, 1.2f, p, fit + 1) > maxw) break;
                if (p[fit] == ' ') last_space = fit;
                fit++;
            }
            if (fit < n && last_space > 0) fit = last_space;
            if (fit <= 0) fit = 1;
            snprintf(lines[nlines++], 128, "%.*s", fit, p);
            p += fit;
            while (p < end && *p == ' ') p++;
        }
        s = end;
        if (*s == '\n') s++;
    }
}

void update_panel_update(void) {
    t += g_dt;
    const char *l, *r;
    buttons(&l, &r);
    if (!l) button = 1;
    int d = input_dir();
    if (d == DIR_LEFT && l && button == 1) { button = 0; sound_play(SFX_HOVER); }
    if (d == DIR_RIGHT && button == 0) { button = 1; sound_play(SFX_HOVER); }
    if (d == DIR_UP && scroll > 0) scroll -= 1;
    if (d == DIR_DOWN && scroll < nlines - 1) scroll += 1;
    if (scroll > nlines - 1) scroll = (float)(nlines - 1);
    if (scroll < 0) scroll = 0;
    if (input_pressed(BTN_A)) {
        const char *b = button == 0 ? l : r;
        if (!b) return;
        if (!strcmp(b, "Install")) { sound_play(SFX_CONFIRM); update_request_install(); button = 1; }
        else if (!strcmp(b, "Restart Now")) {
            sound_play(SFX_CONFIRM);
            char err[160];
            if (!update_restart(err, sizeof(err))) app_toast(err);
        } else if (!strcmp(b, "Stop")) { sound_play(SFX_CANCEL); update_cancel(); }
        else if (!strcmp(b, "Check Again") || !strcmp(b, "Try Again")) {
            sound_play(SFX_SELECT);
            if (update_state() == UPD_FAILED) update_request_install();
            else update_request_check(false);
        } else { sound_play(SFX_CANCEL); active = false; }
        return;
    }
    if (input_pressed(BTN_B)) {
        sound_play(SFX_CANCEL);
        active = false;   /* an install carries on behind; the cube says when it's done */
    }
}

void update_panel_draw(void) {
    if (!active) return;
    float a = ease_out(clamp01(t / 0.2f));
    gfx_fill_screen(rgba(0, 0, 0, (u8)(160 * a)));
    float pw = 520, ph = 380, px = UI_X + UI_W / 2 - pw / 2, py = 22 + 10 * (1 - a);
    ui_panel(px, py, pw, ph, a);
    ui_ptitle(px, py, pw, "Software Update", "CubeVerse " CV_VERSION, a);
    Color text = rgba(0xee, 0xf1, 0xf6, (u8)(255 * a)), soft = rgba(0xc9, 0xd2, 0xee, (u8)(255 * a));
    Color gold = rgba(0xf0, 0xd2, 0x5a, (u8)(255 * a));

    /* the headline */
    UpdateState s = update_state();
    char head[96], sub[300];
    const char *v = update_latest_version();
    sub[0] = 0;
    switch (s) {
    case UPD_NONE:
    case UPD_CHECKING: snprintf(head, sizeof(head), "Checking for a new version..."); snprintf(sub, sizeof(sub), "Asking GitHub (github.com/%s).", UPDATE_REPO); break;
    case UPD_LATEST: snprintf(head, sizeof(head), "CubeVerse is up to date"); snprintf(sub, sizeof(sub), "You have version %s, the newest one.", CV_VERSION); break;
    case UPD_CHECK_FAILED: snprintf(head, sizeof(head), "Couldn't check for updates"); snprintf(sub, sizeof(sub), "%s", update_error()); break;
    case UPD_AVAILABLE: snprintf(head, sizeof(head), "CubeVerse %s is available", v); snprintf(sub, sizeof(sub), "You have version %s.", CV_VERSION); break;
    case UPD_INSTALLING: {
        snprintf(head, sizeof(head), "Installing CubeVerse %s", v);
        UpdateStep st = update_step();
        snprintf(sub, sizeof(sub), "%s", st == UPD_STEP_NETWORK ? "Connecting to the network..." : st == UPD_STEP_DOWNLOAD ? "Downloading..."
                                        : st == UPD_STEP_UNPACK ? "Unpacking..." : "Putting the new version in place...");
        break;
    }
    case UPD_INSTALLED: snprintf(head, sizeof(head), "CubeVerse %s is installed", v); snprintf(sub, sizeof(sub), "Restart CubeVerse to start using it. The old version was kept as a .old file."); break;
    case UPD_FAILED: snprintf(head, sizeof(head), "The update didn't finish"); snprintf(sub, sizeof(sub), "%s", update_error()); break;
    }
    float hy = py + 42;
    float hx = px + 22;
    if (s == UPD_AVAILABLE || s == UPD_INSTALLED) {
        ui_text(FONT_MENU_BOLD, 15, 1.4f, hx, hy, 22, gold, "\x84");
        hx += 22;
    }
    if (s == UPD_CHECKING || s == UPD_INSTALLING) {
        ui_spinner(px + pw - 34, hy + 11, 9, g_time, a);
    }
    ui_text_fit_scroll(FONT_MENU_BOLD, 16, 1.2f, hx, hy, px + pw - 50 - hx, 22, text, head);
    ui_text_fit_scroll(FONT_MENU, 13, 1.2f, px + 22, hy + 24, pw - 44, 18, soft, sub);

    /* what's new, or the progress */
    float bx = px + 14, by = hy + 50, bw = pw - 28, bh = py + ph - 108 - by;
    bool show_notes = (s == UPD_AVAILABLE || s == UPD_INSTALLING || s == UPD_INSTALLED || s == UPD_FAILED) && update_notes()[0];
    if (show_notes) {
        gfx_rrect(bx, by, bw, bh, 4, rgba(0, 0, 0, (u8)(90 * a)));
        gfx_rrect_outline(bx, by, bw, bh, 4, 1, rgba(200, 210, 230, (u8)(60 * a)));
        ui_text(FONT_MENU, 13, 1.4f, bx + 12, by + 6, 18, rgba(0x73, 0x92, 0xff, (u8)(255 * a)), "What's new");
        wrap_notes(bw - 34);
        float ly = by + 28, lh = 18;
        int vis = (int)((bh - 34) / lh);
        int first = (int)(scroll + 0.5f);
        if (first > nlines - vis) first = nlines - vis;
        if (first < 0) first = 0;
        gfx_set_clip(bx + 2, ly - 2, bw - 4, vis * lh + 4);
        for (int i = 0; i < vis && first + i < nlines; i++)
            ui_text(FONT_MENU, 13, 1.2f, bx + 12, ly + i * lh, lh, text, lines[first + i]);
        gfx_clear_clip();
        /* scroll marks */
        float mx = bx + bw - 14;
        if (first > 0) gfx_tri(mx - 5, ly + 6, mx + 5, ly + 6, mx, ly, soft);
        if (first + vis < nlines) { float yy = ly + vis * lh - 6; gfx_tri(mx - 5, yy, mx + 5, yy, mx, yy + 6, soft); }
    }
    if (s == UPD_INSTALLING) {
        float p = update_progress();
        float gy = py + ph - 104;
        gfx_rrect(px + 22, gy, pw - 44, 8, 3, rgba(0, 0, 0, (u8)(120 * a)));
        gfx_rrect_outline(px + 22, gy, pw - 44, 8, 3, 1, rgba(200, 210, 230, (u8)(76 * a)));
        if (p >= 0) {
            gfx_rect_vgrad(px + 23, gy + 1, (pw - 46) * clamp01(p), 6, rgba(0x7a, 0x6c, 0xff, (u8)(255 * a)), rgba(0x3b, 0x2c, 0xb6, (u8)(255 * a)));
        } else {   /* unknown: a block sliding along */
            float k = fmodf(g_time * 0.6f, 1.0f);
            gfx_rect(px + 23 + (pw - 46 - 60) * k, gy + 1, 60, 6, rgba(0x7a, 0x6c, 0xff, (u8)(255 * a)));
        }
    }

    /* the buttons */
    const char *l, *r;
    buttons(&l, &r);
    float bwid = 150, gap = 40, yb = py + ph - 92;
    if (l) {
        float x0 = px + pw / 2 - bwid - gap / 2;
        ui_button(x0, yb, bwid, 28, l, button == 0, a);
        ui_button(x0 + bwid + gap, yb, bwid, 28, r, button == 1, a);
    } else {
        ui_button(px + pw / 2 - bwid / 2, yb, bwid, 28, r, true, a);
    }

    /* what the chosen button does */
    float dy = py + ph - 54;
    ui_desc(px + 10, dy, pw - 20, 44, a);
    ui_text_fit_scroll(FONT_MENU, 13, 1.2f, px + 22, dy, pw - 44, 44, rgba(0xec, 0xeb, 0xff, (u8)(255 * a)),
                       button_help(button == 0 ? l : r));

    const char *h[] = { "S:Scroll", "B:Close", "A:Confirm" };
    ui_hints(h, 3, a, input_stick_dir());
}
