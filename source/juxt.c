/* juxt.c - see juxt.h. One worker thread does all of Juxt's network work
 * in order, keeping one secure connection open to the Juxt server. */
#include "strutil.h"
#include "juxt.h"
#include "web.h"
#include "net.h"
#include "storage.h"
#include "applog.h"
#include "thr.h"
#ifdef GEKKO
#include "netio.h"
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PAGE_MAX (2u * 1024 * 1024)

/* ---------------------------------------------------------------- jobs */

typedef enum {
    J_PAGE, J_MORE, J_SIGNIN, J_CHECK, J_YEAH, J_FOLLOW_C, J_FOLLOW_U, J_UNREAD, J_POST_TEXT, J_POST_DRAW
} JobType;

typedef struct {
    JobType type;
    JPage *pg;
    int gen;
    JPageKind kind;
    char arg[256];
    JResult *res;
    char a[64], b[128];       /* user/password, community id / reply id */
    char login_user[64], login_pass[128];   /* a sign-in: what was typed, saved once it works */
    char *text;               /* malloc'd */
    uint8_t *bits;            /* malloc'd 320x120 */
    int feeling;
    bool spoiler;
    /* filled by the worker */
    JPage *out;               /* malloc'd result page */
    bool ok;
    char err[160];
    int status, count;
    bool on;
    char location[256];
    bool signed_out;          /* the server said the session is no longer good */
} Job;

#define QCAP 32
static Job *queue[QCAP], *done_q[QCAP];
static int qhead, qtail, dhead, dtail;
static thr_mutex qlock;
static thr_t worker;
static volatile int quit;

static WebClient wc;

/* ------------------------------------------------------------ the session */

#define NCOOK 12
typedef struct { char name[48]; char value[600]; } Cookie;
static Cookie cookies[NCOOK];
static thr_mutex clock_;
static volatile bool signed_in;
static char my_name[48], my_mii[200];
static int my_pid;

/* The Pretendo ID and password, remembered on the SD card so CubeVerse can
 * sign in again by itself when Juxt's session runs out. They are scrambled
 * so they can't be read at a glance, but anyone with the SD card and some
 * know-how could recover them; Sign out in Settings deletes them. */
static char saved_user[64], saved_pass[128];
static const char SCRAMBLE[] = "CubeVerse-GameCube-Juxt";

static void scramble(const char *in, char *hex, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; in[i] && n + 3 < cap; i++) n += (size_t)snprintf(hex + n, cap - n, "%02x", (unsigned char)(in[i] ^ SCRAMBLE[i % (sizeof(SCRAMBLE) - 1)]));
    hex[n] = 0;
}

static void unscramble(const char *hex, char *out, size_t cap) {
    size_t i = 0;
    while (hex[0] && hex[1] && i + 1 < cap) {
        char b[3] = { hex[0], hex[1], 0 };
        out[i] = (char)(strtol(b, NULL, 16) ^ SCRAMBLE[i % (sizeof(SCRAMBLE) - 1)]);
        i++;
        hex += 2;
    }
    out[i] = 0;
}

static void save_account(const char *user, const char *pass) {
    char u[200], p[300], buf[600];
    scramble(user, u, sizeof(u));
    scramble(pass, p, sizeof(p));
    int n = snprintf(buf, sizeof(buf), "# CubeVerse: your Pretendo Network ID, kept so CubeVerse can sign in again by itself.\n# Sign out in CubeVerse's Settings to delete it.\nuser=%s\npass=%s\n", u, p);
    char path[300];
    storage_path(path, sizeof(path), "account.ini");
    storage_write_atomic(path, buf, (size_t)n);
}

static void load_account(void) {
    char path[300];
    storage_path(path, sizeof(path), "account.ini");
    char *t = storage_read(path, NULL);
    if (!t) return;
    for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
        size_t l = strlen(line);
        if (l && line[l - 1] == '\r') line[l - 1] = 0;
        if (!strncmp(line, "user=", 5)) unscramble(line + 5, saved_user, sizeof(saved_user));
        else if (!strncmp(line, "pass=", 5)) unscramble(line + 5, saved_pass, sizeof(saved_pass));
    }
    memset(t, 0, strlen(t));
    free(t);
}

bool juxt_saved_login(char *user, size_t ucap, char *pass, size_t pcap) {
    thr_lock(&clock_);
    bool have = saved_user[0] && saved_pass[0];
    if (have) {
        snprintf(user, ucap, "%s", saved_user);
        snprintf(pass, pcap, "%s", saved_pass);
    }
    thr_unlock(&clock_);
    return have;
}

bool juxt_signed_in(void) { return signed_in; }
const char *juxt_my_name(void) { return my_name; }
int juxt_my_pid(void) { return my_pid; }
const char *juxt_my_mii(void) { return my_mii; }

static void save_session(void) {
    char buf[8192];
    int n = snprintf(buf, sizeof(buf), "# CubeVerse sign-in (Juxt cookies)\n");
    thr_lock(&clock_);
    for (int i = 0; i < NCOOK; i++)
        if (cookies[i].name[0] && n < (int)sizeof(buf) - 700)
            n += snprintf(buf + n, sizeof(buf) - n, "cookie=%s=%s\n", cookies[i].name, cookies[i].value);
    thr_unlock(&clock_);
    n += snprintf(buf + n, sizeof(buf) - n, "name=%s\npid=%d\nmii=%s\n", my_name, my_pid, my_mii);
    char path[300];
    storage_path(path, sizeof(path), "session.ini");
    storage_write_atomic(path, buf, (size_t)n);
}

static void set_cookie(const char *name, const char *value, bool remove_it) {
    thr_lock(&clock_);
    int slot = -1;
    for (int i = 0; i < NCOOK; i++) if (!strcmp(cookies[i].name, name)) { slot = i; break; }
    if (remove_it) {
        if (slot >= 0) cookies[slot].name[0] = 0;
    } else {
        if (slot < 0) for (int i = 0; i < NCOOK; i++) if (!cookies[i].name[0]) { slot = i; break; }
        if (slot < 0) slot = NCOOK - 1;
        snprintf(cookies[slot].name, sizeof(cookies[slot].name), "%s", name);
        snprintf(cookies[slot].value, sizeof(cookies[slot].value), "%s", value);
    }
    thr_unlock(&clock_);
}

static void on_cookie(void *ctx, const char *host, const char *sc) {
    (void)ctx;
    if (!str_find_nocase(host, "pretendo")) return;
    char name[48], value[600];
    size_t n = strcspn(sc, "=");
    if (!sc[n] || n >= sizeof(name)) return;
    memcpy(name, sc, n);
    name[n] = 0;
    const char *v = sc + n + 1;
    size_t vl = strcspn(v, ";");
    if (vl >= sizeof(value)) vl = sizeof(value) - 1;
    memcpy(value, v, vl);
    value[vl] = 0;
    bool gone = !value[0] || str_find_nocase(sc, "Max-Age=0") || str_find_nocase(sc, "Expires=Thu, 01 Jan 1970");
    set_cookie(name, value, gone);
}

static void load_session(void) {
    char path[300];
    storage_path(path, sizeof(path), "session.ini");
    char *t = storage_read(path, NULL);
    if (!t) return;
    for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
        size_t l = strlen(line);
        if (l && line[l - 1] == '\r') line[l - 1] = 0;
        if (!strncmp(line, "cookie=", 7)) {
            char *eq = strchr(line + 7, '=');
            if (eq) { *eq = 0; set_cookie(line + 7, eq + 1, false); }
        } else if (!strncmp(line, "name=", 5)) snprintf(my_name, sizeof(my_name), "%s", line + 5);
        else if (!strncmp(line, "pid=", 4)) my_pid = atoi(line + 4);
        else if (!strncmp(line, "mii=", 4)) snprintf(my_mii, sizeof(my_mii), "%s", line + 4);
    }
    free(t);
    bool tok = false;
    for (int i = 0; i < NCOOK; i++) if (!strcmp(cookies[i].name, "access_token") && cookies[i].value[0]) tok = true;
    signed_in = tok && my_name[0];
}

static void cookie_header(char *out, size_t cap) {
    int n = 0;
    out[0] = 0;
    thr_lock(&clock_);
    for (int i = 0; i < NCOOK; i++) {
        if (!cookies[i].name[0]) continue;
        n += snprintf(out + n, cap - n, "%s%s=%s", n ? "; " : "Cookie: ", cookies[i].name, cookies[i].value);
        if (n >= (int)cap) break;
    }
    thr_unlock(&clock_);
    if (n && n + 3 < (int)cap) snprintf(out + n, cap - n, "\r\n");
}

/* ---------------------------------------------------------- requests */

static bool wait_net(char *err, size_t errlen) {
    static bool tls_ready;
    for (int i = 0; i < 600 && net_state() == NET_STARTING && !quit; i++) thr_sleep_ms(50);
    if (net_state() != NET_UP) {
        snprintf(err, errlen, "%s", net_state() == NET_FAILED ? net_message() : "The network isn't ready yet.");
        return false;
    }
    if (!tls_ready) {
#ifdef GEKKO
        if (!netio_time_synced()) netio_sync_time();   /* certificates are checked against the date */
#endif
        if (!tls_global_init(err, errlen)) return false;
        tls_ready = true;
    }
    return true;
}

/* GET or POST on Juxt. Returns false (err filled) if there was no answer. */
static bool juxt_req(const char *method, const char *path, const char *ctype, const void *body, size_t blen,
                     WebResp *r, char *err, size_t errlen) {
    if (!wait_net(err, errlen)) return false;
    char url[600], hdr[8192];
    snprintf(url, sizeof(url), "%s%s", JUXT_BASE, path);
    cookie_header(hdr, sizeof(hdr) - 300);
    size_t hl = strlen(hdr);
    snprintf(hdr + hl, sizeof(hdr) - hl, "Accept: text/html,application/json\r\n%s%s%s",
             ctype ? "Content-Type: " : "", ctype ? ctype : "", ctype ? "\r\n" : "");
    if (!strcmp(method, "POST")) {
        hl = strlen(hdr);
        snprintf(hdr + hl, sizeof(hdr) - hl, "Origin: %s\r\nReferer: %s/\r\nX-Requested-With: XMLHttpRequest\r\n", JUXT_BASE, JUXT_BASE);
    }
    bool ok = web_request(&wc, method, url, hdr, body, blen, PAGE_MAX, r, &quit, err, errlen);
    if (ok) app_log("juxt %s %s -> %d (%u bytes)", method, path, r->status, (unsigned)r->len);
    else app_log("juxt %s %s failed: %s", method, path, err);
    return ok;
}

static bool to_login(const WebResp *r) {
    return (r->status >= 300 && r->status < 400 && strstr(r->location, "/login")) || r->status == 401;
}

static const char *page_path(JPageKind k, const char *arg, char *buf, size_t cap) {
    switch (k) {
    case JP_FEED: return "/feed?pjax=true";
    case JP_FEED_PEOPLE: return "/feed/people?pjax=true";
    case JP_FEED_ALL: return "/feed/all?pjax=true";
    case JP_COMMUNITIES: return "/titles";
    case JP_COMMUNITIES_ALL: return "/titles/all";
    case JP_COMMUNITY_NEW: snprintf(buf, cap, "/titles/%s/new", arg); return buf;
    case JP_COMMUNITY_HOT: snprintf(buf, cap, "/titles/%s/hot", arg); return buf;
    case JP_POST: snprintf(buf, cap, "/posts/%s", arg); return buf;
    case JP_USER_POSTS: snprintf(buf, cap, "/users/%s", arg[0] ? arg : "me"); return buf;
    case JP_USER_YEAHS: snprintf(buf, cap, "/users/%s/yeahs", arg[0] ? arg : "me"); return buf;
    case JP_USER_FOLLOWERS: snprintf(buf, cap, "/users/%s/followers", arg[0] ? arg : "me"); return buf;
    case JP_NEWS: return "/news/my_news";
    default: return "/";
    }
}

static void fill_page(JPage *out, JPageKind kind, const char *html, size_t len) {
    switch (kind) {
    case JP_COMMUNITIES:
    case JP_COMMUNITIES_ALL:
        out->ncomms = jp_parse_communities(html, len, out->comms, JPAGE_COMMUNITIES, &out->npopular);
        break;
    case JP_NEWS:
        out->nnews = jp_parse_news(html, len, out->news, JPAGE_NEWS, out->next, sizeof(out->next));
        break;
    case JP_COMMUNITY_NEW:
    case JP_COMMUNITY_HOT:
    case JP_USER_POSTS:
    case JP_USER_YEAHS:
        jp_parse_header(html, len, out);
        if (kind == JP_USER_POSTS || kind == JP_USER_YEAHS) {
            out->pid = 0;
            const char *m = strstr(out->icon, "/mii/");
            if (m) out->pid = atoi(m + 5);
        }
        out->nposts = jp_parse_posts(html, len, out->posts, JPAGE_POSTS, out->next, sizeof(out->next));
        break;
    case JP_USER_FOLLOWERS:
        jp_parse_header(html, len, out);
        out->pid = 0;
        {
            const char *m = strstr(out->icon, "/mii/");
            if (m) out->pid = atoi(m + 5);
        }
        out->nposts = 0;
        out->ncomms = jp_parse_people(html, len, out->comms, JPAGE_COMMUNITIES);
        break;
    default:
        out->nposts = jp_parse_posts(html, len, out->posts, JPAGE_POSTS, out->next, sizeof(out->next));
        break;
    }
}

static void do_page(Job *j, bool more) {
    char pbuf[300];
    const char *path = more ? j->arg : page_path(j->kind, j->arg, pbuf, sizeof(pbuf));
    WebResp r;
    if (!juxt_req("GET", path, NULL, NULL, 0, &r, j->err, sizeof(j->err))) return;
    if (to_login(&r)) {
        j->signed_out = true;
        snprintf(j->err, sizeof(j->err), "Your sign-in has run out. Please sign in again.");
    } else if (r.status == 404) {
        snprintf(j->err, sizeof(j->err), "That page isn't on Juxt any more.");
    } else if (r.status >= 300 && r.status < 400 && j->kind == JP_FEED && strstr(r.location, "404")) {
        snprintf(j->err, sizeof(j->err), "Your account hasn't been set up on Juxt yet. Set it up once on a Wii U or 3DS.");
    } else if (r.status != 200) {
        snprintf(j->err, sizeof(j->err), "Juxt answered with error %d.", r.status);
    } else {
        j->out = calloc(1, sizeof(JPage));
        if (!j->out) snprintf(j->err, sizeof(j->err), "Out of memory.");
        else {
            fill_page(j->out, j->kind, r.body, r.len);
            j->ok = true;
        }
    }
    web_resp_free(&r);
}

static int json_int(const char *s, const char *key) {
    char k[48];
    snprintf(k, sizeof(k), "\"%s\"", key);
    const char *p = strstr(s, k);
    if (!p) return -1;
    p = strchr(p + strlen(k), ':');
    return p ? atoi(p + 1) : -1;
}

static void json_str(const char *s, const char *key, char *out, size_t cap) {
    char k[48];
    out[0] = 0;
    snprintf(k, sizeof(k), "\"%s\"", key);
    const char *p = strstr(s, k);
    if (!p) return;
    p = strchr(p + strlen(k), ':');
    if (!p) return;
    p = strchr(p, '"');
    if (!p) return;
    p++;
    size_t n = strcspn(p, "\"");
    if (n >= cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

static void form_add(char *out, size_t cap, const char *name, const char *value) {
    size_t n = strlen(out);
    if (n) { if (n + 1 < cap) { out[n++] = '&'; out[n] = 0; } }
    web_urlencode(out, cap, name);
    n = strlen(out);
    if (n + 1 < cap) { out[n++] = '='; out[n] = 0; }
    web_urlencode(out, cap, value);
}

static void do_check(Job *j) {
    WebResp r;
    if (!juxt_req("GET", "/users/me", NULL, NULL, 0, &r, j->err, sizeof(j->err))) return;
    if (to_login(&r)) {
        j->signed_out = true;
        snprintf(j->err, sizeof(j->err), "Please sign in.");
    } else if (r.status == 200) {
        JPage *pg = calloc(1, sizeof(JPage));
        if (pg) {
            jp_parse_header(r.body, r.len, pg);
            if (pg->title[0]) {
                /* handed to the main thread in apply() */
                snprintf(j->a, sizeof(j->a), "%s", pg->title);
                snprintf(j->location, sizeof(j->location), "%s", pg->icon);
                const char *m = strstr(pg->icon, "/mii/");
                j->count = m ? atoi(m + 5) : 0;
                j->ok = true;
            } else {
                snprintf(j->err, sizeof(j->err),
                         "This account hasn't been set up on Juxt yet. Open Juxt once on a Wii U or 3DS, then try again.");
            }
            free(pg);
        }
    } else if (r.status >= 300 && r.status < 400) {
        snprintf(j->err, sizeof(j->err),
                 "This account hasn't been set up on Juxt yet. Open Juxt once on a Wii U or 3DS, then try again.");
    } else {
        snprintf(j->err, sizeof(j->err), "Juxt answered with error %d.", r.status);
    }
    web_resp_free(&r);
}

static void do_signin(Job *j) {
    /* a fresh session */
    thr_lock(&clock_);
    memset(cookies, 0, sizeof(cookies));
    thr_unlock(&clock_);
    char body[600] = "";
    form_add(body, sizeof(body), "username", j->a);
    form_add(body, sizeof(body), "password", j->b);
    form_add(body, sizeof(body), "grant_type", "password");
    form_add(body, sizeof(body), "redirect", "/users/me");
    snprintf(j->login_user, sizeof(j->login_user), "%s", j->a);
    snprintf(j->login_pass, sizeof(j->login_pass), "%s", j->b);
    memset(j->b, 0, sizeof(j->b));
    WebResp r;
    bool ok = juxt_req("POST", "/login", "application/x-www-form-urlencoded", body, strlen(body), &r, j->err, sizeof(j->err));
    memset(body, 0, sizeof(body));
    if (!ok) return;
    bool has_token = false;
    for (int i = 0; i < NCOOK; i++) if (!strcmp(cookies[i].name, "access_token") && cookies[i].value[0]) has_token = true;
    if (r.status >= 300 && r.status < 400 && has_token) {
        web_resp_free(&r);
        do_check(j);
        return;
    }
    char toast[160] = "";
    if (r.body) jp_parse_toast(r.body, r.len, toast, sizeof(toast));
    if (toast[0]) snprintf(j->err, sizeof(j->err), "%s", toast);
    else if (r.status == 504 || r.status == 503) snprintf(j->err, sizeof(j->err), "Juxt is closed for now. Try again later.");
    else snprintf(j->err, sizeof(j->err), "Couldn't sign in (error %d).", r.status);
    web_resp_free(&r);
}

static void do_json_action(Job *j, const char *path, const char *field_name, const char *value) {
    char body[200] = "";
    form_add(body, sizeof(body), field_name, value);
    WebResp r;
    if (!juxt_req("POST", path, "application/x-www-form-urlencoded", body, strlen(body), &r, j->err, sizeof(j->err))) return;
    j->status = r.status;
    if (to_login(&r)) {
        j->signed_out = true;
        snprintf(j->err, sizeof(j->err), "Please sign in again.");
    } else if (r.status == 200 && r.body) {
        int st = json_int(r.body, "status");
        j->count = json_int(r.body, "count");
        if (j->count < 0) j->count = json_int(r.body, "follower_count");
        char act[24];
        json_str(r.body, "action", act, sizeof(act));
        j->on = !strcmp(act, "follow");
        if (st == 200 || st < 0) j->ok = true;
        else if (st == 423) snprintf(j->err, sizeof(j->err), "Slow down a little and try that again.");
        else snprintf(j->err, sizeof(j->err), "Juxt couldn't do that (error %d).", st);
    } else if (r.status == 429) {
        snprintf(j->err, sizeof(j->err), "Slow down a little and try that again.");
    } else {
        snprintf(j->err, sizeof(j->err), "Juxt answered with error %d.", r.status);
    }
    web_resp_free(&r);
}

static void do_unread(Job *j) {
    WebResp r;
    if (!juxt_req("GET", "/users/notifications.json", NULL, NULL, 0, &r, j->err, sizeof(j->err))) return;
    if (r.status == 200 && r.body) {
        j->count = json_int(r.body, "notification_count");
        j->ok = j->count >= 0;
    } else if (to_login(&r)) j->signed_out = true;
    web_resp_free(&r);
}

/* A 320x120 black-and-white BMP of the drawing. */
static unsigned char *make_bmp(const uint8_t *bits, size_t *len) {
    const int W = 320, H = 120, stride = 40;
    size_t n = 62 + (size_t)stride * H;
    unsigned char *b = calloc(1, n);
    if (!b) return NULL;
    #define LE16(o, v) do { b[o] = (unsigned char)(v); b[(o) + 1] = (unsigned char)((v) >> 8); } while (0)
    #define LE32(o, v) do { LE16(o, (v) & 0xFFFF); LE16((o) + 2, ((unsigned)(v)) >> 16); } while (0)
    b[0] = 'B'; b[1] = 'M';
    LE32(2, n); LE32(10, 62);
    LE32(14, 40); LE32(18, W); LE32(22, H); LE16(26, 1); LE16(28, 1); LE32(30, 0);
    LE32(34, stride * H); LE32(38, 2835); LE32(42, 2835); LE32(46, 2); LE32(50, 2);
    /* palette: 0 = black, 1 = white */
    b[54] = 0; b[55] = 0; b[56] = 0; b[57] = 0;
    b[58] = 255; b[59] = 255; b[60] = 255; b[61] = 0;
    for (int y = 0; y < H; y++) {
        unsigned char *row = b + 62 + (size_t)(H - 1 - y) * stride;   /* bottom row first */
        for (int x = 0; x < W; x++)
            if (!bits[y * W + x]) row[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
    }
    *len = n;
    return b;
}

static void mp_field(char *out, size_t cap, const char *boundary, const char *name, const char *value) {
    size_t n = strlen(out);
    snprintf(out + n, cap - n, "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n", boundary, name, value);
}

static void do_post(Job *j, bool drawing) {
    const char *boundary = "----CubeVerseFormBoundary7MA4YWxk";
    size_t paint_len = 0;
    char *paint64 = NULL;
    if (drawing) {
        size_t bl;
        unsigned char *bmp = make_bmp(j->bits, &bl);
        if (!bmp) { snprintf(j->err, sizeof(j->err), "Out of memory."); return; }
        paint64 = malloc(4 * ((bl + 2) / 3) + 1);
        if (paint64) paint_len = web_base64(paint64, bmp, bl);
        free(bmp);
        if (!paint64) { snprintf(j->err, sizeof(j->err), "Out of memory."); return; }
    }
    size_t cap = 4096 + (j->text ? strlen(j->text) : 0) + paint_len;
    char *body = malloc(cap);
    if (!body) { free(paint64); snprintf(j->err, sizeof(j->err), "Out of memory."); return; }
    body[0] = 0;
    char feel[8];
    snprintf(feel, sizeof(feel), "%d", j->feeling);
    mp_field(body, cap, boundary, "community_id", j->a);
    mp_field(body, cap, boundary, "_post_type", drawing ? "painting" : "body");
    mp_field(body, cap, boundary, "feeling_id", feel);
    mp_field(body, cap, boundary, "spoiler", j->spoiler ? "true" : "false");
    mp_field(body, cap, boundary, "body", j->text ? j->text : "");
    if (drawing) {
        mp_field(body, cap, boundary, "bmp", "true");
        mp_field(body, cap, boundary, "painting", paint64);
    }
    size_t n = strlen(body);
    snprintf(body + n, cap - n, "--%s--\r\n", boundary);
    free(paint64);
    char path[200], ctype[120];
    if (j->b[0]) snprintf(path, sizeof(path), "/posts/%s/new", j->b);
    else snprintf(path, sizeof(path), "/posts/new");
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", boundary);
    WebResp r;
    bool ok = juxt_req("POST", path, ctype, body, strlen(body), &r, j->err, sizeof(j->err));
    free(body);
    if (!ok) return;
    j->status = r.status;
    snprintf(j->location, sizeof(j->location), "%s", r.location);
    if (to_login(&r)) {
        j->signed_out = true;
        snprintf(j->err, sizeof(j->err), "Please sign in again, then post.");
    } else if (r.status >= 300 && r.status < 400) {
        if (strstr(r.location, "error-text=")) {
            char msg[200];
            snprintf(msg, sizeof(msg), "%s", strstr(r.location, "error-text=") + 11);
            for (char *p = msg; *p; p++) if (*p == '+') *p = ' ';
            /* %-decoding */
            char *o = msg;
            for (char *p = msg; *p; p++) {
                if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                    char hx[3] = { p[1], p[2], 0 };
                    *o++ = (char)strtol(hx, NULL, 16);
                    p += 2;
                } else *o++ = *p;
            }
            *o = 0;
            snprintf(j->err, sizeof(j->err), "%s", msg);
        } else if (strstr(r.location, "/create")) {
            snprintf(j->err, sizeof(j->err), "Juxt didn't accept that post. Check it and try again.");
        } else {
            j->ok = true;
        }
    } else if (r.status == 429) {
        snprintf(j->err, sizeof(j->err), "Too many posts for now. Wait a minute and try again.");
    } else {
        char toast[160] = "";
        if (r.body) jp_parse_toast(r.body, r.len, toast, sizeof(toast));
        snprintf(j->err, sizeof(j->err), "%s", toast[0] ? toast : "Juxt couldn't post that.");
    }
    web_resp_free(&r);
}

static void run_once(Job *j) {
    switch (j->type) {
    case J_PAGE: do_page(j, false); break;
    case J_MORE: do_page(j, true); break;
    case J_SIGNIN: do_signin(j); break;
    case J_CHECK: do_check(j); break;
    case J_YEAH: do_json_action(j, "/posts/empathy", "postID", j->a); break;
    case J_FOLLOW_C: do_json_action(j, "/titles/follow", "id", j->a); break;
    case J_FOLLOW_U: do_json_action(j, "/users/follow", "id", j->a); break;
    case J_UNREAD: do_unread(j); break;
    case J_POST_TEXT: do_post(j, false); break;
    case J_POST_DRAW: do_post(j, true); break;
    }
}

static void save_session(void);

/* When Juxt says the session has run out, sign in again with the saved ID
 * and password (once), then do the job again. */
static void run(Job *j) {
    run_once(j);
    if (!j->signed_out || j->type == J_SIGNIN) return;
    Job t;
    memset(&t, 0, sizeof(t));
    t.type = J_SIGNIN;
    if (!juxt_saved_login(t.a, sizeof(t.a), t.b, sizeof(t.b))) return;
    do_signin(&t);
    memset(t.login_pass, 0, sizeof(t.login_pass));
    if (!t.ok) return;
    save_session();
    free(j->out);
    j->out = NULL;
    j->ok = false;
    j->signed_out = false;
    j->err[0] = 0;
    j->status = 0;
    run_once(j);
}

static void *worker_main(void *arg) {
    (void)arg;
    while (!quit) {
        Job *j = NULL;
        thr_lock(&qlock);
        if (qhead != qtail) { j = queue[qhead]; qhead = (qhead + 1) % QCAP; }
        thr_unlock(&qlock);
        if (!j) { thr_sleep_ms(15); continue; }
        run(j);
        thr_lock(&qlock);
        done_q[dtail] = j;
        dtail = (dtail + 1) % QCAP;
        thr_unlock(&qlock);
    }
    return NULL;
}

static Job *new_job(JobType t) {
    Job *j = calloc(1, sizeof(Job));
    if (j) j->type = t;
    return j;
}

static void submit(Job *j) {
    if (!j) return;
    if (j->res) { j->res->state = JS_LOADING; j->res->msg[0] = 0; }
    thr_lock(&qlock);
    if ((qtail + 1) % QCAP == qhead) {   /* full: drop it */
        thr_unlock(&qlock);
        if (j->res) { j->res->state = JS_FAILED; snprintf(j->res->msg, sizeof(j->res->msg), "Busy, try again."); }
        free(j->text);
        free(j->bits);
        free(j);
        return;
    }
    queue[qtail] = j;
    qtail = (qtail + 1) % QCAP;
    thr_unlock(&qlock);
}

/* ------------------------------------------------------------ main thread */

void juxt_init(void) {
    thr_mutex_init(&qlock);
    thr_mutex_init(&clock_);
    web_init(&wc);
    wc.on_cookie = on_cookie;
    load_session();
    load_account();
    thr_create(&worker, worker_main, NULL, 96 * 1024, 55);
}

void juxt_shutdown(void) {
    quit = 1;
}

static void forget_session(void) {
    signed_in = false;
    thr_lock(&clock_);
    memset(cookies, 0, sizeof(cookies));
    thr_unlock(&clock_);
    char path[300];
    storage_path(path, sizeof(path), "session.ini");
    remove(path);
}

static void apply(Job *j) {
    if (j->signed_out && j->type != J_SIGNIN) forget_session();
    if (j->type == J_PAGE || j->type == J_MORE) {
        JPage *pg = j->pg;
        if (pg->gen == j->gen) {
            if (j->type == J_MORE) pg->more_loading = false;
            if (!j->ok) {
                if (j->type == J_PAGE) { pg->state = JS_FAILED; snprintf(pg->error, sizeof(pg->error), "%s", j->err); }
                else pg->next[0] = 0;
            } else if (j->type == J_PAGE) {
                int gen = pg->gen;
                JPageKind k = pg->kind;
                char arg[48];
                snprintf(arg, sizeof(arg), "%s", pg->arg);
                *pg = *j->out;
                pg->gen = gen;
                pg->kind = k;
                snprintf(pg->arg, sizeof(pg->arg), "%s", arg);
                pg->state = JS_READY;
            } else {
                JPage *o = j->out;
                for (int i = 0; i < o->nposts && pg->nposts < JPAGE_POSTS; i++) {
                    bool dup = false;
                    for (int k = 0; k < pg->nposts && !dup; k++) dup = !strcmp(pg->posts[k].id, o->posts[i].id);
                    if (!dup) pg->posts[pg->nposts++] = o->posts[i];
                }
                for (int i = 0; i < o->nnews && pg->nnews < JPAGE_NEWS; i++) pg->news[pg->nnews++] = o->news[i];
                bool full = pg->nposts >= JPAGE_POSTS || pg->nnews >= JPAGE_NEWS;
                snprintf(pg->next, sizeof(pg->next), "%s", full || (o->nposts == 0 && o->nnews == 0) ? "" : o->next);
            }
        }
    } else if (j->res) {
        JResult *r = j->res;
        r->status = j->status;
        r->count = j->count;
        r->on = j->on;
        snprintf(r->msg, sizeof(r->msg), "%s", j->err);
        snprintf(r->location, sizeof(r->location), "%s", j->location);
        if ((j->type == J_SIGNIN || j->type == J_CHECK) && j->ok) {
            snprintf(my_name, sizeof(my_name), "%s", j->a);
            snprintf(my_mii, sizeof(my_mii), "%s", j->location);
            my_pid = j->count;
            signed_in = true;
            save_session();
            if (j->type == J_SIGNIN && j->login_user[0] && j->login_pass[0]) {
                thr_lock(&clock_);
                snprintf(saved_user, sizeof(saved_user), "%s", j->login_user);
                snprintf(saved_pass, sizeof(saved_pass), "%s", j->login_pass);
                thr_unlock(&clock_);
                save_account(j->login_user, j->login_pass);
            }
        }
        r->state = j->ok ? JS_READY : JS_FAILED;
    }
    memset(j->login_pass, 0, sizeof(j->login_pass));
    free(j->out);
    free(j->text);
    free(j->bits);
    free(j);
}

void juxt_poll(void) {
    for (;;) {
        Job *j = NULL;
        thr_lock(&qlock);
        if (dhead != dtail) { j = done_q[dhead]; dhead = (dhead + 1) % QCAP; }
        thr_unlock(&qlock);
        if (!j) break;
        apply(j);
    }
}

bool juxt_net_ready(void) { return net_state() == NET_UP; }
const char *juxt_net_message(void) { return net_message(); }

void juxt_load(JPage *pg, JPageKind kind, const char *arg) {
    pg->gen++;
    pg->kind = kind;
    snprintf(pg->arg, sizeof(pg->arg), "%s", arg ? arg : "");
    pg->state = JS_LOADING;
    pg->more_loading = false;
    pg->error[0] = 0;
    Job *j = new_job(J_PAGE);
    if (!j) { pg->state = JS_FAILED; snprintf(pg->error, sizeof(pg->error), "Out of memory."); return; }
    j->pg = pg;
    j->gen = pg->gen;
    j->kind = kind;
    snprintf(j->arg, sizeof(j->arg), "%s", pg->arg);
    submit(j);
}

void juxt_load_more(JPage *pg) {
    if (pg->state != JS_READY || pg->more_loading || !pg->next[0]) return;
    Job *j = new_job(J_MORE);
    if (!j) return;
    pg->more_loading = true;
    j->pg = pg;
    j->gen = pg->gen;
    j->kind = pg->kind;
    snprintf(j->arg, sizeof(j->arg), "%s", pg->next);
    submit(j);
}

void juxt_forget(JPage *pg) { pg->gen++; pg->state = JS_IDLE; }

void juxt_sign_in(JResult *r, const char *username, const char *password) {
    Job *j = new_job(J_SIGNIN);
    if (!j) return;
    j->res = r;
    snprintf(j->a, sizeof(j->a), "%s", username);
    snprintf(j->b, sizeof(j->b), "%s", password);
    submit(j);
}

void juxt_check_session(JResult *r) {
    Job *j = new_job(J_CHECK);
    if (!j) return;
    j->res = r;
    submit(j);
}

void juxt_sign_out(void) {
    forget_session();
    thr_lock(&clock_);
    memset(saved_user, 0, sizeof(saved_user));
    memset(saved_pass, 0, sizeof(saved_pass));
    thr_unlock(&clock_);
    char path[300];
    storage_path(path, sizeof(path), "account.ini");
    remove(path);
    my_name[0] = my_mii[0] = 0;
    my_pid = 0;
}

static void simple(JobType t, JResult *r, const char *a) {
    Job *j = new_job(t);
    if (!j) return;
    j->res = r;
    snprintf(j->a, sizeof(j->a), "%s", a ? a : "");
    submit(j);
}

void juxt_yeah(JResult *r, const char *post_id) { simple(J_YEAH, r, post_id); }
void juxt_follow_community(JResult *r, const char *id) { simple(J_FOLLOW_C, r, id); }
void juxt_follow_user(JResult *r, int pid) {
    char s[24];
    snprintf(s, sizeof(s), "%d", pid);
    simple(J_FOLLOW_U, r, s);
}
void juxt_unread(JResult *r) { simple(J_UNREAD, r, NULL); }

void juxt_post_text(JResult *r, const char *community_id, const char *reply_to, const char *text, int feeling,
                    bool spoiler) {
    Job *j = new_job(J_POST_TEXT);
    if (!j) return;
    j->res = r;
    snprintf(j->a, sizeof(j->a), "%s", community_id ? community_id : "");
    snprintf(j->b, sizeof(j->b), "%s", reply_to ? reply_to : "");
    j->text = strdup(text ? text : "");
    j->feeling = feeling;
    j->spoiler = spoiler;
    submit(j);
}

void juxt_post_drawing(JResult *r, const char *community_id, const char *reply_to, const uint8_t *bits,
                       int feeling, bool spoiler) {
    Job *j = new_job(J_POST_DRAW);
    if (!j) return;
    j->res = r;
    snprintf(j->a, sizeof(j->a), "%s", community_id ? community_id : "");
    snprintf(j->b, sizeof(j->b), "%s", reply_to ? reply_to : "");
    j->bits = malloc(320 * 120);
    if (j->bits) memcpy(j->bits, bits, 320 * 120);
    j->feeling = feeling;
    j->spoiler = spoiler;
    submit(j);
}
