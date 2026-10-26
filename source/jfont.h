/* jfont.h - the Japanese characters CubeVerse can show (font_jp.h): which
 * ones there are, and their texture pages, unpacked the first time
 * Japanese text is drawn (about 1.3 MB). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The character's number in the set, or -1 if it isn't one we have. */
int jfont_index(unsigned cp);
/* If s[i..] starts a Japanese character we have (3 bytes of UTF-8), its
 * number in the set; otherwise -1. */
int jfont_at(const char *s, int i, int len);

/* The unpacked pages (I4, 1024 texels wide), or NULL if they couldn't be
 * unpacked (not enough memory). */
const uint8_t *jfont_page(int page, int *height);
int jfont_pages(void);
/* where glyph n is: its page and the top-left of its em box, in texels */
void jfont_where(int n, int *page, int *x, int *y);
int jfont_px(void);            /* the em box, in texels */
float jfont_ascent(void);      /* the baseline, as a part of the em box from its top */
