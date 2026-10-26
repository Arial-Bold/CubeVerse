/* html.c - see html.h. */
#include "html.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool name_is(const char *p, const char *name) {
    size_t n = strlen(name);
    return !strncasecmp(p, name, n) && !(isalnum((unsigned char)p[n]) || p[n] == '-');
}

const char *html_tag_end(const char *t) {
    char q = 0;
    for (const char *p = t + 1; *p; p++) {
        if (q) { if (*p == q) q = 0; continue; }
        if (*p == '"' || *p == '\'') q = *p;
        else if (*p == '>') return p + 1;
    }
    return t + strlen(t);
}

/* the raw value of attribute name in the start tag at t: [*a, *b) */
static bool attr_span(const char *t, const char *name, const char **a, const char **b) {
    const char *end = html_tag_end(t);
    const char *p = t + 1;
    while (p < end && !isspace((unsigned char)*p) && *p != '>') p++;   /* skip the tag name */
    size_t n = strlen(name);
    while (p < end) {
        while (p < end && (isspace((unsigned char)*p) || *p == '/')) p++;
        if (p >= end || *p == '>') break;
        const char *ns = p;
        while (p < end && !isspace((unsigned char)*p) && *p != '=' && *p != '>') p++;
        size_t nl = (size_t)(p - ns);
        const char *vs = NULL, *ve = NULL;
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p < end && *p == '=') {
            p++;
            while (p < end && isspace((unsigned char)*p)) p++;
            if (*p == '"' || *p == '\'') {
                char q = *p++;
                vs = p;
                while (p < end && *p != q) p++;
                ve = p;
                if (p < end) p++;
            } else {
                vs = p;
                while (p < end && !isspace((unsigned char)*p) && *p != '>') p++;
                ve = p;
            }
        }
        if (nl == n && !strncasecmp(ns, name, n)) {
            *a = vs ? vs : ns + nl;
            *b = ve ? ve : ns + nl;
            return true;
        }
    }
    return false;
}

bool html_attr(const char *t, const char *name, char *out, size_t cap) {
    const char *a, *b;
    if (!cap) return false;
    out[0] = 0;
    if (!attr_span(t, name, &a, &b)) return false;
    size_t n = (size_t)(b - a);
    if (n >= cap) n = cap - 1;
    memcpy(out, a, n);
    out[n] = 0;
    html_decode(out);
    return true;
}

static bool list_has(const char *a, const char *b, const char *word) {
    size_t n = strlen(word);
    const char *p = a;
    while (p < b) {
        while (p < b && isspace((unsigned char)*p)) p++;
        const char *s = p;
        while (p < b && !isspace((unsigned char)*p)) p++;
        if ((size_t)(p - s) == n && !strncmp(s, word, n)) return true;
    }
    return false;
}

bool html_has_class(const char *t, const char *cls) {
    const char *a, *b;
    return attr_span(t, "class", &a, &b) && list_has(a, b, cls);
}

const char *html_find_tag(const char *p, const char *end, const char *tag) {
    while (p && p < end) {
        const char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt) return NULL;
        if (name_is(lt + 1, tag)) return lt;
        p = lt + 1;
    }
    return NULL;
}

const char *html_find_class(const char *p, const char *end, const char *tag, const char *cls) {
    while (p && p < end) {
        const char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt || lt + 1 >= end) return NULL;
        char c1 = lt[1];
        if (c1 != '/' && c1 != '!' && (!tag || name_is(lt + 1, tag)) && html_has_class(lt, cls)) return lt;
        p = lt + 1;
    }
    return NULL;
}

const char *html_find_id(const char *p, const char *end, const char *id) {
    char v[128];
    while (p && p < end) {
        const char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt || lt + 1 >= end) return NULL;
        if (lt[1] != '/' && lt[1] != '!' && html_attr(lt, "id", v, sizeof(v)) && !strcmp(v, id)) return lt;
        p = lt + 1;
    }
    return NULL;
}

static const char *const VOIDS[] = { "img", "br", "input", "meta", "link", "hr", "source", "wbr", "area", "col", NULL };

static int tag_name(const char *t, char *out, int cap) {
    int n = 0;
    const char *p = t + 1;
    if (*p == '/') p++;
    while ((isalnum((unsigned char)*p) || *p == '-') && n + 1 < cap) out[n++] = (char)tolower((unsigned char)*p++);
    out[n] = 0;
    return n;
}

const char *html_elem_end(const char *t, const char *end) {
    char name[24];
    tag_name(t, name, sizeof(name));
    const char *te = html_tag_end(t);
    if (te >= t + 2 && te[-2] == '/') return te;
    for (int i = 0; VOIDS[i]; i++) if (!strcmp(name, VOIDS[i])) return te;
    int depth = 1;
    const char *p = te;
    while (p < end) {
        const char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt) return end;
        if (!strncmp(lt, "<!--", 4)) {
            const char *ce = strstr(lt + 4, "-->");
            p = ce ? ce + 3 : end;
            continue;
        }
        char n2[24];
        tag_name(lt, n2, sizeof(n2));
        const char *ne = html_tag_end(lt);
        if (!strcmp(n2, name)) {
            if (lt[1] == '/') { if (--depth == 0) return ne; }
            else if (!(ne >= lt + 2 && ne[-2] == '/')) depth++;
        }
        p = ne;
    }
    return end;
}

/* --- text --- */

static void put_utf8(char **o, char *oe, unsigned cp) {
    /* fold to what the fonts have */
    const char *rep = NULL;
    char one[2] = { 0, 0 };
    if (cp < 128) { one[0] = (char)cp; rep = one; }
    else if (cp == 0xA0) rep = " ";
    else if (cp == 0x2018 || cp == 0x2019 || cp == 0x2032) rep = "'";
    else if (cp == 0x201C || cp == 0x201D) rep = "\"";
    else if (cp == 0x2013 || cp == 0x2014) rep = "-";
    else if (cp == 0x2026) rep = "...";
    else if (cp == 0xB7 || cp == 0x2022 || cp == 0x30FB) rep = "\x7f";
    else if (cp == 0xD7) rep = "\x80";
    else if (cp == 0x2605 || cp == 0x2606) rep = "\x84";
    else if (cp == 0x266A || cp == 0x266B) rep = "\x83";
    else if (cp == 0x2713 || cp == 0x2714) rep = "";   /* the verified tick (shown as its own badge) */
    else if (cp >= 0xC0 && cp <= 0x17F) {
        static const char *lat = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy"
                                 "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
        one[0] = lat[cp - 0xC0];
        rep = one;
    } else if (cp >= 0x1F000 || (cp >= 0x2600 && cp <= 0x27BF) || cp == 0xFE0F || cp == 0x200D) rep = "";   /* emoji */
    else rep = "?";
    while (*rep && *o < oe) *(*o)++ = *rep++;
}

static unsigned entity(const char **pp) {
    const char *p = *pp;   /* at '&' */
    static const struct { const char *n; unsigned c; } E[] = {
        { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' }, { "quot;", '"' }, { "apos;", '\'' }, { "nbsp;", 0xA0 },
        { "hellip;", 0x2026 }, { "mdash;", 0x2014 }, { "ndash;", 0x2013 }, { "rsquo;", 0x2019 }, { "lsquo;", 0x2018 },
        { "ldquo;", 0x201C }, { "rdquo;", 0x201D }, { "middot;", 0xB7 }, { "times;", 0xD7 }, { "eacute;", 0xE9 },
    };
    if (p[1] == '#') {
        const char *q = p + 2;
        unsigned v = 0;
        bool hex = (*q == 'x' || *q == 'X');
        if (hex) q++;
        const char *s = q;
        while (hex ? isxdigit((unsigned char)*q) : isdigit((unsigned char)*q)) {
            v = v * (hex ? 16 : 10) + (unsigned)(isdigit((unsigned char)*q) ? *q - '0' : (tolower((unsigned char)*q) - 'a' + 10));
            q++;
        }
        if (q > s) {
            if (*q == ';') q++;
            *pp = q;
            return v;
        }
    } else {
        for (unsigned i = 0; i < sizeof(E) / sizeof(E[0]); i++) {
            size_t n = strlen(E[i].n);
            if (!strncmp(p + 1, E[i].n, n)) { *pp = p + 1 + n; return E[i].c; }
        }
    }
    *pp = p + 1;
    return '&';
}

static unsigned utf8_next(const char **pp) {
    const unsigned char *p = (const unsigned char *)*pp;
    unsigned c = *p;
    int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (n < 0) { *pp += 1; return '?'; }
    unsigned v = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
    for (int i = 1; i <= n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *pp += i; return '?'; }
        v = (v << 6) | (p[i] & 0x3F);
    }
    *pp += n + 1;
    return v;
}

void html_decode(char *s) {
    size_t n = strlen(s);
    char *tmp = malloc(n + 1);
    if (!tmp) return;
    char *o = tmp, *oe = tmp + n;
    const char *p = s;
    while (*p) {
        unsigned cp = *p == '&' ? entity(&p) : utf8_next(&p);
        put_utf8(&o, oe, cp);
    }
    *o = 0;
    memcpy(s, tmp, (size_t)(o - tmp) + 1);
    free(tmp);
}

void html_text_range(const char *a, const char *b, char *out, size_t cap) {
    if (!cap) return;
    char *o = out, *oe = out + cap - 1;
    bool space = true;   /* drop leading spaces */
    const char *p = a;
    while (p < b && o < oe) {
        if (*p == '<') {
            if (!strncmp(p, "<!--", 4)) {
                const char *ce = strstr(p + 4, "-->");
                p = ce ? ce + 3 : b;
                continue;
            }
            char name[16];
            tag_name(p, name, sizeof(name));
            bool closing = p[1] == '/';
            const char *te = html_tag_end(p);
            if (!closing && (!strcmp(name, "script") || !strcmp(name, "style"))) { p = html_elem_end(p, b); continue; }
            if (!strcmp(name, "br") || (closing && (!strcmp(name, "p") || !strcmp(name, "div") || !strcmp(name, "li")))) {
                while (o > out && o[-1] == ' ') o--;
                if (o > out && o[-1] != '\n') *o++ = '\n';
                space = true;
            }
            p = te;
            continue;
        }
        unsigned cp;
        if (*p == '&') cp = entity(&p);
        else cp = utf8_next(&p);
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0) {
            if (!space) { *o++ = ' '; space = true; }
            continue;
        }
        space = false;
        put_utf8(&o, oe, cp);
    }
    while (o > out && (o[-1] == ' ' || o[-1] == '\n')) o--;
    *o = 0;
    /* runs of new lines: at most one blank line */
    char *w = out;
    int nl = 0;
    for (char *r = out; *r; r++) {
        if (*r == '\n') { if (++nl > 2) continue; }
        else nl = 0;
        *w++ = *r;
    }
    *w = 0;
}

void html_inner_text(const char *t, const char *end, char *out, size_t cap) {
    const char *a = html_tag_end(t);
    const char *e = html_elem_end(t, end);
    /* back up over the closing tag */
    const char *b = e;
    if (b > a && b[-1] == '>') {
        const char *lt = b - 1;
        while (lt > a && *lt != '<') lt--;
        if (*lt == '<' && lt[1] == '/') b = lt;
    }
    html_text_range(a, b, out, cap);
}
