/* strutil.h - string helpers the GameCube's C library doesn't have. */
#pragma once
#include <ctype.h>
#include <stddef.h>

/* Finds needle in hay, ignoring upper/lower case (like strcasestr). */
static inline const char *str_find_nocase(const char *hay, const char *needle) {
    if (!*needle) return hay;
    for (; *hay; hay++) {
        size_t i = 0;
        while (needle[i] && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (!needle[i]) return hay;
    }
    return NULL;
}
