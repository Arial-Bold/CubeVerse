/* update.h - checks GitHub for a newer CubeVerse and installs it.
 *
 * Releases come from github.com/Arial-Bold/CubeVerse. The latest release's
 * tag (e.g. "v1.1") is compared with the version (version.h); its .zip
 * asset holds a cubeverse folder: default.dol, opening.bnr, README.txt and
 * cacert.pem. Installing replaces the running .dol (the old one stays as
 * <name>.old only until the new one has started), the banner and
 * cacert.pem; settings and the sign-in are never touched.
 *
 * The network work runs on a thread of its own; everything else here is
 * called from the main thread. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UPDATE_REPO "Arial-Bold/CubeVerse"

typedef enum {
    UPD_NONE,          /* not checked yet */
    UPD_CHECKING,
    UPD_LATEST,        /* this is the newest version */
    UPD_AVAILABLE,     /* a newer version can be installed */
    UPD_CHECK_FAILED,
    UPD_INSTALLING,    /* downloading, unpacking, replacing */
    UPD_INSTALLED,     /* done: restart to use it */
    UPD_FAILED         /* install failed; nothing was replaced */
} UpdateState;

typedef enum { UPD_STEP_NETWORK, UPD_STEP_DOWNLOAD, UPD_STEP_UNPACK, UPD_STEP_REPLACE } UpdateStep;

void update_set_program(const char *argv0);   /* the .dol we were started from, if the loader said */

UpdateState update_state(void);
UpdateStep  update_step(void);
float       update_progress(void);            /* 0..1 within the step, or -1 if unknown */
const char *update_latest_version(void);      /* e.g. "1.1" (without the v) */
const char *update_notes(void);               /* release notes, plain text */
const char *update_error(void);
bool        update_was_quiet(void);           /* the last check was the automatic one */

void update_request_check(bool quiet);        /* quiet: the automatic check at start-up */
void update_request_install(void);
void update_cancel(void);
void update_poll(void);                       /* once per frame */
bool update_restart(char *err, size_t errlen);   /* starts the newly installed .dol */
/* At start: deletes the previous version left by an update (<name>.old), now
 * that this one has started, so old versions don't pile up on the card. */
void update_remove_old(void);
