/* applog.c - appends lines to the log; starts afresh when it passes 64 KB. */
#include "applog.h"
#include "storage.h"
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

void app_log(const char *fmt, ...) {
    if (!storage_ok()) return;
    char path[300];
    storage_path(path, sizeof(path), "cubeverse.log");
    struct stat st;
    const char *mode = (stat(path, &st) == 0 && st.st_size > 64 * 1024) ? "wb" : "ab";
    FILE *f = fopen(path, mode);
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* ---- lines from other threads, written later by the main thread */
#define LATER_LINES 8
static char later[LATER_LINES][240];
static volatile unsigned later_in, later_out;   /* one writer (the music thread), one reader */

void app_log_later(const char *fmt, ...) {
    if (later_in - later_out >= LATER_LINES) return;   /* full: dropped */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(later[later_in % LATER_LINES], sizeof(later[0]), fmt, ap);
    va_end(ap);
    __sync_synchronize();   /* the text is in place before it's counted */
    later_in++;
}

void app_log_flush(void) {
    while (later_out != later_in) {
        app_log("%s", later[later_out % LATER_LINES]);
        __sync_synchronize();
        later_out++;
    }
}
