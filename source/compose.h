/* compose.h - what the writing and drawing screens post to: a community
 * (arg "c|<community id>|<name>") or a comment on a post
 * (arg "r|<post id>|<community id>|<name>"). */
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool reply;
    char post_id[40];
    char cid[24];
    char name[64];
} PostTarget;

void post_target_parse(PostTarget *t, const char *arg);
/* "New post · Drawing Club" or "Comment · Drawing Club" */
void post_target_title(const PostTarget *t, char *out, size_t cap);
/* The same argument again (to switch between Text and Drawing). */
void post_target_arg(const PostTarget *t, char *out, size_t cap);

/* The "Posting..." box while a post is on its way. */
void compose_sending_draw(float alpha);
/* Draws the Text / Drawing tabs at the right of the title (ids base, base+1). */
void compose_tabs(int on, int base);
/* The feelings as a row (horizontal) or a column, ids base..base+5. */
void compose_feelings(float x, float y, float box_w, float box_h, float gap, bool column, int on, int base, float alpha);
/* The feelings as a grid of bigger buttons, each with its name. */
void compose_feelings_grid(float x, float y, int cols, float box_w, float box_h, float gap, int on, int base, float alpha);
