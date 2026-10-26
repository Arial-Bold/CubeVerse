/* keyboard.h - the on-screen keyboard (signing in, writing posts): white
 * square keys you point at with the hand and press with A, a shift key, a
 * symbols page, space and delete. X deletes, Y types a space, L and R move
 * the text cursor, Z switches shift. */
#pragma once
#include <stdbool.h>

typedef struct {
    char *buf;
    int   cap;          /* bytes, including the 0 */
    int   cursor;       /* byte offset */
    bool  shift;        /* one capital, then back to small letters */
    bool  caps;         /* shift pressed twice: capitals until pressed again */
    bool  symbols;
    bool  no_space;     /* sign-in names */
} Kbd;

void kbd_init(Kbd *k, char *buf, int cap);
/* Draws the keys filling x..x+w from y, rows of key_h. Ids id_base.. */
void kbd_draw(Kbd *k, float x, float y, float w, float key_h, float gap, int id_base, float alpha);
/* The height kbd_draw uses. */
float kbd_height(float key_h, float gap);
/* Handles a click on one of its keys and the X/Y/L/R/Z buttons.
 * Returns true if the text changed. */
bool kbd_update(Kbd *k, int clicked, int id_base);
/* Draws the text with its cursor in a box (one line, scrolled so the
 * cursor shows). mask: show dots instead of the letters. */
void kbd_draw_field(const Kbd *k, float x, float y, float w, float h, float size, bool focused, bool mask, float alpha);
