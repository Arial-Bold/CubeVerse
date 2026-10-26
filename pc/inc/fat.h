#pragma once
#include <stdbool.h>
typedef struct { int dummy; } DISC_INTERFACE;
static inline bool fatMountSimple(const char *n, const DISC_INTERFACE *d) { (void)n; (void)d; return true; }
