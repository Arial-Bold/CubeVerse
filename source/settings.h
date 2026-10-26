/* settings.h - CubeVerse's settings, kept in sd:/cubeverse/settings.ini.
 * Field names shared with CubeBrowser's modules (music player, sounds) are
 * kept the same so that code is reused unchanged. */
#pragma once
#include <stdbool.h>

typedef struct {
    /* Network (static address only by editing settings.ini) */
    int  dhcp;
    char ip[16], netmask[16], gateway[16], dns[16];
    int  http_port;
    /* Display */
    int  video_mode, widescreen, full_width;
    int  margin;           /* screen margin for overscan, percent (0..12) */
    int  intro;            /* play the opening animation */
    /* Controls */
    int  pointer_speed;    /* 1 (slow) .. 10 (fast) */
    /* Sound */
    int  sounds, volume;
    int  menu_music;       /* CubeVerse's own background music in the menus */
    int  music_volume;
    /* Updates */
    int  update_auto;      /* check GitHub for a new version at start-up */
    /* Leaving */
    int  exit_action;      /* EXIT_LOAD_DOL or EXIT_RESTART */
    char swiss_dol[256];   /* the program Exit starts (sd:/boot.dol unless chosen) */
} Settings;

enum { EXIT_LOAD_DOL, EXIT_RESTART };

extern Settings g_set;

void settings_defaults(Settings *s);
bool settings_load(void);
bool settings_save(void);
