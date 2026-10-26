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

static bool mounted;
static const char *device = "none";

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
    mkdir(DATA_DIR, 0777);
    storage_mkdirs(DATA_DIR "/cache");
    return true;
}

bool storage_ok(void) { return mounted; }
const char *storage_device_name(void) { return device; }
const char *storage_dir(void) { return DATA_DIR; }

void storage_path(char *out, size_t cap, const char *name) { snprintf(out, cap, "%s/%s", DATA_DIR, name); }

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
