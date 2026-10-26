/* jparse.c - reading Juxt's pages (see juxt.h). The class names come from
 * the Juxt website's views (juxtaposition-ui, views/web). */
#include "juxt.h"
#include "html.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void jp_absolute(char *url, size_t cap) {
    if (!url[0] || !strncmp(url, "http://", 7) || !strncmp(url, "https://", 8)) return;
    char tmp[512];
    if (url[0] == '/' && url[1] == '/') snprintf(tmp, sizeof(tmp), "https:%s", url);
    else snprintf(tmp, sizeof(tmp), "%s%s%s", JUXT_BASE, url[0] == '/' ? "" : "/", url);
    snprintf(url, cap, "%s", tmp);
}

static int num(const char *s) {
    while (*s && !isdigit((unsigned char)*s)) s++;
    int v = 0;
    for (; *s; s++) {
        if (isdigit((unsigned char)*s)) v = v * 10 + (*s - '0');
        else if (*s != ',') break;
    }
    return v;
}

static int pid_from(const char *href) {
    const char *p = strstr(href, "pid=");
    if (p) return atoi(p + 4);
    p = strstr(href, "/users/");
    if (p) return atoi(p + 7);
    p = strstr(href, "/mii/");
    if (p) return atoi(p + 5);
    return 0;
}

static void parse_post(const char *t, const char *end, JPost *po) {
    memset(po, 0, sizeof(*po));
    char v[256];
    if (html_attr(t, "id", v, sizeof(v)) && !strncmp(v, "post-", 5)) snprintf(po->id, sizeof(po->id), "%s", v + 5);
    po->removed = html_has_class(t, "posts-wrapper-removed");
    po->spoiler = html_has_class(t, "spoiler");
    const char *icon = html_find_class(t, end, "img", "user-icon");
    if (icon) {
        html_attr(icon, "src", po->mii, sizeof(po->mii));
        jp_absolute(po->mii, sizeof(po->mii));
        po->verified = html_has_class(icon, "verified");
    }
    const char *meta = html_find_class(t, end, "div", "post-meta-wrapper");
    if (meta) {
        const char *me = html_elem_end(meta, end);
        const char *h3 = html_find_tag(meta, me, "h3");
        if (h3) {
            const char *a = html_find_tag(h3, me, "a");
            if (a) {
                html_inner_text(a, me, po->author, sizeof(po->author));
                if (html_attr(a, "href", v, sizeof(v))) po->pid = pid_from(v);
            }
        }
        if (html_find_class(meta, me, "span", "verified-badge")) po->verified = true;
        const char *ex = html_find_class(meta, me, "p", "extra-info");
        if (ex) {
            const char *ee = html_elem_end(ex, me);
            for (const char *a = html_find_tag(ex, ee, "a"); a; a = html_find_tag(a + 1, ee, "a")) {
                if (!html_attr(a, "href", v, sizeof(v))) continue;
                if (!strncmp(v, "/posts/", 7)) {
                    html_inner_text(a, ee, po->when, sizeof(po->when));
                    if (!po->id[0]) snprintf(po->id, sizeof(po->id), "%s", v + 7);
                } else if (!strncmp(v, "/titles/", 8)) {
                    html_inner_text(a, ee, po->community, sizeof(po->community));
                    snprintf(po->community_id, sizeof(po->community_id), "%.*s", (int)strcspn(v + 8, "/?"), v + 8);
                }
            }
        }
    }
    const char *txt = html_find_class(t, end, "div", "post-content-text-container");
    if (txt) html_inner_text(txt, end, po->text, sizeof(po->text));
    const char *shot = html_find_class(t, end, "img", "screenshot");
    if (shot) { html_attr(shot, "src", po->shot, sizeof(po->shot)); jp_absolute(po->shot, sizeof(po->shot)); }
    const char *pt = html_find_class(t, end, "img", "painting");
    if (pt) { html_attr(pt, "src", po->painting, sizeof(po->painting)); jp_absolute(po->painting, sizeof(po->painting)); }
    const char *ye = html_find_class(t, end, "span", "empathy-button");
    if (ye) {
        po->yeahed = html_has_class(ye, "selected");
        const char *h4 = html_find_tag(ye, end, "h4");
        if (h4) { html_inner_text(h4, end, v, sizeof(v)); po->yeahs = num(v); }
    }
    const char *re = html_find_class(t, end, "a", "reply-button");
    if (re) {
        const char *h4 = html_find_tag(re, end, "h4");
        if (h4) { html_inner_text(h4, end, v, sizeof(v)); po->replies = num(v); }
    }
}

int jp_parse_posts(const char *html, size_t len, JPost *out, int cap, char *next, size_t next_cap) {
    const char *end = html + len, *p = html;
    int n = 0;
    if (next && next_cap) next[0] = 0;
    while (n < cap && (p = html_find_class(p, end, "div", "posts-wrapper")) != NULL) {
        const char *e = html_elem_end(p, end);
        parse_post(p, e, &out[n]);
        if (out[n].id[0] && !out[n].removed) n++;
        p = e;
    }
    const char *more = html_find_id(html, end, "load-more");
    if (more && next) {
        html_attr(more, "data-href", next, next_cap);
    }
    return n;
}

int jp_parse_communities(const char *html, size_t len, JCommunity *out, int cap, int *npopular) {
    const char *end = html + len, *p = html;
    const char *newsec = html_find_id(html, end, "new");
    int n = 0;
    if (npopular) *npopular = -1;
    char v[256];
    while (n < cap && (p = html_find_class(p, end, "a", "community-list-wrapper")) != NULL) {
        if (npopular && *npopular < 0 && newsec && p > newsec) *npopular = n;
        const char *e = html_elem_end(p, end);
        JCommunity *c = &out[n];
        memset(c, 0, sizeof(*c));
        if (html_attr(p, "href", v, sizeof(v)) && !strncmp(v, "/titles/", 8))
            snprintf(c->id, sizeof(c->id), "%.*s", (int)strcspn(v + 8, "/?"), v + 8);
        const char *img = html_find_tag(p, e, "img");
        if (img) { html_attr(img, "src", c->icon, sizeof(c->icon)); jp_absolute(c->icon, sizeof(c->icon)); }
        const char *t = html_find_class(p, e, NULL, "community-list-title");
        if (t) html_inner_text(t, e, c->name, sizeof(c->name));
        const char *f = html_find_class(p, e, NULL, "community-list-followers");
        if (f) html_inner_text(f, e, c->followers, sizeof(c->followers));
        if (c->id[0]) n++;
        p = e;
    }
    if (npopular && *npopular < 0) *npopular = newsec ? n : 0;
    return n;
}

int jp_parse_news(const char *html, size_t len, JNews *out, int cap, char *next, size_t next_cap) {
    const char *end = html + len, *p = html;
    int n = 0;
    if (next && next_cap) next[0] = 0;
    while (n < cap && (p = html_find_class(p, end, "div", "hover")) != NULL) {
        const char *e = html_elem_end(p, end);
        JNews *it = &out[n];
        memset(it, 0, sizeof(*it));
        const char *body = html_find_class(p, e, "a", "body");
        const char *icon = html_find_class(p, e, "img", "icon");
        if (icon) { html_attr(icon, "src", it->icon, sizeof(it->icon)); jp_absolute(it->icon, sizeof(it->icon)); }
        if (body) {
            html_attr(body, "href", it->link, sizeof(it->link));
            const char *be = html_elem_end(body, e);
            const char *ts = html_find_class(body, be, "span", "timestamp");
            const char *lk = html_find_class(body, be, "span", "link");
            if (ts) html_inner_text(ts, be, it->when, sizeof(it->when));
            if (lk) html_inner_text(lk, be, it->text, sizeof(it->text));
            else {
                const char *tx = html_find_class(body, be, "span", "text");
                if (tx) html_text_range(html_tag_end(tx), ts ? ts : be, it->text, sizeof(it->text));
            }
            if (it->text[0]) n++;
        }
        p = e;
    }
    const char *more = html_find_id(html, end, "load-more");
    if (more && next) html_attr(more, "data-href", next, next_cap);
    return n;
}

void jp_parse_header(const char *html, size_t len, JPage *pg) {
    const char *end = html + len;
    pg->title[0] = pg->subtitle[0] = pg->desc[0] = pg->icon[0] = pg->stat1[0] = pg->stat2[0] = 0;
    pg->following = false;
    pg->can_follow = false;
    const char *tl = html_find_class(html, end, "div", "title-line");
    if (!tl) return;
    const char *te = html_elem_end(tl, end);
    const char *img = html_find_tag(tl, te, "img");
    if (img) { html_attr(img, "src", pg->icon, sizeof(pg->icon)); jp_absolute(pg->icon, sizeof(pg->icon)); }
    const char *ti = html_find_class(tl, te, "div", "title");
    if (ti) {
        char t[160];
        html_inner_text(ti, te, t, sizeof(t));
        /* a person: "Mii Name @username v" */
        char *at = strstr(t, " @");
        if (at) {
            *at = 0;
            snprintf(pg->subtitle, sizeof(pg->subtitle), "@%.*s", (int)strcspn(at + 2, " "), at + 2);
        }
        snprintf(pg->title, sizeof(pg->title), "%.79s", t);
    }
    const char *fb = html_find_class(tl, te, "a", "follow-button");
    if (fb) { pg->can_follow = true; pg->following = html_has_class(fb, "selected"); }
    const char *de = html_find_class(te, end, "div", "description");
    if (de) html_inner_text(de, end, pg->desc, sizeof(pg->desc));
    /* stat boxes: value + name pairs */
    const char *p = te;
    int k = 0;
    while (k < 4 && (p = html_find_class(p, end, "div", "value")) != NULL) {
        const char *ve = html_elem_end(p, end);
        char val[40], name[40] = "";
        html_inner_text(p, end, val, sizeof(val));
        const char *nm = html_find_class(ve, end, "div", "name");
        if (nm) html_inner_text(nm, end, name, sizeof(name));
        if (strstr(name, "ollower")) snprintf(pg->stat1, sizeof(pg->stat1), "%.20s followers", val);
        else if (strstr(name, "ost") && !pg->stat2[0]) snprintf(pg->stat2, sizeof(pg->stat2), "%.20s posts", val);
        else if (strstr(name, "ountry") && !pg->stat2[0]) snprintf(pg->stat2, sizeof(pg->stat2), "%.38s", val);
        p = ve;
        k++;
    }
}

void jp_parse_toast(const char *html, size_t len, char *out, size_t cap) {
    const char *t = html_find_id(html, html + len, "toast");
    out[0] = 0;
    if (t) html_inner_text(t, html + len, out, cap);
}
