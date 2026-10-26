/* settings.c - defaults and a small key=value INI file. */
#include "settings.h"
#include "storage.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Settings g_set;

typedef struct { const char *key; int is_str; size_t off; int min, max, size; } Key;
#define I(k, f, lo, hi) { k, 0, offsetof(Settings, f), lo, hi, 0 }
#define S(k, f) { k, 1, offsetof(Settings, f), 0, 0, sizeof(((Settings *)0)->f) }
static const Key keys[] = {
    I("net.dhcp", dhcp, 0, 1), S("net.ip", ip), S("net.netmask", netmask), S("net.gateway", gateway),
    S("net.dns", dns), I("net.port", http_port, 1, 65535),
    I("display.video", video_mode, 0, 2), I("display.widescreen", widescreen, 0, 1),
    I("display.full_width", full_width, 0, 1), I("display.margin", margin, 0, 12), I("display.intro", intro, 0, 1), I("control.pointer_speed", pointer_speed, 1, 10),
    I("sound.menu", sounds, 0, 1), I("sound.volume", volume, 0, 10), I("sound.menu_music", menu_music, 0, 1),
    I("music.volume", music_volume, 0, 10),
    I("update.auto", update_auto, 0, 1), I("exit.action", exit_action, 0, 1), S("exit.dol", swiss_dol),
};
#define NKEYS (int)(sizeof(keys) / sizeof(keys[0]))

void settings_defaults(Settings *s) {
    memset(s, 0, sizeof(*s));
    s->dhcp = 1;
    snprintf(s->netmask, sizeof(s->netmask), "255.255.255.0");
    s->http_port = 80;
    s->video_mode = 0;
    s->widescreen = 0;
    s->full_width = 1;
    s->margin = 3;
    s->intro = 1;
    s->pointer_speed = 4;
    s->sounds = 1;
    s->volume = 7;
    s->menu_music = 1;
    s->update_auto = 1;
    s->music_volume = 7;
    snprintf(s->swiss_dol, sizeof(s->swiss_dol), "sd:/boot.dol");
}

static void trim(char *s) {
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
}

bool settings_load(void) {
    settings_defaults(&g_set);
    char path[300];
    storage_path(path, sizeof(path), "settings.ini");
    char *text = storage_read(path, NULL);
    if (!text) return false;
    char *line = text;
    bool has_action = strstr(text, "exit.action=") != NULL;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        trim(line);
        char *eq = strchr(line, '=');
        if (eq && line[0] != '#' && line[0] != ';') {
            *eq = 0;
            char *k = line, *v = eq + 1;
            trim(k);
            while (*v == ' ') v++;
            for (int i = 0; i < NKEYS; i++) {
                if (strcmp(keys[i].key, k)) continue;
                char *field = (char *)&g_set + keys[i].off;
                if (keys[i].is_str) {
                    snprintf(field, (size_t)keys[i].size, "%s", v);
                } else {
                    int n = atoi(v);
                    if (n < keys[i].min) n = keys[i].min;
                    if (n > keys[i].max) n = keys[i].max;
                    *(int *)field = n;
                }
            }
        }
        line = next;
    }
    free(text);
    /* files from before the Exit choice: their default was ipl.dol */
    if (!has_action && !strcmp(g_set.swiss_dol, "sd:/ipl.dol"))
        snprintf(g_set.swiss_dol, sizeof(g_set.swiss_dol), "sd:/boot.dol");
    return true;
}

bool settings_save(void) {
    char buf[4096];
    int n = snprintf(buf, sizeof(buf), "# CubeVerse settings\n");
    for (int i = 0; i < NKEYS && n < (int)sizeof(buf) - 300; i++) {
        const char *field = (const char *)&g_set + keys[i].off;
        if (keys[i].is_str) n += snprintf(buf + n, sizeof(buf) - n, "%s=%s\n", keys[i].key, field);
        else n += snprintf(buf + n, sizeof(buf) - n, "%s=%d\n", keys[i].key, *(const int *)field);
    }
    char path[300];
    storage_path(path, sizeof(path), "settings.ini");
    return storage_write_atomic(path, buf, (size_t)n);
}
