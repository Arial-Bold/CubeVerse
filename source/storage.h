/* storage.h - the SD card (SD2SP2 or SD Gecko), CubeVerse's data folder
 * (settings, sign-in, picture cache) and file helpers.
 *
 * The data folder is the folder CubeVerse's .dol is in (usually
 * sd:/apps/cubeverse); a .dol at the root of the card uses sd:/cubeverse,
 * as older versions did. Settings left in sd:/cubeverse by an older version
 * are moved over the first time. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define LEGACY_DATA_DIR "sd:/cubeverse"   /* where older versions kept everything */
#define APP_DIR "sd:/apps/cubeverse"     /* where launchers install CubeVerse */
#define RESTART_NOTE "sd:/cubeverse-restart.txt"   /* the .dol, for a start without arguments (update.c) */

void storage_set_program(const char *argv0);   /* before storage_init: argv[0] from the loader */
bool storage_init(void);
const char *storage_program_file(void);         /* the .dol we were started from, or NULL if unknown */
bool storage_ok(void);
const char *storage_device_name(void);
const char *storage_dir(void);
void storage_path(char *out, size_t cap, const char *name);   /* the data folder "/" name */

char *storage_read(const char *path, size_t *len_out);   /* malloc'd, NUL-terminated; up to 8 MB */
bool storage_write_atomic(const char *path, const void *data, size_t len);
bool storage_is_dir(const char *path);
bool storage_is_file(const char *path);
void storage_mkdirs(const char *path);   /* creates every missing folder along the path */
