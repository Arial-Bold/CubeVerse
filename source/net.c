/* net.c - see net.h. The phone sign-in server answers one request at a
 * time, with "Connection: close".
 *
 *   GET  /          the sign-in page
 *   POST /signin    username=..&password=.. (form)
 */
#include "net.h"
#include "settings.h"
#include "applog.h"
#include "thr.h"
#include <ctype.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef GEKKO
#include <network.h>
#define SK_SOCKET(a, b, c) net_socket(a, b, c)
#define SK_BIND net_bind
#define SK_LISTEN net_listen
#define SK_ACCEPT net_accept
#define SK_RECV(s, b, n) net_recv(s, b, n, 0)
#define SK_SEND(s, b, n) net_send(s, b, n, 0)
#define SK_CLOSE net_close
#define SK_SELECT net_select
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define SK_SOCKET(a, b, c) socket(a, b, c)
#define SK_BIND bind
#define SK_LISTEN listen
#define SK_ACCEPT accept
#define SK_RECV(s, b, n) recv(s, b, n, 0)
#define SK_SEND(s, b, n) send(s, b, n, MSG_NOSIGNAL)
#define SK_CLOSE close
#define SK_SELECT select
#endif

static volatile NetState state = NET_OFF;
static char ip[16], mask[16], gw[16], msg[96] = "Network not started", url[48];
static int port = 80;
static thr_t thread;
static thr_mutex lock;
static char pend_user[64], pend_pass[128];
static volatile bool pending;
static volatile bool serving;

NetState net_state(void) { return state; }
const char *net_ip(void) { return ip; }
const char *net_gateway(void) { return gw; }
const char *net_message(void) { return msg; }
const char *net_url(void) { return url; }
bool net_server_ready(void) { return serving; }

bool net_take_signin(char *user, size_t ucap, char *pass, size_t pcap) {
    if (!pending) return false;
    thr_lock(&lock);
    snprintf(user, ucap, "%s", pend_user);
    snprintf(pass, pcap, "%s", pend_pass);
    memset(pend_pass, 0, sizeof(pend_pass));
    pending = false;
    thr_unlock(&lock);
    return true;
}

static int wait_fd(int fd, bool wr, int ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    int r = SK_SELECT(fd + 1, wr ? NULL : &set, wr ? &set : NULL, NULL, &tv);
    return r < 0 ? -1 : r > 0;
}

static bool send_all(int fd, const void *data, size_t len) {
    const char *p = data;
    while (len > 0) {
        if (wait_fd(fd, true, 20000) != 1) return false;
        int chunk = len > 8192 ? 8192 : (int)len;
        int n = SK_SEND(fd, p, chunk);
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

static int recv_some(int fd, void *buf, int cap, int ms) {
    if (wait_fd(fd, false, ms) != 1) return -1;
    int n = SK_RECV(fd, buf, cap);
    return n;
}

static void send_head(int fd, int code, const char *status, const char *type, long len, const char *extra) {
    char h[512];
    int n = snprintf(h, sizeof(h),
                     "HTTP/1.0 %d %s\r\nContent-Type: %s\r\n%s%sConnection: close\r\nCache-Control: no-cache\r\n",
                     code, status, type, extra ? extra : "", "");
    if (len >= 0) n += snprintf(h + n, sizeof(h) - n, "Content-Length: %ld\r\n", len);
    n += snprintf(h + n, sizeof(h) - n, "\r\n");
    send_all(fd, h, (size_t)n);
}

static void send_text(int fd, int code, const char *status, const char *type, const char *body, size_t len) {
    send_head(fd, code, status, type, (long)len, NULL);
    send_all(fd, body, len);
}

static void send_json(int fd, int code, const char *status, const char *body) {
    send_text(fd, code, status, "application/json", body, strlen(body));
}

/* ------------------------------------------------------------- helpers */

static void url_decode(char *s) {
    char *o = s;
    for (; *s; s++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 2;
        } else if (*s == '+') *o++ = ' ';
        else *o++ = *s;
    }
    *o = 0;
}

static const char PAGE[] =
"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>CubeVerse sign in</title><style>"
"body{margin:0;font:16px/1.4 system-ui,sans-serif;background:#0e1218;color:#eef1f6;display:flex;min-height:100vh;align-items:center;justify-content:center}"
"form{background:#fff;color:#2a2f45;padding:24px;width:min(340px,86vw);box-shadow:0 8px 30px #000a}"
"h1{font-size:20px;margin:0 0 4px;color:#3a9a18}p{margin:0 0 16px;color:#5c6380;font-size:14px}"
"label{display:block;font-size:12px;font-weight:700;color:#5c6380;margin-top:12px}"
"input{width:100%;box-sizing:border-box;font:inherit;padding:10px;border:1px solid #cbd3e6;margin-top:4px}"
"button{margin-top:18px;width:100%;padding:12px;border:0;background:#56c22f;color:#fff;font:700 16px system-ui,sans-serif}"
"</style></head><body><form method=post action=/signin><h1>CubeVerse</h1>"
"<p>Sign in with your Pretendo Network ID. It goes straight to your GameCube, which signs in to Juxt.</p>"
"<label>Pretendo Network ID<input name=username autocapitalize=none autocomplete=username required></label>"
"<label>Password<input name=password type=password autocomplete=current-password required></label>"
"<button>Sign in on my GameCube</button></form></body></html>";

static const char DONE[] =
"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<style>body{margin:0;font:16px/1.4 system-ui,sans-serif;background:#0e1218;color:#eef1f6;display:flex;min-height:100vh;"
"align-items:center;justify-content:center;text-align:center;padding:24px}</style></head><body>"
"<div><h2>Sent to your GameCube</h2><p>Look at your TV: CubeVerse is signing in now. You can close this page.</p></div></body></html>";

static void field(const char *body, const char *name, char *out, size_t cap) {
    out[0] = 0;
    size_t n = strlen(name);
    const char *p = body;
    while (p && *p) {
        if (!strncmp(p, name, n) && p[n] == '=') {
            p += n + 1;
            size_t k = strcspn(p, "&");
            if (k >= cap) k = cap - 1;
            memcpy(out, p, k);
            out[k] = 0;
            url_decode(out);
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

static void handle(int fd) {
    enum { CAP = 4096 };
    char *req = malloc(CAP + 1);
    if (!req) return;
    int len = 0;
    char *end = NULL;
    long clen = 0;
    while (len < CAP) {
        int n = recv_some(fd, req + len, CAP - len, 15000);
        if (n <= 0) break;
        len += n;
        req[len] = 0;
        if (!end && (end = strstr(req, "\r\n\r\n")) != NULL) {
            for (char *h = strstr(req, "\r\n"); h && h < end; h = strstr(h + 2, "\r\n"))
                if (!strncasecmp(h + 2, "Content-Length:", 15)) clen = atol(h + 17);
        }
        if (end && len - (int)(end + 4 - req) >= clen) break;
    }
    if (!end) { free(req); return; }
    *end = 0;
    char *body = end + 4;
    char method[8] = "", target[200] = "";
    sscanf(req, "%7s %199s", method, target);
    if (!strcmp(method, "POST") && !strcmp(target, "/signin")) {
        char u[64], p[128];
        field(body, "username", u, sizeof(u));
        field(body, "password", p, sizeof(p));
        if (u[0] && p[0]) {
            thr_lock(&lock);
            snprintf(pend_user, sizeof(pend_user), "%s", u);
            snprintf(pend_pass, sizeof(pend_pass), "%s", p);
            pending = true;
            thr_unlock(&lock);
        }
        memset(p, 0, sizeof(p));
        memset(body, 0, (size_t)(req + len - body));
        send_text(fd, 200, "OK", "text/html; charset=utf-8", DONE, sizeof(DONE) - 1);
    } else if (!strcmp(method, "GET") && (!strcmp(target, "/") || !strncmp(target, "/?", 2))) {
        send_text(fd, 200, "OK", "text/html; charset=utf-8", PAGE, sizeof(PAGE) - 1);
    } else {
        send_text(fd, 404, "Not Found", "text/plain", "Not found", 9);
    }
    free(req);
}

/* ------------------------------------------------------------- startup */

static bool bring_up(void) {
#ifdef GEKKO
    if (g_set.dhcp) ip[0] = mask[0] = gw[0] = 0;
    else {
        snprintf(ip, sizeof(ip), "%s", g_set.ip);
        snprintf(mask, sizeof(mask), "%s", g_set.netmask);
        snprintf(gw, sizeof(gw), "%s", g_set.gateway);
    }
    s32 r = if_config(ip, mask, gw, g_set.dhcp ? true : false);
    if (r < 0) {
        snprintf(msg, sizeof(msg), "No network (error %d). Check the network adapter's cable.", (int)r);
        return false;
    }
    return ip[0] != 0;
#else
    snprintf(ip, sizeof(ip), "192.168.1.42");   /* PC tests: an address like a home network's, for screenshots */
    return true;
#endif
}

static int open_server(int p) {
    int s = SK_SOCKET(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) return -1;
#ifndef GEKKO
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
#ifdef GEKKO
    sa.sin_len = sizeof(sa);
#endif
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)p);
    sa.sin_addr.s_addr = INADDR_ANY;
    if (SK_BIND(s, (struct sockaddr *)&sa, sizeof(sa)) < 0 || SK_LISTEN(s, 8) < 0) {
        SK_CLOSE(s);
        return -1;
    }
    return s;
}

static void *net_main(void *arg) {
    (void)arg;
    snprintf(msg, sizeof(msg), g_set.dhcp ? "Asking your router for an address..." : "Starting the network...");
    if (!bring_up()) {
        state = NET_FAILED;
        if (!msg[0] || !strncmp(msg, "Asking", 6) || !strncmp(msg, "Starting", 8))
            snprintf(msg, sizeof(msg), "No network. Is the network adapter connected?");
        return NULL;
    }
    snprintf(msg, sizeof(msg), "Connected");
    state = NET_UP;
    app_log("network: %s", ip);
    /* the phone sign-in page */
    port = g_set.http_port;
    int s = open_server(port);
    if (s < 0 && port != 8080) { port = 8080; s = open_server(port); }
    if (s < 0) return NULL;
    if (port == 80) snprintf(url, sizeof(url), "http://%s/", ip);
    else snprintf(url, sizeof(url), "http://%s:%d/", ip, port);
    serving = true;
    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int c = SK_ACCEPT(s, (struct sockaddr *)&from, &fl);
        if (c < 0) { thr_sleep_ms(50); continue; }
        handle(c);
        SK_CLOSE(c);
    }
    return NULL;
}

void net_start(void) {
    if (state != NET_OFF) return;
    thr_mutex_init(&lock);
    state = NET_STARTING;
    if (!thr_create(&thread, net_main, NULL, 64 * 1024, 60)) {
        state = NET_FAILED;
        snprintf(msg, sizeof(msg), "Couldn't start the network.");
    }
}
