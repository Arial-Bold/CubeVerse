/* juxt.h - CubeVerse's connection to Juxtaposition (Pretendo Network's
 * Miiverse), through the same web pages the Juxt website uses.
 *
 * Everything that touches the network runs on a worker thread; screens ask
 * for a page or an action and check its state each frame. Finished work is
 * handed over in juxt_poll(), on the main thread, so a screen's data never
 * changes while it is being drawn. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JUXT_HOST "juxt.pretendo.network"
#define JUXT_BASE "https://" JUXT_HOST

typedef struct {
    char id[40];
    char author[48];
    char username[40];
    int  pid;
    bool verified;
    char mii[200];            /* the poster's Mii picture */
    char when[32];            /* "2 hours ago" */
    char community[64];
    char community_id[24];
    char text[600];
    char painting[200];       /* picture addresses, or "" */
    char shot[200];
    int  yeahs, replies;
    bool yeahed, spoiler, removed;
} JPost;

typedef struct {
    char id[24];
    char name[64];
    char followers[40];       /* "1,204 followers" */
    char icon[200];
} JCommunity;

typedef struct {
    char text[200];
    char when[32];
    char link[120];           /* "/posts/..." or "/users/..." */
    char icon[200];
} JNews;

typedef enum {
    JP_NONE,
    JP_FEED,                  /* following */
    JP_FEED_PEOPLE,
    JP_FEED_ALL,              /* everyone */
    JP_COMMUNITIES,           /* popular and new */
    JP_COMMUNITIES_ALL,
    JP_COMMUNITY_NEW,         /* arg: community id */
    JP_COMMUNITY_HOT,
    JP_POST,                  /* arg: post id; posts[0] is the post, the rest replies */
    JP_USER_POSTS,            /* arg: pid or "me" */
    JP_USER_YEAHS,
    JP_NEWS,
} JPageKind;

enum { JS_IDLE, JS_LOADING, JS_READY, JS_FAILED };

#define JPAGE_POSTS 60
#define JPAGE_COMMUNITIES 100
#define JPAGE_NEWS 40

typedef struct {
    JPageKind kind;
    char arg[48];
    int  state;               /* JS_* */
    bool more_loading;
    char error[160];
    int  gen;                 /* bumped by each new request */
    /* lists */
    JPost posts[JPAGE_POSTS];
    int  nposts;
    JCommunity comms[JPAGE_COMMUNITIES];
    int  ncomms;
    int  npopular;            /* JP_COMMUNITIES: the first npopular are "popular", then "new" */
    JNews news[JPAGE_NEWS];
    int  nnews;
    char next[200];           /* address of the next part of the list, or "" */
    /* page header: a community or a person */
    char title[80];           /* community name, or Mii name */
    char subtitle[64];        /* "@username" */
    char desc[400];
    char icon[200];
    char stat1[40], stat2[40];   /* followers, posts (or country) */
    bool following;           /* the follow button is on */
    bool can_follow;
    int  pid;                 /* JP_USER_*: whose page */
} JPage;

typedef struct {
    volatile int state;       /* JS_* */
    int  status;              /* HTTP status, or 0 */
    int  count;               /* Yeahs / followers after a Yeah or follow */
    bool on;                  /* now Yeah'd / following */
    char msg[200];            /* a problem, for the screen */
    char location[256];       /* where a new post went */
} JResult;

void juxt_init(void);
void juxt_poll(void);                    /* main thread, once a frame */
void juxt_shutdown(void);

/* the network adapter */
bool juxt_net_ready(void);
const char *juxt_net_message(void);

/* signing in: the session (cookies) and, so CubeVerse can sign in again
 * by itself, the ID and password are kept on the SD card */
bool juxt_signed_in(void);
const char *juxt_my_name(void);
int  juxt_my_pid(void);
const char *juxt_my_mii(void);
void juxt_sign_in(JResult *r, const char *username, const char *password);
void juxt_check_session(JResult *r);     /* is the saved session still good? */
void juxt_sign_out(void);              /* also forgets the saved ID and password */
/* The saved Pretendo ID and password (kept after a good sign-in), if any. */
bool juxt_saved_login(char *user, size_t ucap, char *pass, size_t pcap);

/* pages */
void juxt_load(JPage *pg, JPageKind kind, const char *arg);
void juxt_load_more(JPage *pg);
void juxt_forget(JPage *pg);             /* drop any pending answer for pg */

/* actions */
void juxt_yeah(JResult *r, const char *post_id);
void juxt_follow_community(JResult *r, const char *community_id);
void juxt_follow_user(JResult *r, int pid);
void juxt_unread(JResult *r);            /* r->count = unread notifications */
/* feeling: 0 normal, 1 happy, 2 wink, 3 surprised, 4 frustrated, 5 puzzled.
 * reply_to: a post id to reply to, or NULL for a new post in community_id. */
void juxt_post_text(JResult *r, const char *community_id, const char *reply_to, const char *text, int feeling,
                    bool spoiler);
/* bits: 320x120, one byte a pixel, non-zero = black */
void juxt_post_drawing(JResult *r, const char *community_id, const char *reply_to, const uint8_t *bits,
                       int feeling, bool spoiler);

/* Parsers (also used by the PC tests). */
int  jp_parse_posts(const char *html, size_t len, JPost *out, int cap, char *next, size_t next_cap);
int  jp_parse_communities(const char *html, size_t len, JCommunity *out, int cap, int *npopular);
int  jp_parse_news(const char *html, size_t len, JNews *out, int cap, char *next, size_t next_cap);
void jp_parse_header(const char *html, size_t len, JPage *pg);
/* the toast on the sign-in page ("Password was incorrect."), or "" */
void jp_parse_toast(const char *html, size_t len, char *out, size_t cap);
/* an absolute address for a link or picture on the page */
void jp_absolute(char *url, size_t cap);
