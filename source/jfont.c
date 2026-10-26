/* jfont.c - see jfont.h. */
#include "jfont.h"
#include "font_jp.h"
#include "inflate.h"
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#ifdef GEKKO
#include <gccore.h>
#endif

int jfont_index(unsigned cp) {
    if (cp < JP_CODES[0] || cp > JP_CODES[JP_COUNT - 1]) return -1;
    int lo = 0, hi = JP_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (JP_CODES[mid] == cp) return mid;
        if (JP_CODES[mid] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

int jfont_at(const char *s, int i, int len) {
    const unsigned char *p = (const unsigned char *)s + i;
    if (i + 2 >= len || (p[0] & 0xF0) != 0xE0 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return -1;
    unsigned cp = ((unsigned)(p[0] & 0x0F) << 12) | ((unsigned)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return jfont_index(cp);
}

static uint8_t *mem;
static bool tried;
typedef struct { const uint8_t *p; size_t left; } Src;
typedef struct { uint8_t *p; size_t left; } Dst;
static int rd(void *u, unsigned char *buf, int max) {
    Src *s = u;
    size_t n = s->left < (size_t)max ? s->left : (size_t)max;
    memcpy(buf, s->p, n);
    s->p += n;
    s->left -= n;
    return (int)n;
}
static int wr(void *u, const unsigned char *d, size_t len) {
    Dst *o = u;
    if (len > o->left) return -1;
    memcpy(o->p, d, len);
    o->p += len;
    o->left -= len;
    return 0;
}

static bool unpack(void) {
    if (tried) return mem != NULL;
    tried = true;
    mem = memalign(32, JP_RAW_BYTES);
    if (!mem) return false;
    Src s = { JP_DATA, JP_DATA_BYTES };
    Dst d = { mem, JP_RAW_BYTES };
    if (inflate_stream(rd, &s, wr, &d, NULL, NULL) != 0 || d.left != 0) {
        free(mem);
        mem = NULL;
        return false;
    }
#ifdef GEKKO
    DCFlushRange(mem, JP_RAW_BYTES);
#endif
    return true;
}

const uint8_t *jfont_page(int page, int *height) {
    if (page < 0 || page >= JP_PAGES || !unpack()) return NULL;
    size_t off = 0;
    for (int i = 0; i < page; i++) off += (size_t)JP_PAGE_W * JP_PAGE_H[i] / 2;
    if (height) *height = JP_PAGE_H[page];
    return mem + off;
}

int jfont_pages(void) { return JP_PAGES; }

void jfont_where(int n, int *page, int *x, int *y) {
    *page = n / JP_PER_PAGE;
    int k = n % JP_PER_PAGE;
    *x = (k % JP_PER_ROW) * JP_CELL;
    *y = (k / JP_PER_ROW) * JP_CELL;
}

int jfont_px(void) { return JP_PX; }
float jfont_ascent(void) { return JP_ASCENT; }
