/* imgcache.c - see imgcache.h.
 *
 * Each picture, once fetched, decoded and shrunk to the size it's shown at,
 * is also saved on the SD card (sd:/cubeverse/cache) exactly as the GPU
 * wants it, so next time it's just read back: no network, no decoding.
 * Faces and icons are fetched again after a few days (people change their
 * Mii); drawings and screenshots never change. The cache is kept under
 * 48 MB by clearing out the oldest files when CubeVerse starts. */
#include "imgcache.h"
#include "web.h"
#include "net.h"
#include "thr.h"
#ifdef GEKKO
#include "netio.h"
#endif
#include "ui.h"
#include "pixels.h"
#include "applog.h"
#include "stb_image.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "storage.h"

#define NIMG 72
#define MAX_BYTES (4u * 1024 * 1024)

enum { E_FREE, E_WANTED, E_LOADING, E_DECODED, E_READY, E_FAILED };

typedef struct {
    char url[200];
    ImgKind kind;
    volatile int state;
    /* decoded, waiting for the main thread */
    void *data;
    int w, h, tw, th;
    GfxImage img;
    float last_used;
} Entry;

static Entry ents[NIMG];
static thr_mutex lock;
static thr_t thread;
static WebClient wc;
static volatile int quit;
static bool started;

static uint16_t rgb5a3(int r, int g, int b, int a) {
    if (a >= 248) return (uint16_t)(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    return (uint16_t)(((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
}

/* Box-filtered shrink of rgba (sw x sh) to dw x dh, into a tiled RGB5A3 texture. */
static void *to_texture(const unsigned char *rgba, int sw, int sh, int dw, int dh, int *tw, int *th) {
    *tw = tex_round(dw);
    *th = tex_round(dh);
    uint16_t *t = memalign(32, (size_t)*tw * *th * 2);
    if (!t) return NULL;
    memset(t, 0, (size_t)*tw * *th * 2);
    for (int y = 0; y < dh; y++) {
        int y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = x * sw / dw, x1 = (x + 1) * sw / dw;
            if (x1 <= x0) x1 = x0 + 1;
            unsigned r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const unsigned char *p = rgba + ((size_t)yy * sw + xx) * 4;
                    /* average colour weighted by coverage, so edges don't darken */
                    r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; a += p[3];
                    n++;
                }
            int R = a ? (int)(r / a) : 0, G = a ? (int)(g / a) : 0, B = a ? (int)(b / a) : 0, A = (int)(a / n);
            t[tex_index(*tw, x, y)] = rgb5a3(R, G, B, A);
        }
    }
    return t;
}

static void target_size(ImgKind k, int sw, int sh, int *dw, int *dh) {
    int mw = k == IMG_SMALL ? 96 : k == IMG_PAINTING ? 320 : 400;
    int mh = k == IMG_SMALL ? 96 : k == IMG_PAINTING ? 120 : 240;
    float s = 1;
    if (sw > mw) s = (float)mw / sw;
    if (sh * s > mh) s = (float)mh / sh;
    *dw = (int)(sw * s + 0.5f);
    *dh = (int)(sh * s + 0.5f);
    if (*dw < 1) *dw = 1;
    if (*dh < 1) *dh = 1;
}

/* ---- the SD card cache ---- */
#define CACHE_DIR DATA_DIR "/cache"
#define CACHE_MAX (48u * 1024 * 1024)
#define CACHE_KEEP (36u * 1024 * 1024)
#define SMALL_DAYS 3
typedef struct { char magic[4]; uint16_t w, h, tw, th; uint32_t saved; } CacheHead;   /* 16 bytes, then the texture */

static void cache_path(const Entry *e, char *out, size_t cap) {
    uint64_t hsh = 1469598103934665603ull;   /* FNV-1a of the address and the size it's shown at */
    for (const char *p = e->url; *p; p++) { hsh ^= (uint8_t)*p; hsh *= 1099511628211ull; }
    hsh ^= (uint64_t)e->kind + 1; hsh *= 1099511628211ull;
    snprintf(out, cap, "%s/%02x/%014llx.tex", CACHE_DIR, (unsigned)(hsh >> 56), (unsigned long long)(hsh & 0xFFFFFFFFFFFFFFull));
}

static bool cache_read(Entry *e) {
    char path[160];
    cache_path(e, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    CacheHead h;
    bool ok = fread(&h, 1, sizeof(h), f) == sizeof(h) && !memcmp(h.magic, "CVT1", 4) && h.w && h.h && h.tw >= h.w &&
              h.th >= h.h && h.tw <= 1024 && h.th <= 1024 && !(h.tw & 3) && !(h.th & 3);
    if (ok && e->kind == IMG_SMALL) {
        time_t now = time(NULL);
        if (now > (time_t)h.saved + SMALL_DAYS * 86400) ok = false;   /* stale: fetch it again */
    }
    void *t = NULL;
    if (ok) {
        size_t n = (size_t)h.tw * h.th * 2;
        t = memalign(32, n);
        ok = t && fread(t, 1, n, f) == n;
    }
    fclose(f);
    if (!ok) { free(t); return false; }
    e->data = t;
    e->w = h.w; e->h = h.h; e->tw = h.tw; e->th = h.th;
    e->state = E_DECODED;
    return true;
}

static void cache_write(const Entry *e, const void *tex) {
    if (!storage_ok()) return;
    char path[160];
    cache_path(e, path, sizeof(path));
    char dir[160];
    snprintf(dir, sizeof(dir), "%s", path);
    char *sl = strrchr(dir, '/');
    if (sl) *sl = 0;
    storage_mkdirs(dir);
    size_t n = (size_t)e->tw * e->th * 2;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    CacheHead h;
    memcpy(h.magic, "CVT1", 4);
    h.w = (uint16_t)e->w; h.h = (uint16_t)e->h; h.tw = (uint16_t)e->tw; h.th = (uint16_t)e->th;
    h.saved = (uint32_t)time(NULL);
    bool ok = fwrite(&h, 1, sizeof(h), f) == sizeof(h) && fwrite(tex, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    if (!ok) remove(path);   /* never leave half a file */
}

/* Keeps the cache under CACHE_MAX: if it's bigger, the oldest files go
 * until it's under CACHE_KEEP. */
typedef struct { char name[24]; uint8_t dir; uint32_t size; time_t when; } CFile;
static void cache_trim(void) {
    if (!storage_ok() || !storage_is_dir(CACHE_DIR)) return;
    size_t cap = 4096, n = 0;
    CFile *list = malloc(cap * sizeof(CFile));
    if (!list) return;
    uint64_t total = 0;
    for (int d = 0; d < 256 && !quit; d++) {
        char dir[160];
        snprintf(dir, sizeof(dir), "%s/%02x", CACHE_DIR, d);
        DIR *dp = opendir(dir);
        if (!dp) continue;
        struct dirent *de;
        while ((de = readdir(dp))) {
            if (de->d_name[0] == '.' || strlen(de->d_name) >= sizeof(list[0].name)) continue;
            char p[200];
            snprintf(p, sizeof(p), "%s/%s", dir, de->d_name);
            struct stat st;
            if (stat(p, &st) != 0) continue;
            total += (uint64_t)st.st_size;
            if (n == cap) {
                CFile *m = realloc(list, cap * 2 * sizeof(CFile));
                if (!m) break;
                list = m;
                cap *= 2;
            }
            snprintf(list[n].name, sizeof(list[n].name), "%s", de->d_name);
            list[n].dir = (uint8_t)d;
            list[n].size = (uint32_t)st.st_size;
            list[n].when = st.st_mtime;
            n++;
        }
        closedir(dp);
    }
    if (total > CACHE_MAX) {
        /* oldest first */
        for (size_t i = 1; i < n; i++) {
            CFile c = list[i];
            size_t j = i;
            while (j > 0 && list[j - 1].when > c.when) { list[j] = list[j - 1]; j--; }
            list[j] = c;
        }
        int removed = 0;
        for (size_t i = 0; i < n && total > CACHE_KEEP; i++) {
            char p[200];
            snprintf(p, sizeof(p), "%s/%02x/%s", CACHE_DIR, list[i].dir, list[i].name);
            if (remove(p) == 0) { total -= list[i].size; removed++; }
        }
        app_log_later("Picture cache trimmed: %d files removed", removed);
    }
    free(list);
}

static void load(Entry *e) {
    char err[160];
    if (cache_read(e)) return;   /* saved on the SD card last time */
    if (net_state() != NET_UP) { e->state = net_state() == NET_FAILED ? E_FAILED : E_WANTED; return; }
    static bool tls;
    if (!tls) {
#ifdef GEKKO
        if (!netio_time_synced()) netio_sync_time();
#endif
        if (!tls_global_init(err, sizeof(err))) { e->state = E_FAILED; return; }
        tls = true;
    }
    WebResp r;
    if (!web_request(&wc, "GET", e->url, "Accept: image/*\r\n", NULL, 0, MAX_BYTES, &r, &quit, err, sizeof(err)) ||
        r.status != 200 || !r.body) {
        if (r.body) web_resp_free(&r);
        e->state = E_FAILED;
        return;
    }
    int w, h, ch;
    unsigned char *px = stbi_load_from_memory((const unsigned char *)r.body, (int)r.len, &w, &h, &ch, 4);
    web_resp_free(&r);
    if (!px) { e->state = E_FAILED; return; }
    int dw, dh, tw, th;
    target_size(e->kind, w, h, &dw, &dh);
    void *t = to_texture(px, w, h, dw, dh, &tw, &th);
    stbi_image_free(px);
    if (!t) { e->state = E_FAILED; return; }
    e->data = t;
    e->w = dw; e->h = dh; e->tw = tw; e->th = th;
    cache_write(e, t);
    e->state = E_DECODED;
}

static void *img_main(void *arg) {
    (void)arg;
    cache_trim();
    while (!quit) {
        Entry *pick = NULL;
        thr_lock(&lock);
        float newest = -1;
        for (int i = 0; i < NIMG; i++)   /* the most recently wanted first */
            if (ents[i].state == E_WANTED && ents[i].last_used > newest) { newest = ents[i].last_used; pick = &ents[i]; }
        if (pick) pick->state = E_LOADING;
        thr_unlock(&lock);
        if (!pick) { thr_sleep_ms(20); continue; }
        load(pick);
        if (pick->state == E_WANTED) thr_sleep_ms(200);   /* network not up yet */
    }
    return NULL;
}

void img_init(void) {
    if (started) return;
    started = true;
    thr_mutex_init(&lock);
    web_init(&wc);
    thr_create(&thread, img_main, NULL, 64 * 1024, 58);
}

static Entry *find(const char *url) {
    for (int i = 0; i < NIMG; i++) if (ents[i].state != E_FREE && !strcmp(ents[i].url, url)) return &ents[i];
    return NULL;
}

GfxImage *img_get(const char *url, ImgKind kind) {
    if (!url || !url[0]) return NULL;
    Entry *e = find(url);
    if (e) {
        e->last_used = g_time;
        return e->state == E_READY ? &e->img : NULL;
    }
    /* a free slot, or the least recently used finished one */
    thr_lock(&lock);
    Entry *slot = NULL;
    for (int i = 0; i < NIMG && !slot; i++) if (ents[i].state == E_FREE) slot = &ents[i];
    if (!slot) {
        for (int i = 0; i < NIMG; i++) {
            Entry *c = &ents[i];
            if ((c->state == E_READY || c->state == E_FAILED || c->state == E_WANTED) && c->last_used < g_time - 0.5f &&
                (!slot || c->last_used < slot->last_used))
                slot = c;
        }
    }
    if (slot) {
        if (slot->state == E_READY) gfx_image_free(&slot->img);
        snprintf(slot->url, sizeof(slot->url), "%s", url);
        slot->kind = kind;
        slot->data = NULL;
        slot->last_used = g_time;
        slot->state = E_WANTED;
    }
    thr_unlock(&lock);
    return NULL;
}

bool img_failed(const char *url) {
    Entry *e = url && url[0] ? find(url) : NULL;
    return e && e->state == E_FAILED;
}

void img_tick(void) {
    for (int i = 0; i < NIMG; i++) {
        Entry *e = &ents[i];
        if (e->state != E_DECODED) continue;
        if (gfx_image_init(&e->img, e->data, e->w, e->h, e->tw, e->th, GFX_RGB5A3)) e->state = E_READY;
        else e->state = E_FAILED;
        e->data = NULL;
    }
}

static void placeholder(const char *url, float x, float y, float w, float h, float alpha) {
    float p = 0.5f + 0.5f * sinf(g_time * 3 + x * 0.01f);
    bool bad = img_failed(url);
    gfx_rect(x, y, w, h, bad ? rgba(222, 226, 236, (u8)(255 * alpha)) : rgba(214, 220, 232, (u8)((200 + 40 * p) * alpha)));
}

void img_draw_cover(const char *url, ImgKind kind, float x, float y, float w, float h, float alpha) {
    GfxImage *img = img_get(url, kind);
    if (!img) { placeholder(url, x, y, w, h, alpha); return; }
    float tw = img->w, th = img->h, u0 = 0, v0 = 0, u1 = tw, v1 = th;
    float cell = w / h, pic = tw / th;
    if (pic > cell) { float cw = th * cell; u0 = (tw - cw) / 2; u1 = u0 + cw; }
    else { float chh = tw / cell; v0 = (th - chh) / 2; v1 = v0 + chh; }
    gfx_image_draw_part(img, x, y, w, h, u0, v0, u1, v1, rgba(255, 255, 255, (u8)(255 * alpha)));
}

void img_draw_fit(const char *url, ImgKind kind, float x, float y, float w, float h, float alpha) {
    GfxImage *img = img_get(url, kind);
    if (!img) { placeholder(url, x, y, w, h, alpha); return; }
    float s = w / img->w;
    if (img->h * s > h) s = h / img->h;
    float dw = img->w * s, dh = img->h * s;
    gfx_image_draw(img, x + (w - dw) / 2, y + (h - dh) / 2, dw, dh, rgba(255, 255, 255, (u8)(255 * alpha)));
}
