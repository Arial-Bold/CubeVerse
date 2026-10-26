/* web.h - a small HTTP/1.1 client for talking to Juxt and its picture
 * server: GET and POST with any headers and body, keeping the secure
 * connection open between requests (a TLS handshake takes the GameCube a
 * second or two, so it's done once per server, not once per request).
 *
 * A WebClient is used by one thread at a time; each worker thread has its
 * own. Redirects are not followed: the caller sees the status and Location
 * (Juxt answers a sign-in or a new post with a redirect). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "conn.h"

#define WEB_SLOTS 2

typedef struct {
    char host[128];
    unsigned short port;
    bool tls;
    bool open;
    Conn c;
    int used;               /* requests sent on this connection */
    unsigned last_use;      /* for picking a slot to reuse */
} WebSlot;

typedef struct {
    WebSlot slot[WEB_SLOTS];
    unsigned tick;
    /* each Set-Cookie header of every answer goes here */
    void (*on_cookie)(void *ctx, const char *host, const char *set_cookie);
    void *cookie_ctx;
} WebClient;

typedef struct {
    int    status;
    char  *body;            /* malloc'd, NUL-terminated (free with web_resp_free) */
    size_t len;
    char   location[512];
    char   content_type[64];
} WebResp;

void web_init(WebClient *wc);
/* url: "https://host/path". headers: extra lines, each ending "\r\n" (or NULL).
 * Returns false (err filled) if there was no answer at all. */
bool web_request(WebClient *wc, const char *method, const char *url, const char *headers, const void *body,
                 size_t body_len, size_t max, WebResp *r, volatile int *cancel, char *err, size_t errlen);
void web_resp_free(WebResp *r);
void web_close_all(WebClient *wc);

/* Helpers for building requests. */
/* Appends s to out, %-encoded for a URL or a form (spaces as +). */
size_t web_urlencode(char *out, size_t cap, const char *s);
/* Standard base64 of n bytes into out (cap must be 4*((n+2)/3)+1). */
size_t web_base64(char *out, const unsigned char *in, size_t n);
