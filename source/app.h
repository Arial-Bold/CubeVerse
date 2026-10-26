/* app.h - moving between CubeVerse's screens. */
#pragma once
#include <stdbool.h>

typedef enum {
    SCR_SPLASH,       /* the opening: blocks build the logo, which becomes the home cubes */
    SCR_HOME,         /* the four cubes */
    SCR_SIGNIN,
    SCR_FEED,         /* Activity */
    SCR_COMMUNITIES,
    SCR_COMMUNITY,    /* arg: community id */
    SCR_POST,         /* arg: post id */
    SCR_COMPOSE,      /* arg: "c:<community id>" or "r:<post id>:<community id>" */
    SCR_DRAW,         /* the same */
    SCR_USER,         /* arg: pid, or "" for my page */
    SCR_NEWS,         /* Notifications */
    SCR_COUNT
} ScreenId;

/* Each screen: enter (with an argument), update (input, once a frame),
 * draw. `open` is seconds since the screen appeared. */
typedef struct {
    void (*enter)(const char *arg, bool back);   /* back: returning to it */
    void (*update)(void);
    void (*draw)(float open);
    void (*leave)(void);
} Screen;

/* Opens a screen (remembering where we were, for app_back). */
void app_go(ScreenId s, const char *arg);
/* Switches straight away, with no fade (the opening hands over to the home cubes). */
void app_replace(ScreenId s, const char *arg);
/* Switches screen with the fade but without remembering this one (Text <-> Drawing). */
void app_swap(ScreenId s, const char *arg);
/* Back to the screen before (the home cubes if none). */
void app_back(void);
/* Back to the home cubes, showing cube `section` (0..3) at the front. */
void app_home(int section);
ScreenId app_screen(void);
const Screen *app_screen_def(ScreenId s);
void app_init(void);
void app_frame(void);
const char *app_exit_label(void);
bool app_exit(void);

/* A message for a few seconds across the top of the screen. */
void app_toast(const char *msg);

/* The home cube to show when going home. */
extern int g_home_section;
/* The unread notification count (updated in the background), or -1. */
extern int g_unread;
/* Set after posting, so the page we go back to loads again. */
extern bool g_posted;
/* The common layout: the list's left edge and width for this screen width. */
float cv_side(void);

extern const Screen screen_splash, screen_home, screen_signin, screen_feed, screen_communities, screen_community,
    screen_post, screen_compose, screen_draw, screen_user, screen_news;

/* Settings overlay (START on the home screen). */
bool settings_panel_active(void);
void settings_panel_open(void);
void settings_panel_update(void);
void settings_panel_draw(void);

/* The Software Update window. */
bool update_panel_active(void);
void update_panel_open(bool check_now);
void update_panel_update(void);
void update_panel_draw(void);
