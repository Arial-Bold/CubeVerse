/* applog.h - a small log file (sd:/cubeverse/cubeverse.log) for problems
 * worth knowing about later. Also serves the music code's http_log_note. */
#pragma once
void app_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* The same for the music thread: the line is kept in memory and written by
 * app_log_flush() on the main thread, so the music never waits for the
 * SD card. (A few lines can wait; more are dropped.) */
void app_log_later(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void app_log_flush(void);
#define http_log_note app_log_later
