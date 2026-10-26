/* https.h - a small HTTPS client for the update check: GET a page into
 * memory or a file, following redirects (GitHub's downloads redirect to
 * another server), with Content-Length or chunked bodies. GameCube only. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { volatile uint32_t done, total; } HttpsProgress;   /* total 0 = unknown */

/* Returns the final HTTP status (body in *body, malloc'd and NUL-terminated,
 * at most max bytes), or -1 with err filled in. */
int https_get(const char *url, const char *accept, char **body, size_t *len, size_t max, volatile int *cancel,
              char *err, size_t errlen);

/* Saves the page at url to path (written as path.part, then renamed). */
bool https_download(const char *url, const char *path, HttpsProgress *pg, volatile int *cancel, char *err,
                    size_t errlen);
