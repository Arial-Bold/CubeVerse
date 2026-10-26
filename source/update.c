/* update.c - see update.h. Adapted from CubeBrowser's updater. */
#include "update.h"
#include "version.h"
#include "storage.h"
#include "settings.h"
#include "dolload.h"
#include "unzip.h"
#include "net.h"
#include "thr.h"
#include "applog.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef GEKKO
#include "https.h"
#include "netio.h"
#endif

static volatile UpdateState state = UPD_NONE;
static volatile UpdateStep step;
static volatile bool busy, quiet;
static volatile int cancel_flag;
static char latest[32];               /* "1.1" */
static char asset_url[1024];
static char notes[4000];
static char err_msg[256];
static char target[300];              /* the .dol being replaced */
static char program[300];             /* argv[0], when it's on our card */
#ifdef GEKKO
static HttpsProgress dl_prog;
#endif
static UnzipProgress unzip_prog;

UpdateState update_state(void) { return state; }
UpdateStep update_step(void) { return step; }
const char *update_latest_version(void) { return latest; }
const char *update_notes(void) { return notes; }
const char *update_error(void) { return err_msg; }
bool update_was_quiet(void) { return quiet; }

float update_progress(void) {
#ifdef GEKKO
    if (step == UPD_STEP_DOWNLOAD && dl_prog.total) return (float)dl_prog.done / (float)dl_prog.total;
#endif
    if (step == UPD_STEP_UNPACK && unzip_prog.total) return (float)unzip_prog.done / (float)unzip_prog.total;
    return -1;
}

void update_set_program(const char *argv0) {
    program[0] = 0;
    if (!argv0 || !*argv0) return;
    const char *colon = strstr(argv0, ":/");
    const char *path = colon ? colon + 1 : argv0;
    if (path[0] != '/') return;
    char full[300];
    if (snprintf(full, sizeof(full), "sd:%s", path) >= (int)sizeof(full)) return;
    if (storage_is_file(full)) snprintf(program, sizeof(program), "%s", full);
}

/* ------------------------------------------------------------ versions */

/* Reads up to 4 dotted numbers from s ("v1.10.2" -> 1,10,2,0). Returns how many. */
static int parse_version(const char *s, int out[4]) {
    memset(out, 0, sizeof(int) * 4);
    while (*s && !isdigit((unsigned char)*s)) s++;
    int n = 0;
    while (*s && n < 4 && isdigit((unsigned char)*s)) {
        out[n++] = atoi(s);
        while (isdigit((unsigned char)*s)) s++;
        if (*s != '.') break;
        s++;
    }
    return n;
}

static int compare_versions(const int a[4], const int b[4]) {
    for (int i = 0; i < 4; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* ------------------------------------------------------------ tiny JSON reading */

static const char *json_string(const char *p, char *out, size_t cap) {
    size_t n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            char e = *p++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = ' '; break;
            case 'u': {   /* \uXXXX: keep ASCII and a few punctuation marks */
                unsigned v = 0;
                for (int i = 0; i < 4 && isxdigit((unsigned char)*p); i++, p++)
                    v = v * 16 + (unsigned)(isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10));
                c = (v >= 32 && v < 127) ? (char)v
                    : (v == 0x2019 || v == 0x2018) ? '\''
                    : (v == 0x201c || v == 0x201d) ? '"'
                    : (v == 0x2013 || v == 0x2014) ? '-'
                    : (v == 0x00b7 || v == 0x2022) ? '\x7f' : '?';
                break;
            }
            default: c = e; break;
            }
        } else if ((unsigned char)c >= 0x80) {
            while (((unsigned char)*p & 0xC0) == 0x80) p++;
            c = '?';
        }
        if (n + 1 < cap) out[n++] = c;
    }
    if (cap) out[n < cap ? n : cap - 1] = 0;
    return *p == '"' ? p + 1 : p;
}

static bool json_find_string(const char *json, const char *key, char *out, size_t cap, const char **after) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p != '"') return false;
    p = json_string(p + 1, out, cap);
    if (after) *after = p;
    return true;
}

/* Release notes are Markdown: tidy the common bits for plain display. */
static void tidy_notes(char *s) {
    char *w = s;
    bool line_start = true;
    for (char *r = s; *r; r++) {
        if (*r == '\r') continue;
        if (line_start) {
            while (*r == '#') r++;
            while (*r == ' ') r++;
            if ((*r == '*' || *r == '-') && r[1] == ' ') { *w++ = '\x7f'; r++; }   /* bullets */
        }
        if (!*r) break;
        if (*r == '*' || *r == '`' || (*r == '_' && r[1] == '_')) { if (*r == '_') r++; line_start = false; continue; }
        *w++ = *r;
        line_start = (*r == '\n');
    }
    *w = 0;
    while (w > s && (w[-1] == '\n' || w[-1] == ' ')) *--w = 0;
}

/* ------------------------------------------------------------ files */

static bool copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb");
    if (!in) return false;
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); return false; }
    char *buf = malloc(32 * 1024);
    bool ok = buf != NULL;
    size_t n;
    while (ok && (n = fread(buf, 1, 32 * 1024, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) ok = false;
    if (ferror(in)) ok = false;
    free(buf);
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok) remove(to);
    return ok;
}

static void remove_tree(const char *path, int depth) {
    char open_path[330];
    snprintf(open_path, sizeof(open_path), "%s/", path);
    DIR *d = opendir(open_path);
    if (d) {
        char (*names)[256] = malloc(sizeof(*names) * 64);
        int count = 0;
        struct dirent *e;
        while (names && (e = readdir(d)) != NULL && count < 64)
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) snprintf(names[count++], 256, "%s", e->d_name);
        closedir(d);
        for (int i = 0; i < count; i++) {
            char sub[600];
            snprintf(sub, sizeof(sub), "%s/%s", path, names[i]);
            if (storage_is_file(sub)) remove(sub);
            else if (depth < 4) remove_tree(sub, depth + 1);
        }
        free(names);
    }
    rmdir(path);
}

/* The .dol inside the unpacked update: cubeverse.dol at the top, or any .dol. */
static bool find_new_dol(const char *dir, char *out, size_t cap) {
    snprintf(out, cap, "%s/cubeverse.dol", dir);
    if (storage_is_file(out)) return true;
    char open_path[330];
    snprintf(open_path, sizeof(open_path), "%s/", dir);
    DIR *d = opendir(open_path);
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n > 4 && !strcasecmp(e->d_name + n - 4, ".dol")) {
            snprintf(out, cap, "%s/%s", dir, e->d_name);
            found = storage_is_file(out);
        }
    }
    closedir(d);
    return found;
}

/* Which .dol to replace: the one we were started from, if the loader said;
 * otherwise the only cubeverse*.dol on the card. */
static bool find_target(char *out, size_t cap) {
    if (program[0]) { snprintf(out, cap, "%s", program); return true; }
    static char found[48][256];
    int n = dol_find("sd:/", found, 48), match = -1, matches = 0;
    for (int i = 0; i < n; i++) {
        const char *name = strrchr(found[i], '/');
        name = name ? name + 1 : found[i];
        if (!strncasecmp(name, "cubeverse", 9) && !strstr(name, ".old") && !strstr(name, ".new")) { match = i; matches++; }
    }
    if (matches == 1) { snprintf(out, cap, "%s", found[match]); return true; }
    return false;
}

/* ------------------------------------------------------------ the worker */

#ifdef GEKKO

/* Starts the network (the same one Transfer uses) and waits for it. */
static bool network_up(void) {
    step = UPD_STEP_NETWORK;
    net_start();
    for (int i = 0; i < 400 && net_state() == NET_STARTING; i++) {
        if (cancel_flag) return false;
        thr_sleep_ms(100);
    }
    if (net_state() != NET_UP) {
        snprintf(err_msg, sizeof(err_msg), "%s", net_message());
        return false;
    }
    if (!netio_time_synced()) netio_sync_time();   /* certificates are checked against the date */
    return true;
}

static void do_check(void) {
    char url[200];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", UPDATE_REPO);
    char *body = NULL, err[256] = "";
    int status = https_get(url, "application/vnd.github+json", &body, NULL, 512 * 1024, &cancel_flag, err, sizeof(err));
    if (status < 0) {
        snprintf(err_msg, sizeof(err_msg), "%s", err[0] ? err : "GitHub couldn't be reached.");
        free(body);
        state = UPD_CHECK_FAILED;
        return;
    }
    if (status == 404) snprintf(err_msg, sizeof(err_msg), "No versions have been published yet.");
    else if (status == 403 || status == 429) snprintf(err_msg, sizeof(err_msg), "GitHub is limiting checks from your network. Try again in an hour.");
    else if (status != 200 || !body) snprintf(err_msg, sizeof(err_msg), "GitHub answered with error %d.", status);
    if (status != 200 || !body) { free(body); state = UPD_CHECK_FAILED; return; }
    char tag[64] = "";
    json_find_string(body, "tag_name", tag, sizeof(tag), NULL);
    int have[4], got[4];
    parse_version(CV_VERSION, have);
    if (parse_version(tag, got) == 0) {
        snprintf(latest, sizeof(latest), "%s", tag);
        free(body);
        state = UPD_LATEST;
        return;
    }
    const char *t = tag;
    while (*t && !isdigit((unsigned char)*t)) t++;
    snprintf(latest, sizeof(latest), "%s", t);
    notes[0] = 0;
    json_find_string(body, "body", notes, sizeof(notes), NULL);
    tidy_notes(notes);
    asset_url[0] = 0;
    const char *p = body;
    char link[1024];
    while (json_find_string(p, "browser_download_url", link, sizeof(link), &p)) {
        size_t n = strlen(link);
        if (n > 4 && !strcasecmp(link + n - 4, ".zip")) { snprintf(asset_url, sizeof(asset_url), "%s", link); break; }
        if (n > 4 && !strcasecmp(link + n - 4, ".dol") && !asset_url[0]) snprintf(asset_url, sizeof(asset_url), "%s", link);
    }
    free(body);
    if (compare_versions(got, have) <= 0) { state = UPD_LATEST; return; }
    if (!asset_url[0]) {
        snprintf(err_msg, sizeof(err_msg), "Version %s has no download attached yet.", latest);
        state = UPD_CHECK_FAILED;
        return;
    }
    state = UPD_AVAILABLE;
}

static void install_failed(const char *msg) {
    snprintf(err_msg, sizeof(err_msg), "%s", msg);
    state = UPD_FAILED;
}

static void do_install(void) {
    char zip[400], dir[400], newdol[600], err[256];
    storage_path(zip, sizeof(zip), "update.zip");
    storage_path(dir, sizeof(dir), "update");
    if (!find_target(target, sizeof(target))) {
        install_failed("Couldn't tell which file is CubeVerse on the SD card. Download the zip from GitHub on a computer instead.");
        return;
    }
    /* 1. download */
    step = UPD_STEP_DOWNLOAD;
    size_t ul = strlen(asset_url);
    bool bare_dol = ul > 4 && !strcasecmp(asset_url + ul - 4, ".dol");
    if (bare_dol) storage_path(zip, sizeof(zip), "update.dol");
    remove(zip);
    app_log("update: downloading %.120s", asset_url);
    if (!https_download(asset_url, zip, &dl_prog, &cancel_flag, err, sizeof(err))) {
        char m[300];
        snprintf(m, sizeof(m), "%s Nothing was changed.", cancel_flag ? "Stopped." : err);
        install_failed(m);
        return;
    }
    /* 2. unpack and check */
    step = UPD_STEP_UNPACK;
    remove_tree(dir, 0);
    if (bare_dol) {
        snprintf(newdol, sizeof(newdol), "%s", zip);
    } else {
        memset(&unzip_prog, 0, sizeof(unzip_prog));
        if (!unzip_file(zip, dir, &unzip_prog, &cancel_flag, err, sizeof(err))) {
            remove(zip);
            remove_tree(dir, 0);
            char m[300];
            snprintf(m, sizeof(m), "The download couldn't be unpacked (%s). Nothing was changed.", err);
            install_failed(m);
            return;
        }
        if (!find_new_dol(dir, newdol, sizeof(newdol))) {
            remove(zip);
            remove_tree(dir, 0);
            install_failed("The download didn't contain a .dol file. Nothing was changed.");
            return;
        }
    }
    if (!dol_check(newdol, err, sizeof(err))) {
        char m[300];
        snprintf(m, sizeof(m), "The downloaded program looks damaged (%s). Nothing was changed.", err);
        remove(zip);
        remove_tree(dir, 0);
        install_failed(m);
        return;
    }
    /* 3. replace: new -> <target>.new, <target> -> <target>.old, <target>.new -> <target> */
    step = UPD_STEP_REPLACE;
    char tnew[320], told[320];
    snprintf(tnew, sizeof(tnew), "%s.new", target);
    snprintf(told, sizeof(told), "%s.old", target);
    remove(tnew);
    if (rename(newdol, tnew) != 0 && !copy_file(newdol, tnew)) {
        remove(zip);
        remove_tree(dir, 0);
        install_failed("Couldn't write to the SD card (is it full or locked?). Nothing was changed.");
        return;
    }
    remove(told);
    bool had_old = storage_is_file(target);
    if (had_old && rename(target, told) != 0) {
        remove(tnew);
        remove(zip);
        remove_tree(dir, 0);
        install_failed("Couldn't replace the old version. Nothing was changed.");
        return;
    }
    if (rename(tnew, target) != 0) {
        if (had_old) rename(told, target);
        remove(tnew);
        remove(zip);
        remove_tree(dir, 0);
        install_failed("Couldn't put the new version in place. The old one was kept.");
        return;
    }
    /* the certificates and readme from the release (settings and photos are left alone) */
    static const char *const extras[] = { "cacert.pem", "README.txt" };
    for (int i = 0; i < 2 && !bare_dol; i++) {
        char from[600], to[400], tmp[420];
        snprintf(from, sizeof(from), "%s/cubeverse/%s", dir, extras[i]);
        storage_path(to, sizeof(to), extras[i]);
        snprintf(tmp, sizeof(tmp), "%s.tmp", to);
        if (storage_is_file(from) && copy_file(from, tmp)) { remove(to); rename(tmp, to); }
    }
    remove(zip);
    remove_tree(dir, 0);
    app_log("update: installed %s as %s", latest, target);
    state = UPD_INSTALLED;
}

static volatile bool job_install;

static void *worker(void *arg) {
    (void)arg;
    if (!network_up()) {
        if (cancel_flag) snprintf(err_msg, sizeof(err_msg), "Stopped. Nothing was changed.");
        state = job_install ? UPD_FAILED : UPD_CHECK_FAILED;
    } else if (job_install) {
        do_install();
    } else {
        do_check();
    }
    busy = false;
    return NULL;
}

static void start_job(bool install) {
    thr_t t;
    job_install = install;
    cancel_flag = 0;
    busy = true;
    if (thr_create(&t, worker, NULL, 64 * 1024, 60)) {
        thr_detach(t);
    } else {
        busy = false;
        snprintf(err_msg, sizeof(err_msg), "Couldn't start the update.");
        state = install ? UPD_FAILED : UPD_CHECK_FAILED;
    }
}

#else   /* PC test build: no network. FC_FAKE_UPDATE shows an update for screenshots. */

static void start_job(bool install) {
    if (getenv("FC_FAKE_UPDATE")) {
        snprintf(latest, sizeof(latest), "1.2");
        snprintf(notes, sizeof(notes),
                 "What's new in CubeVerse 1.2\n\n\x7f Background music in the menus, made by the GameCube as it plays\n"
                 "\x7f Software updates from GitHub, right from Settings\n\x7f Screen margin and 16:9 layouts\n"
                 "\x7f Long names scroll\n\nFixes\n\x7f Outlines no longer disappear on some TVs\n"
                 "\x7f The slideshow shading reaches the edges of the screen");
        state = install ? UPD_INSTALLED : UPD_AVAILABLE;
        if (install) snprintf(target, sizeof(target), "sd:/cubeverse.dol");
    } else {
        snprintf(err_msg, sizeof(err_msg), "Updates work on a GameCube.");
        state = install ? UPD_FAILED : UPD_CHECK_FAILED;
    }
}

#endif

/* ------------------------------------------------------------ main thread */

void update_request_check(bool q) {
    if (busy || state == UPD_INSTALLING || state == UPD_INSTALLED) return;
    quiet = q;
    err_msg[0] = 0;
    state = UPD_CHECKING;
    start_job(false);
}

void update_request_install(void) {
    if (busy || (state != UPD_AVAILABLE && state != UPD_FAILED)) return;
    if (!storage_ok()) { snprintf(err_msg, sizeof(err_msg), "No SD card was found."); state = UPD_FAILED; return; }
    quiet = false;
    err_msg[0] = 0;
    step = UPD_STEP_DOWNLOAD;
    state = UPD_INSTALLING;
    start_job(true);
}

void update_cancel(void) { cancel_flag = 1; }

void update_poll(void) {
    /* a failed automatic check just tries again next time CubeVerse starts */
    if (!busy && quiet && state == UPD_CHECK_FAILED) state = UPD_NONE;
}

bool update_restart(char *err, size_t errlen) {
    if (state != UPD_INSTALLED) { snprintf(err, errlen, "Nothing to restart into yet."); return false; }
    settings_save();
    return dol_launch(target, err, errlen);
}
