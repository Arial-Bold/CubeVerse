/* web.c - see web.h. The reading of an answer (Content-Length or chunked)
 * is CubeVerse's https.c, extended to keep the connection open. */
#include "web.h"
#include "version.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    Conn *c;
    char buf[4096];
    int pos, len;
    bool chunked;
    long chunk_left;
    long remaining;
    bool done;
    bool broken;
} Rd;

static int fill(Rd *s) {
    if (s->pos < s->len) return s->len - s->pos;
    int r = conn_read(s->c, s->buf, sizeof(s->buf));
    if (r <= 0) { s->broken = true; return r; }
    s->pos = 0;
    s->len = r;
    return r;
}

static int read_line(Rd *s, char *out, int cap) {
    int n = 0;
    for (;;) {
        if (fill(s) <= 0) return n ? n : -1;
        char ch = s->buf[s->pos++];
        if (ch == '\n') break;
        if (ch != '\r' && n + 1 < cap) out[n++] = ch;
    }
    out[n] = 0;
    return n;
}

static int read_body(Rd *s, char *out, int cap) {
    if (s->done) return 0;
    if (s->chunked) {
        if (s->chunk_left <= 0) {
            char line[64];
            if (s->chunk_left == 0 && read_line(s, line, sizeof(line)) < 0) return -1;
            if (read_line(s, line, sizeof(line)) < 0) return -1;
            s->chunk_left = strtol(line, NULL, 16);
            if (s->chunk_left <= 0) {
                /* trailers, then the blank line */
                while (read_line(s, line, sizeof(line)) > 0) {}
                s->done = true;
                return 0;
            }
        }
        if (cap > s->chunk_left) cap = (int)s->chunk_left;
    } else if (s->remaining >= 0) {
        if (s->remaining == 0) { s->done = true; return 0; }
        if (cap > s->remaining) cap = (int)s->remaining;
    }
    int got = fill(s);
    if (got < 0) return -1;
    if (got == 0) {
        s->done = true;
        return (s->chunked || s->remaining > 0) ? -1 : 0;
    }
    if (got > cap) got = cap;
    memcpy(out, s->buf + s->pos, (size_t)got);
    s->pos += got;
    if (s->chunked) s->chunk_left -= got;
    else if (s->remaining > 0) s->remaining -= got;
    return got;
}

static bool split_url(const char *url, char *host, size_t hcap, unsigned short *port, bool *tls, char *path, size_t pcap) {
    if (!strncasecmp(url, "https://", 8)) { *tls = true; url += 8; }
    else if (!strncasecmp(url, "http://", 7)) { *tls = false; url += 7; }
    else return false;
    *port = *tls ? 443 : 80;
    size_t n = strcspn(url, ":/?");
    if (n == 0 || n >= hcap) return false;
    memcpy(host, url, n);
    host[n] = 0;
    url += n;
    if (*url == ':') { *port = (unsigned short)atoi(url + 1); url += strcspn(url, "/?"); }
    snprintf(path, pcap, "%s%s", *url == '/' ? "" : "/", url);
    return true;
}

void web_init(WebClient *wc) {
    memset(wc, 0, sizeof(*wc));
    for (int i = 0; i < WEB_SLOTS; i++) wc->slot[i].c.fd = -1;
}

static void slot_close(WebSlot *s) {
    if (s->open) conn_close(&s->c);
    s->open = false;
    s->used = 0;
    s->c.fd = -1;
}

void web_close_all(WebClient *wc) {
    for (int i = 0; i < WEB_SLOTS; i++) slot_close(&wc->slot[i]);
}

void web_resp_free(WebResp *r) {
    free(r->body);
    r->body = NULL;
    r->len = 0;
}

static WebSlot *slot_for(WebClient *wc, const char *host, unsigned short port, bool tls) {
    WebSlot *oldest = &wc->slot[0];
    for (int i = 0; i < WEB_SLOTS; i++) {
        WebSlot *s = &wc->slot[i];
        if (s->open && s->port == port && s->tls == tls && !strcasecmp(s->host, host)) return s;
        if (!s->open) oldest = s;
        else if (oldest->open && s->last_use < oldest->last_use) oldest = s;
    }
    slot_close(oldest);
    snprintf(oldest->host, sizeof(oldest->host), "%s", host);
    oldest->port = port;
    oldest->tls = tls;
    return oldest;
}

/* One try on the slot's connection. 1 = answered, 0 = the kept-open
 * connection had been closed by the server (try again on a new one), -1 = failed. */
static int attempt(WebClient *wc, WebSlot *sl, const char *head, size_t head_len, const void *body, size_t body_len,
                   size_t max, WebResp *r, volatile int *cancel, char *err, size_t errlen) {
    bool reused = sl->open;
    if (!sl->open) {
        if (!conn_open(&sl->c, sl->host, sl->port, sl->tls, cancel, NULL, err, errlen)) return -1;
        sl->open = true;
        sl->used = 0;
    }
    if (!conn_write_all(&sl->c, head, head_len) || (body_len && !conn_write_all(&sl->c, body, body_len))) {
        slot_close(sl);
        if (reused) return 0;
        snprintf(err, errlen, "Couldn't send the request to %s.", sl->host);
        return -1;
    }
    sl->used++;
    Rd rd;
    memset(&rd, 0, sizeof(rd));
    rd.c = &sl->c;
    char line[1024];
    if (read_line(&rd, line, sizeof(line)) < 0) {
        slot_close(sl);
        if (reused) return 0;
        snprintf(err, errlen, "%s didn't answer.", sl->host);
        return -1;
    }
    memset(r, 0, sizeof(*r));
    const char *sp = strchr(line, ' ');
    r->status = sp ? atoi(sp + 1) : 0;
    rd.remaining = -1;
    rd.chunk_left = -1;
    bool keep = strncmp(line, "HTTP/1.0", 8) != 0;
    bool has_length = false;
    for (;;) {
        int l = read_line(&rd, line, sizeof(line));
        if (l < 0) { slot_close(sl); snprintf(err, errlen, "The answer from %s was cut off.", sl->host); return -1; }
        if (l == 0) break;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = 0;
        char *v = colon + 1;
        while (*v == ' ') v++;
        if (!strcasecmp(line, "content-length")) { rd.remaining = atol(v); has_length = true; }
        else if (!strcasecmp(line, "transfer-encoding") && strstr(v, "chunked")) rd.chunked = true;
        else if (!strcasecmp(line, "location")) snprintf(r->location, sizeof(r->location), "%s", v);
        else if (!strcasecmp(line, "content-type")) snprintf(r->content_type, sizeof(r->content_type), "%s", v);
        else if (!strcasecmp(line, "connection")) { if (strcasestr(v, "close")) keep = false; else if (strcasestr(v, "keep-alive")) keep = true; }
        else if (!strcasecmp(line, "set-cookie") && wc->on_cookie) wc->on_cookie(wc->cookie_ctx, sl->host, v);
    }
    if (rd.chunked) rd.remaining = -1;
    else if (!has_length && (r->status == 204 || r->status == 304 || (r->status >= 100 && r->status < 200))) rd.remaining = 0;
    else if (!has_length) keep = false;   /* read until closed */
    size_t cap = 8192, n = 0;
    if (max < cap) cap = max;
    char *b = malloc(cap + 1);
    if (!b) { slot_close(sl); snprintf(err, errlen, "Out of memory."); return -1; }
    bool cut = false;
    for (;;) {
        if (cancel && *cancel) { cut = true; break; }
        if (n == cap) {
            if (cap >= max) {   /* too big: read the rest away so the connection stays usable */
                char sink[1024];
                int k;
                while ((k = read_body(&rd, sink, sizeof(sink))) > 0) {}
                if (k < 0) cut = true;
                break;
            }
            size_t nc = cap * 2 > max ? max : cap * 2;
            char *nb = realloc(b, nc + 1);
            if (!nb) { cut = true; break; }
            b = nb;
            cap = nc;
        }
        int k = read_body(&rd, b + n, (int)(cap - n));
        if (k < 0) { cut = true; break; }
        if (k == 0) break;
        n += (size_t)k;
    }
    b[n] = 0;
    r->body = b;
    r->len = n;
    sl->last_use = ++wc->tick;
    if (cut || !keep || rd.broken || !rd.done) slot_close(sl);
    if (cut && cancel && *cancel) { snprintf(err, errlen, "Stopped."); web_resp_free(r); return -1; }
    return 1;
}

bool web_request(WebClient *wc, const char *method, const char *url, const char *headers, const void *body,
                 size_t body_len, size_t max, WebResp *r, volatile int *cancel, char *err, size_t errlen) {
    char host[128], path[2048];
    unsigned short port;
    bool tls;
    memset(r, 0, sizeof(*r));
    if (!split_url(url, host, sizeof(host), &port, &tls, path, sizeof(path))) {
        snprintf(err, errlen, "Bad address.");
        return false;
    }
    char head[4096 + 2048];
    int hl = snprintf(head, sizeof(head),
                      "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: CubeVerse/%s (Nintendo GameCube)\r\n"
                      "Accept-Encoding: identity\r\nConnection: keep-alive\r\n%s",
                      method, path, host, CV_VERSION, headers ? headers : "");
    if (body || !strcmp(method, "POST"))
        hl += snprintf(head + hl, sizeof(head) - hl, "Content-Length: %u\r\n", (unsigned)body_len);
    hl += snprintf(head + hl, sizeof(head) - hl, "\r\n");
    if (hl >= (int)sizeof(head)) { snprintf(err, errlen, "The request was too long."); return false; }
    for (int tries = 0; tries < 2; tries++) {
        WebSlot *sl = slot_for(wc, host, port, tls);
        int k = attempt(wc, sl, head, (size_t)hl, body, body_len, max, r, cancel, err, errlen);
        if (k == 1) return true;
        if (k < 0) return false;
    }
    snprintf(err, errlen, "%s closed the connection.", host);
    return false;
}

size_t web_urlencode(char *out, size_t cap, const char *s) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = strlen(out);
    for (; *s && n + 4 < cap; s++) {
        unsigned char ch = (unsigned char)*s;
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') out[n++] = (char)ch;
        else if (ch == ' ') out[n++] = '+';
        else { out[n++] = '%'; out[n++] = hex[ch >> 4]; out[n++] = hex[ch & 15]; }
    }
    out[n] = 0;
    return n;
}

size_t web_base64(char *out, const unsigned char *in, size_t n) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)in[i] << 16 | (i + 1 < n ? (unsigned)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = t[(v >> 18) & 63];
        out[o++] = t[(v >> 12) & 63];
        out[o++] = i + 1 < n ? t[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}
