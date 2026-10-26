/* apptest.c - runs CubeVerse on a PC with scripted controller input, against
 * server.py (a stand-in for Juxt on 127.0.0.1:8099), saving screenshots. */
#include "app.h"
#include "input.h"
#include "pointer.h"
#include "juxt.h"
#include "cvui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
void gxemu_save(const char *path, int out_w);
bool gfx_widescreen(void);
void gfx_set_widescreen(bool wide);
static int frame;
static void run(int frames) { for (int i = 0; i < frames; i++) { app_frame(); frame++; usleep(1500); } }
static void shot(const char *name) {
    char p[128];
    snprintf(p, sizeof(p), "out/%s.ppm", name);
    gxemu_save(p, gfx_widescreen() ? 854 : 640);
    printf("shot %s (frame %d, screen %d)\n", name, frame, app_screen());
}
static void press(uint32_t b) { input_inject(b); run(1); }
static void hold(uint32_t b, float sx, float sy, int frames) { input_test_state(b, sx, sy, 0, 0); run(frames); input_test_state(0, 0, 0, 0, 0); run(1); }
/* points the hand at (x, y) and presses A */
static void click(float x, float y) { ptr_set(x, y); run(2); press(BTN_A); }
static void wait_loaded(int max) { run(max); }

int main(int argc, char **argv) {
    const char *script = argc > 1 ? argv[1] : "tour";
    bool wide = argc > 2 && !strcmp(argv[2], "wide");
    app_init();
    if (wide) gfx_set_widescreen(true);
    if (!strcmp(script, "intro")) {
        float t[] = { 0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f, 8.5f, 9.5f, 10.5f };
        float now = 0;
        for (int i = 0; i < 11; i++) {
            int f = (int)((t[i] - now) * 60);
            run(f);
            now = t[i];
            char n[32];
            snprintf(n, sizeof(n), "intro_%04.1f", t[i]);
            shot(n);
        }
        return 0;
    }
    if (!strcmp(script, "bg")) {
        /* the background options, on the home screen and a list */
        run(60 * 12);
        JResult r = { 0 };
        juxt_sign_in(&r, "kirbyfan", "secret");
        for (int i = 0; i < 300 && r.state != JS_READY && r.state != JS_FAILED; i++) run(1);
        for (int st = 0; st <= 4; st++) {
            cv_set_bg_style(st);
            char n[32];
            run(40); snprintf(n, sizeof(n), "bg%d_home", st); shot(n);
        }
        hold(0, 1, 0, 6); run(70); hold(0, 1, 0, 6); run(70); hold(0, 1, 0, 6); run(70);   /* to My page */
        press(BTN_A); run(150);
        for (int st = 0; st <= 4; st++) {
            cv_set_bg_style(st);
            char n[32];
            run(20); snprintf(n, sizeof(n), "bg%d_mypage", st); shot(n);
        }
        return 0;
    }
    run(60 * 12);                 /* the opening */
    shot("01_home");
    if (!strcmp(script, "tour")) {
        /* sign in (directly; the keyboard is shown further down) */
        JResult r = { 0 };
        juxt_sign_in(&r, "kirbyfan", "secret");
        for (int i = 0; i < 300 && r.state != JS_READY && r.state != JS_FAILED; i++) run(1);
        printf("sign in: %d %s name=%s pid=%d\n", r.state, r.msg, juxt_my_name(), juxt_my_pid());
        run(30); shot("02_home_signed_in");
        press(BTN_A); wait_loaded(90); shot("03_activity");
        hold(0, 0, 0.5f, 50); shot("04_activity_pointer");
        input_test_state(0, 0, 0, 0, 0);
        for (int i = 0; i < 40; i++) { input_test_state(0, 0, 0, 0, 0); run(1); }
        press(BTN_R); wait_loaded(90); shot("05_activity_people");
        press(BTN_B); run(60);
        hold(0, 1, 0, 6); run(70); shot("06_home_communities");
        press(BTN_A); wait_loaded(120); shot("07_communities");
        click(140, 160); wait_loaded(120); shot("08_community");
        click(170, 230); wait_loaded(120); shot("09_post");
        press(BTN_X); run(40); shot("10_post_yeah");
        press(BTN_Y); run(60); shot("11_compose");
        click(320 - 70, 300); run(10);
        ptr_set(200, 300); run(5);
        press(BTN_A); run(5); press(BTN_A); run(5);
        shot("12_compose_typed");
        click(560, 30); run(60); shot("13_draw");
        /* a scribble */
        ptr_set(120, 120); run(3);
        input_test_state(BTN_A, 0.8f, 0.4f, 0, 0); run(30);
        input_test_state(BTN_A, 0.2f, -0.8f, 0, 0); run(20);
        input_test_state(BTN_A, 0.9f, 0.6f, 0, 0); run(25);
        input_test_state(0, 0, 0, 0, 0); run(5);
        shot("14_draw_scribble");
        press(BTN_START); run(90); shot("15_after_post");
        press(BTN_B); run(40); press(BTN_B); run(40); press(BTN_B); run(80);
        hold(0, 1, 0, 6); run(70);
        press(BTN_A); wait_loaded(90); shot("16_notifications");
        press(BTN_B); run(60);
        hold(0, 1, 0, 6); run(70);
        press(BTN_A); wait_loaded(90); shot("17_my_page");
        hold(0, 0, 1, 150); shot("17b_my_page_scrolled");
        press(BTN_B); run(60); press(BTN_A); wait_loaded(90);
        click(120, 210); wait_loaded(90); shot("18_post_from_profile");
        press(BTN_B); run(40); press(BTN_B); run(80);
        press(BTN_START); run(30); shot("19_settings");
    } else if (!strcmp(script, "signin")) {
        press(BTN_A); run(60); shot("s1_signin");
        click(200, 160); run(20); shot("s2_keyboard");
        juxt_sign_out();
    }
    return 0;
}
