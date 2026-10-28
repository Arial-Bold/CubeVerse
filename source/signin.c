/* signin.c - signing in with a Pretendo Network ID: type it with the
 * on-screen keyboard, or scan the QR code and type it on a phone. The ID
 * and password are remembered on the SD card (see juxt.c) so CubeVerse can
 * sign in again by itself; Sign out in Settings forgets them. */
#include "app.h"
#include "cvui.h"
#include "keyboard.h"
#include "pointer.h"
#include "logo2d.h"
#include "input.h"
#include "sound.h"
#include "juxt.h"
#include "net.h"
#include "qrcodegen.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static char user[40], pass[64];
static Kbd kuser, kpass;
static int field = -1;          /* -1 none (keyboard hidden), 0 name, 1 password */
static JResult res;
static uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
static bool qr_ok;
static char qr_for[64];

#define ID_USER 10
#define ID_PASS 11
#define ID_GO 12
#define ID_DONE 13
#define ID_KEYS 100

static void make_qr(void) {
    const char *u = net_url();
    if (!u[0] || !strcmp(u, qr_for)) return;
    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    qr_ok = qrcodegen_encodeText(u, tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                 qrcodegen_Mask_AUTO, true);
    snprintf(qr_for, sizeof(qr_for), "%s", u);
}

static void enter(const char *arg, bool back) {
    (void)arg;
    if (back) return;
    pass[0] = 0;
    /* the ID and password from last time, if they were saved */
    juxt_saved_login(user, sizeof(user), pass, sizeof(pass));
    kbd_init(&kuser, user, sizeof(user));
    kbd_init(&kpass, pass, sizeof(pass));
    kuser.ok_label = "Next";
    kpass.ok_label = "Sign in";
    kuser.no_space = kpass.no_space = true;
    field = -1;
    res.state = JS_IDLE;
}

static void leave(void) { memset(pass, 0, sizeof(pass)); }

static void go(void) {
    if (res.state == JS_LOADING) return;
    if (!user[0] || !pass[0]) {
        app_toast(!user[0] ? "Type your Pretendo Network ID." : "Type your password.");
        sound_play(SFX_ERROR);
        field = !user[0] ? 0 : 1;
        return;
    }
    if (net_state() != NET_UP) { app_toast(net_message()); sound_play(SFX_ERROR); return; }
    sound_play(SFX_CONFIRM);
    field = -1;
    juxt_sign_in(&res, user, pass);
}

static void signed_in(void) {
    memset(pass, 0, sizeof(pass));
    char m[96];
    snprintf(m, sizeof(m), "Signed in as %s.", juxt_my_name());
    app_toast(m);
    app_home(g_home_section);   /* back to the cubes, signed in */
}

static void update(void) {
    if (net_state() == NET_UP) make_qr();
    if (res.state == JS_LOADING) return;
    if (res.state == JS_READY) { res.state = JS_IDLE; signed_in(); return; }
    if (res.state == JS_FAILED) {
        res.state = JS_IDLE;
        sound_play(SFX_ERROR);
        app_toast(res.msg[0] ? res.msg : "That didn't work. Check the ID and password.");
        memset(pass, 0, sizeof(pass));
        kpass.cursor = 0;
    }
    /* a sign-in typed on the phone */
    char pu[40], pp[64];
    if (net_take_signin(pu, sizeof(pu), pp, sizeof(pp))) {
        snprintf(user, sizeof(user), "%s", pu);
        snprintf(pass, sizeof(pass), "%s", pp);
        memset(pp, 0, sizeof(pp));
        kuser.cursor = (int)strlen(user);
        kpass.cursor = (int)strlen(pass);
        go();
        return;
    }
    int c = ptr_clicked();
    if (c == ID_USER) { field = 0; sound_play(SFX_SELECT); return; }
    if (c == ID_PASS) { field = 1; sound_play(SFX_SELECT); return; }
    if (c == ID_GO || input_pressed(BTN_START)) { go(); return; }
    if (c == ID_DONE) {
        sound_play(SFX_SELECT);
        if (field == 0) field = 1;
        else go();
        return;
    }
    if (input_pressed(BTN_B)) {
        sound_play(SFX_BACK);
        if (field >= 0) field = -1;
        else app_back();
        return;
    }
    if (field == 0) kbd_update(&kuser, c, ID_KEYS);
    else if (field == 1) kbd_update(&kpass, c, ID_KEYS);
    /* the keyboard's OK key: on to the password, then sign in */
    if (kuser.ok) { kuser.ok = false; sound_play(SFX_SELECT); field = 1; }
    if (kpass.ok) { kpass.ok = false; go(); }
}

static void field_box(int id, const char *label, Kbd *k, float x, float y, float w, bool focused, bool mask) {
    ui_text(FONT_ROUND, 11, 0, x, y, 16, CV_INK2, label);
    bool hot = ptr_hot(id, x, y + 18, w, 42);
    kbd_draw_field(k, x, y + 18, w, 42, 16, focused, mask, 1);
    if (hot && !focused) ui_gold_ring(x, y + 18, w, 42, 0.5f + 0.5f * sinf(g_time * 4), 1);
}

static void draw_qr(float x, float y, float size) {
    gfx_rect(x - 2, y - 2, size + 4, size + 4, rgba(0xB4, 0xBD, 0xD2, 255));
    gfx_rect(x, y, size, size, rgba(255, 255, 255, 255));
    if (!qr_ok) { ui_spinner(x + size / 2, y + size / 2, 10, g_time, 1); return; }
    int n = qrcodegen_getSize(qr);
    float m = size / (n + 8);
    float ox = x + 4 * m, oy = y + 4 * m;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
            if (qrcodegen_getModule(qr, i, j)) gfx_rect(ox + i * m, oy + j * m, m + 0.3f, m + 0.3f, rgba(0x14, 0x14, 0x1F, 255));
}

static void draw(float open) {
    (void)open;
    cv_background(1);
    /* title with the logo */
    float lw = logo_mark_width(18);
    logo_mark_draw(CV_L + 16, CV_TOP + 6, 18, 1);
    ui_text(FONT_MENU, 15, 1.6f, CV_L + 16 + lw + 8, CV_TOP + 9, 15, CV_T_GREEN, "Welcome to CubeVerse");
    ui_text_right(FONT_MENU, 15, 1.6f, UI_W - CV_L - 16, CV_TOP + 9, 15, COL_SOFT, "Sign in");
    gfx_rect(CV_L + 10, CV_TOP + 31, UI_W - 2 * CV_L - 20, 1, rgba(200, 210, 230, 71));
    float x = CV_L + 18, w = UI_W - 2 * x;
    ui_text(FONT_MENU, 13, 1.0f, x, 56, 20, rgba(0xdf, 0xe4, 0xee, 255),
            "Sign in with your Pretendo Network ID to read and post on Juxt.");
    bool wide = UI_W > 700;
    float qw = wide ? 250 : 200, gap = 16, fw = w - qw - gap, cy = 86;
    if (field < 0) {
        /* the form, filling the screen down to the hints */
        float ch = CV_BOTTOM - 6 - cy;
        cv_card(x, cy, fw, ch, false, 1);
        ui_text(FONT_CUBE, 17, 0, x + 18, cy + 14, 22, CV_INK, "Type it here");
        field_box(ID_USER, "Pretendo Network ID", &kuser, x + 18, cy + 52, fw - 36, false, false);
        field_box(ID_PASS, "Password", &kpass, x + 18, cy + 124, fw - 36, false, true);
        cv_wrap(FONT_ROUND, 12, 0, "Your password only goes to Pretendo. CubeVerse remembers it on the SD card so you stay signed in. Sign out in Settings to forget it.",
                x + 18, cy + 196, fw - 36, 18, 3, CV_INK2, true);
        bool hg = ptr_hot(ID_GO, x + 18, cy + ch - 62, fw - 36, 44);
        cv_pill(x + 18, cy + ch - 62, fw - 36, 44, "Sign in", CV_GREEN, rgba(255, 255, 255, 255), hg, 1);
        /* the phone */
        float qx = x + fw + gap;
        cv_card(qx, cy, qw, ch, false, 1);
        ui_text(FONT_CUBE, 17, 0, qx + 16, cy + 14, 22, CV_INK, "Or use your phone");
        float qs = qw - 64 > 170 ? 170 : qw - 64;
        draw_qr(qx + qw / 2 - qs / 2, cy + 52, qs);
        float ty = cy + 52 + qs + 16;
        if (net_state() == NET_UP) {
            cv_wrap(FONT_ROUND, 12, 0, "Scan and sign in on your phone. Same Wi-Fi as the GameCube.", qx + 16, ty, qw - 32, 17, 3, CV_INK2, true);
            ui_text_center(FONT_ROUND, 14, 0, qx + qw / 2, cy + ch - 36, 22, rgba(0x1B, 0x6A, 0x99, 255), net_ip());
        } else {
            cv_wrap(FONT_ROUND, 12, 0, net_message(), qx + 16, ty, qw - 32, 17, 4, CV_INK2, true);
        }
    } else {
        /* typing: the field on top, the keyboard under it */
        float ch = 78;
        cv_card(x, cy, w, ch, false, 1);
        if (field == 0) field_box(ID_USER, "Pretendo Network ID", &kuser, x + 16, cy + 8, w - 144, true, false);
        else field_box(ID_PASS, "Password", &kpass, x + 16, cy + 8, w - 144, true, true);
        bool hd = ptr_hot(ID_DONE, x + w - 116, cy + 26, 100, 42);
        cv_pill(x + w - 116, cy + 26, 100, 42, field == 0 ? "Next" : "Sign in", field == 0 ? rgba(0x29, 0xA8, 0xE0, 255) : CV_GREEN,
                rgba(255, 255, 255, 255), hd, 1);
        float kw = w > 660 ? 660 : w;
        kbd_draw(field == 0 ? &kuser : &kpass, x + (w - kw) / 2, cy + ch + 18, kw, 40, 7, ID_KEYS, 1);
    }
    if (field >= 0) {
        const char *h[] = { "A:Type", "X:Delete", "LR:Cursor", "ST:Sign in", "B:Done" };
        ui_hints(h, 5, 1, -1);
    } else {
        const char *h[] = { "S:Pointer", "A:Type", "ST:Sign in", "B:Back" };
        ui_hints(h, 4, 1, input_stick_dir());
    }
    if (res.state == JS_LOADING) {
        gfx_fill_screen(rgba(0, 0, 0, 128));
        float bw = 300, bh = 110, bx = UI_W / 2 - bw / 2, by = 240 - bh / 2 - 20;
        cv_card(bx, by, bw, bh, false, 1);
        ui_spinner(bx + bw / 2, by + 40, 12, g_time, 1);
        ui_text_center(FONT_ROUND, 15, 0, bx + bw / 2, by + 66, 24, CV_INK, "Signing in...");
    }
}

const Screen screen_signin = { enter, update, draw, leave };
