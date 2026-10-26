/* storage.h - the SD card (SD2SP2 or SD Gecko), CubeVerse's data folder
 * (sd:/cubeverse: settings, favourites, thumbnail cache) and file helpers.
 * Photos themselves live in sd:/DCIM. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define DATA_DIR   "sd:/cubeverse"

bool storage_init(void);
bool storage_ok(void);
const char *storage_device_name(void);
const char *storage_dir(void);
void storage_path(char *out, size_t cap, const char *name);   /* DATA_DIR "/" name */

char *storage_read(const char *path, size_t *len_out);   /* malloc'd, NUL-terminated; up to 8 MB */
bool storage_write_atomic(const char *path, const void *data, size_t len);
bool storage_is_dir(const char *path);
bool storage_is_file(const char *path);
void storage_mkdirs(const char *path);   /* creates every missing folder along the path */
