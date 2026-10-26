#include "juxt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *rd(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); fseek(f, 0, 2); *n = ftell(f); rewind(f); char *b = malloc(*n + 1); fread(b, 1, *n, f); b[*n] = 0; fclose(f); return b; }
static JPage pg;
int main(void) {
    size_t n; char *h; char next[200];
    JPost posts[60];
    h = rd("pages/feed.html", &n);
    int k = jp_parse_posts(h, n, posts, 60, next, sizeof(next));
    printf("feed %d next=%s\n", k, next);
    for (int i = 0; i < k; i++) printf(" [%s] %s pid=%d v=%d when='%s' comm='%s'(%s) y=%d%s r=%d sp=%d\n   text='%s' paint=%s shot=%s mii=%s\n", posts[i].id, posts[i].author, posts[i].pid, posts[i].verified, posts[i].when, posts[i].community, posts[i].community_id, posts[i].yeahs, posts[i].yeahed ? "*" : "", posts[i].replies, posts[i].spoiler, posts[i].text, posts[i].painting, posts[i].shot, posts[i].mii);
    h = rd("pages/feed_more.html", &n); k = jp_parse_posts(h, n, posts, 60, next, sizeof(next)); printf("more %d next=%s\n", k, next);
    JCommunity c[100]; int np;
    h = rd("pages/communities.html", &n); k = jp_parse_communities(h, n, c, 100, &np);
    printf("communities %d popular=%d\n", k, np); for (int i = 0; i < k; i++) printf(" %s '%s' '%s' %s\n", c[i].id, c[i].name, c[i].followers, c[i].icon);
    h = rd("pages/community.html", &n); jp_parse_header(h, n, &pg); k = jp_parse_posts(h, n, posts, 60, next, sizeof(next));
    printf("community '%s' sub='%s' desc='%s' icon=%s s1='%s' s2='%s' follow=%d/%d posts=%d next=%s\n", pg.title, pg.subtitle, pg.desc, pg.icon, pg.stat1, pg.stat2, pg.can_follow, pg.following, k, next);
    h = rd("pages/user.html", &n); jp_parse_header(h, n, &pg); k = jp_parse_posts(h, n, posts, 60, next, sizeof(next));
    printf("user '%s' sub='%s' desc='%s' icon=%s s1='%s' s2='%s' follow=%d posts=%d\n", pg.title, pg.subtitle, pg.desc, pg.icon, pg.stat1, pg.stat2, pg.can_follow, k);
    h = rd("pages/post.html", &n); k = jp_parse_posts(h, n, posts, 60, next, sizeof(next)); printf("post page %d first=%s\n", k, posts[0].id);
    JNews ne[40]; h = rd("pages/news.html", &n); k = jp_parse_news(h, n, ne, 40, next, sizeof(next));
    printf("news %d\n", k); for (int i = 0; i < k; i++) printf(" '%s' | '%s' | %s | %s\n", ne[i].text, ne[i].when, ne[i].link, ne[i].icon);
    char t[200]; h = rd("pages/login_bad.html", &n); jp_parse_toast(h, n, t, sizeof(t)); printf("toast '%s'\n", t);
}
