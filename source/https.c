/* https.c - see https.h. */
#include "https.h"
#include "conn.h"
#include "version.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define URL_CAP 2048
#define HEAD_CAP 16384

typedef struct {
    Conn c;
    char buf[4096];
    int pos, len;          /* unread bytes in buf */
    bool chunked;
    long chunk_left;       /* bytes left in this chunk (chunked), -1 = need a size line */
    long remaining;        /* Content-Length left, -1 = until closed */
    bool done;
} Stream;

static bool split_url(const char *url, char *host, size_t hcap, unsigned short *port, char *path, size_t pcap) {
    bool tls;
    if (!strncasecmp(url, "https://", 8)) { tls = true; url += 8; }
    else if (!strncasecmp(url, "http://", 7)) { tls = false; url += 7; }
    else return false;
    *port = tls ? 443 : 80;
    size_t n = strcspn(url, ":/?");
    if (n == 0 || n >= hcap) return false;
    memcpy(host, url, n);
    host[n] = 0;
    url += n;
    if (*url == ':') { *port = (unsigned short)atoi(url + 1); url += strcspn(url, "/?"); }
    snprintf(path, pcap, "%s%s", *url == '/' ? "" : "/", url);
    return tls;
}

static int fill(Stream *s) {
    if (s->pos < s->len) return s->len - s->pos;
    int r = conn_read(&s->c, s->buf, sizeof(s->buf));
    if (r <= 0) return r;
    s->pos = 0;
    s->len = r;
    return r;
}

static int read_line(Stream *s, char *out, int cap) {
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

/* Reads up to cap bytes of the body. 0 at the end, <0 on error. */
static int read_body(Stream *s, char *out, int cap) {
    if (s->done) return 0;
    if (s->chunked) {
        if (s->chunk_left <= 0) {
            char line[64];
            if (s->chunk_left == 0 && read_line(s, line, sizeof(line)) < 0) return -1;   /* the CRLF after a chunk */
            if (read_line(s, line, sizeof(line)) < 0) return -1;
            s->chunk_left = strtol(line, NULL, 16);
            if (s->chunk_left <= 0) { s->done = true; return 0; }
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

/* Opens url and reads the headers; follows redirects. Returns the status or -1. */
static int open_stream(Stream *s, const char *start_url, const char *accept, volatile int *cancel, long *length,
                       char *err, size_t errlen) {
    static char url[URL_CAP];
    snprintf(url, sizeof(url), "%s", start_url);
    for (int hop = 0; hop < 8; hop++) {
        char host[256], path[URL_CAP];
        unsigned short port;
        memset(s, 0, sizeof(*s));
        s->c.fd = -1;
        if (strncasecmp(url, "http", 4)) { snprintf(err, errlen, "Bad address."); return -1; }
        bool tls = split_url(url, host, sizeof(host), &port, path, sizeof(path));
        if (!conn_open(&s->c, host, port, tls, cancel, NULL, err, errlen)) return -1;
        static char req[URL_CAP + 512];
        int n = snprintf(req, sizeof(req),
                         "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: CubeVerse/%s (Nintendo GameCube)\r\n"
                         "Accept: %s\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
                         path, host, CV_VERSION, accept ? accept : "*/*");
        if (!conn_write_all(&s->c, req, (size_t)n)) {
            snprintf(err, errlen, "Couldn't send the request to %s.", host);
            conn_close(&s->c);
            return -1;
        }
        char line[URL_CAP];
        if (read_line(s, line, sizeof(line)) < 0) {
            snprintf(err, errlen, "%s didn't answer.", host);
            conn_close(&s->c);
            return -1;
        }
        int status = 0;
        const char *sp = strchr(line, ' ');
        if (sp) status = atoi(sp + 1);
        s->remaining = -1;
        s->chunk_left = -1;
        char location[URL_CAP] = "";
        for (;;) {
            int l = read_line(s, line, sizeof(line));
            if (l < 0) { snprintf(err, errlen, "The answer from %s was cut off.", host); conn_close(&s->c); return -1; }
            if (l == 0) break;
            char *colon = strchr(line, ':');
            if (!colon) continue;
            *colon = 0;
            char *v = colon + 1;
            while (*v == ' ') v++;
            if (!strcasecmp(line, "content-length")) s->remaining = atol(v);
            else if (!strcasecmp(line, "transfer-encoding") && strstr(v, "chunked")) s->chunked = true;
            else if (!strcasecmp(line, "location")) snprintf(location, sizeof(location), "%s", v);
        }
        if (s->chunked) s->remaining = -1;
        if (status >= 300 && status < 400 && location[0]) {
            conn_close(&s->c);
            if (location[0] == '/') {   /* same server */
                char base[300];
                snprintf(base, sizeof(base), "%s://%s", tls ? "https" : "http", host);
                char tmp[URL_CAP];
                snprintf(tmp, sizeof(tmp), "%s%s", base, location);
                snprintf(url, sizeof(url), "%s", tmp);
            } else {
                snprintf(url, sizeof(url), "%s", location);
            }
            continue;
        }
        if (length) *length = s->remaining;
        return status;
    }
    snprintf(err, errlen, "Too many redirects.");
    return -1;
}

int https_get(const char *url, const char *accept, char **body, size_t *len, size_t max, volatile int *cancel,
              char *err, size_t errlen) {
    static Stream s;
    *body = NULL;
    if (len) *len = 0;
    long length = -1;
    int status = open_stream(&s, url, accept, cancel, &length, err, errlen);
    if (status < 0) return -1;
    size_t cap = 16384, n = 0;
    char *b = malloc(cap + 1);
    if (!b) { conn_close(&s.c); snprintf(err, errlen, "Out of memory."); return -1; }
    for (;;) {
        if (n == cap) {
            if (cap >= max) break;
            size_t nc = cap * 2 > max ? max : cap * 2;
            char *nb = realloc(b, nc + 1);
            if (!nb) break;
            b = nb;
            cap = nc;
        }
        int r = read_body(&s, b + n, (int)(cap - n));
        if (r <= 0) break;
        n += (size_t)r;
    }
    conn_close(&s.c);
    b[n] = 0;
    *body = b;
    if (len) *len = n;
    return status;
}

bool https_download(const char *url, const char *path, HttpsProgress *pg, volatile int *cancel, char *err,
                    size_t errlen) {
    static Stream s;
    long length = -1;
    int status = open_stream(&s, url, "application/octet-stream", cancel, &length, err, errlen);
    if (status < 0) return false;
    if (status != 200) {
        conn_close(&s.c);
        snprintf(err, errlen, "The download answered with error %d.", status);
        return false;
    }
    char part[400];
    snprintf(part, sizeof(part), "%s.part", path);
    FILE *f = fopen(part, "wb");
    if (!f) { conn_close(&s.c); snprintf(err, errlen, "Couldn't write to the SD card."); return false; }
    if (pg) { pg->done = 0; pg->total = length > 0 ? (uint32_t)length : 0; }
    static char buf[16384];
    bool ok = true;
    for (;;) {
        if (cancel && *cancel) { ok = false; snprintf(err, errlen, "Stopped."); break; }
        int r = read_body(&s, buf, sizeof(buf));
        if (r < 0) { ok = false; snprintf(err, errlen, "The download was cut off."); break; }
        if (r == 0) break;
        if (fwrite(buf, 1, (size_t)r, f) != (size_t)r) { ok = false; snprintf(err, errlen, "The SD card is full."); break; }
        if (pg) pg->done += (uint32_t)r;
    }
    conn_close(&s.c);
    if (fclose(f) != 0) ok = false;
    if (ok && length > 0 && pg && pg->done != (uint32_t)length) { ok = false; snprintf(err, errlen, "The download was cut off."); }
    if (!ok) { remove(part); return false; }
    remove(path);
    if (rename(part, path) != 0) { remove(part); snprintf(err, errlen, "Couldn't save the download."); return false; }
    return true;
}
