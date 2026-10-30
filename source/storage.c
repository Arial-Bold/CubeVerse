/* storage.c - mounts the SD card through libfat (the same adapters as
 * CubeBrowser: SD2SP2 in Serial Port 2, SD Gecko in a memory card slot). */
#include "storage.h"
#include <fat.h>
#include <sdcard/gcsd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <strings.h>

static bool mounted;
static const char *device = "none";
static char data_dir[300] = LEGACY_DATA_DIR;
static char argv0_copy[300];
static char prog_file[300];        /* "" = unknown */

bool storage_is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

bool storage_is_file(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && !S_ISDIR(st.st_mode);
}

void storage_mkdirs(const char *path) {
    char p[300];
    snprintf(p, sizeof(p), "%s", path);
    char *s = strstr(p, ":/");
    s = s ? s + 2 : p;
    for (; *s; s++) {
        if (*s == '/') { *s = 0; mkdir(p, 0777); *s = '/'; }
    }
    mkdir(p, 0777);
}

void storage_set_program(const char *argv0) {
    snprintf(argv0_copy, sizeof(argv0_copy), "%s", argv0 ? argv0 : "");
}

/* argv[0] as the loader gave it ("sd:/apps/cubeverse/default.dol"; the
 * device name may differ), trusted only if that file is on our card. A
 * restart after an update (no arguments) left the path in RESTART_NOTE. */
static void find_program(void) {
    char a[300];
    snprintf(a, sizeof(a), "%s", argv0_copy);
    if (!a[0]) {
        FILE *f = fopen(RESTART_NOTE, "rb");
        if (f) {
            size_t n = fread(a, 1, sizeof(a) - 1, f);
            fclose(f);
            a[n] = 0;
            while (n && (a[n - 1] == '\n' || a[n - 1] == '\r')) a[--n] = 0;
        }
    }
    remove(RESTART_NOTE);   /* only good for the one start */
    prog_file[0] = 0;
    const char *colon = strstr(a, ":/");
    const char *path = colon ? colon + 1 : a;
    if (path[0] != '/') return;
    char full[300];
    if (snprintf(full, sizeof(full), "sd:%s", path) >= (int)sizeof(full)) return;
    if (storage_is_file(full)) snprintf(prog_file, sizeof(prog_file), "%s", full);
}

/* Moves what an older version kept in sd:/cubeverse into the new folder
 * (settings, sign-in, picture cache...). The release's own cacert.pem and
 * README.txt are already in the new folder, so those stay put. */
static void move_old_data(const char *to) {
    DIR *d = opendir(LEGACY_DATA_DIR "/");
    if (!d) return;
    struct dirent *e;
    char names[64][128];
    int n = 0;
    while ((e = readdir(d)) != NULL && n < 64) {
        if (e->d_name[0] == '.') continue;
        snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
    }
    closedir(d);
    for (int i = 0; i < n; i++) {
        char from[300], dest[440];
        snprintf(from, sizeof(from), "%s/%s", LEGACY_DATA_DIR, names[i]);
        snprintf(dest, sizeof(dest), "%s/%s", to, names[i]);
        if (storage_is_file(dest) || storage_is_dir(dest)) {
            if (!strcasecmp(names[i], "cacert.pem") || !strcasecmp(names[i], "README.txt")) remove(from);
            continue;   /* never overwrite */
        }
        rename(from, dest);
    }
    rmdir(LEGACY_DATA_DIR);   /* only goes if it's empty now */
}

bool storage_init(void) {
    struct { const DISC_INTERFACE *io; const char *name; } devs[] = {
        { &__io_gcsd2, "SD2SP2" },
        { &__io_gcsdb, "SD Gecko (slot B)" },
        { &__io_gcsda, "SD Gecko (slot A)" },
    };
    for (unsigned i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
        if (fatMountSimple("sd", devs[i].io)) {
            mounted = true;
            device = devs[i].name;
            break;
        }
    }
    if (!mounted) return false;
    find_program();
    /* the folder the .dol is in, unless that's the root of the card */
    const char *known = prog_file[0] ? prog_file : NULL;
    char dir[300] = "";
    if (!known && storage_is_file(APP_DIR "/default.dol")) known = APP_DIR "/default.dol";
    if (known) {
        snprintf(dir, sizeof(dir), "%s", known);
        char *slash = strrchr(dir, '/');
        if (slash) *slash = 0;
        if (!strcmp(dir, "sd:")) dir[0] = 0;
    }
    if (dir[0]) snprintf(data_dir, sizeof(data_dir), "%s", dir);
    if (strcasecmp(data_dir, LEGACY_DATA_DIR) != 0) {
        char s1[340], s2[340];
        snprintf(s1, sizeof(s1), "%s/settings.ini", data_dir);
        snprintf(s2, sizeof(s2), "%s/settings.ini", LEGACY_DATA_DIR);
        if (!storage_is_file(s1) && storage_is_file(s2)) move_old_data(data_dir);
    }
    storage_mkdirs(data_dir);
    char cache[320];
    snprintf(cache, sizeof(cache), "%s/cache", data_dir);
    storage_mkdirs(cache);
    return true;
}

bool storage_ok(void) { return mounted; }
const char *storage_device_name(void) { return device; }
const char *storage_dir(void) { return data_dir; }
const char *storage_program_file(void) { return prog_file[0] ? prog_file : NULL; }

void storage_path(char *out, size_t cap, const char *name) { snprintf(out, cap, "%s/%s", data_dir, name); }

char *storage_read(const char *path, size_t *len_out) {
    if (!mounted) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > 8 * 1024 * 1024) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = 0;
    if (len_out) *len_out = got;
    return buf;
}

/* Written to a temporary file, then swapped in, so a power cut never leaves
 * a half-written file. */
bool storage_write_atomic(const char *path, const void *data, size_t len) {
    if (!mounted) return false;
    char tmp[320];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fflush(f) == 0) && ok;
    fclose(f);
    if (!ok) { remove(tmp); return false; }
    remove(path);
    return rename(tmp, path) == 0;
}
