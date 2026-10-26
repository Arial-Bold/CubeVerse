/* feed.c - Activity: posts from the people and communities you follow
 * (My Feed), from people only, or from everyone (Global). */
#include "app.h"
#include "cvui.h"
#include "postlist.h"
#include "pointer.h"
#include "input.h"
#include "sound.h"
#include "juxt.h"
#include <stdio.h>
#include <string.h>

static const char *const TABS[3] = { "My Feed", "People", "Global" };
static const JPageKind KINDS[3] = { JP_FEED, JP_FEED_PEOPLE, JP_FEED_ALL };
static JPage page;
static PostList list;
static int tab;

#define ID_TAB 10
#define ID_LIST 1000

static void load(void) {
    juxt_load(&page, KINDS[tab], "");
    pl_reset_scroll(&list);
}

static void feed_enter(const char *arg, bool back) {
    (void)arg;
    if (!back || page.state != JS_READY || g_posted) {
        if (!back) tab = 0;
        pl_init(&list, &page, 1, ID_LIST);
        list.show_community = true;
        load();
    }
    g_posted = false;
    pl_empty_text = tab == 0 ? "Follow people and communities, and their posts will show up here." : "There are no posts here yet.";
}

static void set_tab(int t) {
    if (t == tab) return;
    tab = (t + 3) % 3;
    sound_play(SFX_SELECT);
    load();
    pl_empty_text = tab == 0 ? "Follow people and communities, and their posts will show up here." : "There are no posts here yet.";
}

static void feed_update(void) {
    int c = ptr_clicked();
    if (c >= ID_TAB && c < ID_TAB + 3) { set_tab(c - ID_TAB); return; }
    if (input_pressed(BTN_L)) { set_tab(tab - 1); return; }
    if (input_pressed(BTN_R)) { set_tab(tab + 1); return; }
    if (input_pressed(BTN_B)) { sound_play(SFX_BACK); app_back(); return; }
    if (page.state == JS_FAILED && input_pressed(BTN_A)) { sound_play(SFX_SELECT); load(); return; }
    pl_update(&list);
}

static void feed_draw(float open) {
    (void)open;
    cv_background(1);
    char right[48] = "";
    if (page.state == JS_READY) snprintf(right, sizeof(right), "%d post%s", page.nposts, page.nposts == 1 ? "" : "s");
    cv_title("Activity", CV_T_GREEN, right, 1);
    float x = cv_side(), w = UI_W - 2 * x;
    cv_tabs(CV_L + 10, 52, TABS, 3, tab, ID_TAB, 1);
    pl_draw(&list, x, 86, w, CV_BOTTOM, 1);
    const char *h[] = { "S:Pointer", "LR:Tab", "X:Yeah!", "Z:Their page", "B:Back", "A:Open" };
    ui_hints(h, 6, 1, input_stick_dir());
}

const Screen screen_feed = { feed_enter, feed_update, feed_draw, NULL };
