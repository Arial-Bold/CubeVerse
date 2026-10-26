/* postlist.h - a scrolling list of Juxt posts, shared by Activity, a
 * community and a person's page: full cards (one column) or tiles (a grid),
 * Yeah! with X or the pill, A to open a post, Z (or the face) for the
 * poster's page. More posts load as you scroll near the end. */
#pragma once
#include <stdbool.h>
#include "juxt.h"

#define PL_MAX JPAGE_POSTS

typedef struct {
    JPage *pg;
    int    cols;            /* 1: cards; 2..4: tiles */
    int    id_base;         /* pointer ids id_base .. id_base + 4*PL_MAX */
    int    first;           /* first post to show (1 on a post's page: 0 is the post) */
    bool   show_community;  /* name the community on each card */
    bool   big_first;       /* a post's page: post 0 big, then "Comments" and the replies */
    float  scroll, target;
    float  content_h;
    float  view_top, view_bottom;
    float  item_top[PL_MAX], item_bot[PL_MAX];
    JResult yeah;
    int    yeah_idx;
    int    last_over;
} PostList;

void pl_init(PostList *pl, JPage *pg, int cols, int id_base);
void pl_reset_scroll(PostList *pl);
/* Input: clicks, X/Z on the post under the hand, scrolling, loading more. */
void pl_update(PostList *pl);
/* Draws the list in x..x+w from y down to bottom (it runs off the bottom
 * into the fading line). Also the loading/empty/error notes. */
void pl_draw(PostList *pl, float x, float y, float w, float bottom, float alpha);
/* The post under the hand, or -1. */
int  pl_over(const PostList *pl);
/* Yeah a post (index), with the count updated at once. */
void pl_yeah(PostList *pl, int i);
/* The text shown when a list is empty. */
extern const char *pl_empty_text;

/* One post as a full card (also the big post on a post's page). Returns the
 * height. draw = false only measures. big: the post's own page (more text). */
float pl_card(const JPost *p, float x, float y, float w, bool hot, bool yeah_hot, bool show_community, bool big,
              bool draw, float alpha);
/* Where the Yeah! pill of a card at (x, y) sits. */
void pl_card_yeah_box(const JPost *p, float x, float y, float w, bool show_community, bool big, float *bx, float *by,
                      float *bw, float *bh);
