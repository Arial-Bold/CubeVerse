/* keyboard.c - see keyboard.h. */
#include "keyboard.h"
#include "pointer.h"
#include "input.h"
#include "sound.h"
#include "gfx.h"
#include "ui.h"
#include "cvui.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* special keys */
enum { K_SHIFT = 1, K_DEL, K_SYM, K_SPACE, K_RET };

typedef struct { const char *label; char ch; int special; float units; } Key;

#define ROWS 5
#define MAXK 12

static const Key LETTERS[ROWS][MAXK] = {
    { {"1",'1'},{"2",'2'},{"3",'3'},{"4",'4'},{"5",'5'},{"6",'6'},{"7",'7'},{"8",'8'},{"9",'9'},{"0",'0'} },
    { {"q",'q'},{"w",'w'},{"e",'e'},{"r",'r'},{"t",'t'},{"y",'y'},{"u",'u'},{"i",'i'},{"o",'o'},{"p",'p'} },
    { {"a",'a'},{"s",'s'},{"d",'d'},{"f",'f'},{"g",'g'},{"h",'h'},{"j",'j'},{"k",'k'},{"l",'l'},{"-",'-'} },
    { {"Shift",0,K_SHIFT,1.5f},{"z",'z'},{"x",'x'},{"c",'c'},{"v",'v'},{"b",'b'},{"n",'n'},{"m",'m'},{"Delete",0,K_DEL,1.5f} },
    { {"!?#",0,K_SYM,1.5f},{"@",'@'},{",",','},{"Space",0,K_SPACE,4},{".",'.'},{"_",'_'},{"Return",0,K_RET,1.5f} },
};
static const Key SYMBOLS[ROWS][MAXK] = {
    { {"1",'1'},{"2",'2'},{"3",'3'},{"4",'4'},{"5",'5'},{"6",'6'},{"7",'7'},{"8",'8'},{"9",'9'},{"0",'0'} },
    { {"!",'!'},{"?",'?'},{"#",'#'},{"$",'$'},{"%",'%'},{"&",'&'},{"*",'*'},{"(",'('},{")",')'},{"=",'='} },
    { {"+",'+'},{"/",'/'},{":",':'},{";",';'},{"'",'\''},{"\"",'"'},{"<",'<'},{">",'>'},{"[",'['},{"]",']'} },
    { {"Shift",0,K_SHIFT,1.5f},{"~",'~'},{"^",'^'},{"|",'|'},{"\\",'\\'},{"{",'{'},{"}",'}'},{"`",'`'},{"Delete",0,K_DEL,1.5f} },
    { {"abc",0,K_SYM,1.5f},{"@",'@'},{",",','},{"Space",0,K_SPACE,4},{".",'.'},{"_",'_'},{"Return",0,K_RET,1.5f} },
};

static int row_len(const Key *row) {
    int n = 0;
    while (n < MAXK && (row[n].label)) n++;
    return n;
}

void kbd_init(Kbd *k, char *buf, int cap) {
    memset(k, 0, sizeof(*k));
    k->buf = buf;
    k->cap = cap;
    k->cursor = (int)strlen(buf);
}

float kbd_height(float key_h, float gap) { return ROWS * key_h + (ROWS - 1) * gap; }

static const Key (*layer(const Kbd *k))[MAXK] { return k->symbols ? SYMBOLS : LETTERS; }

void kbd_draw(Kbd *k, float x, float y, float w, float key_h, float gap, int id_base, float al) {
    const Key (*L)[MAXK] = layer(k);
    /* the unit width: the widest row is 10 units */
    float unit = (w - 9 * gap) / 10;
    for (int r = 0; r < ROWS; r++) {
        int n = row_len(L[r]);
        float units = 0;
        for (int i = 0; i < n; i++) units += L[r][i].units > 0 ? L[r][i].units : 1;
        float rw = units * unit + (n - 1) * gap;
        float kx = x + (w - rw) / 2, ky = y + r * (key_h + gap);
        for (int i = 0; i < n; i++) {
            const Key *key = &L[r][i];
            float kw = (key->units > 0 ? key->units : 1) * unit;
            if (key->units > 1) kw += (key->units - 1) * gap;
            int id = id_base + r * MAXK + i;
            bool hot = ptr_hot(id, kx, ky, kw, key_h);
            bool special = key->special != 0 && key->special != K_SPACE;
            bool lit = (key->special == K_SHIFT && (k->shift || k->caps));
            Color face = special ? rgba(0xDC, 0xE3, 0xF0, 255) : rgba(255, 255, 255, 255);
            if (lit) face = rgba(0xC9, 0xE8, 0xFF, 255);
            gfx_rect(kx - 1, ky - 1, kw + 2, key_h + 2, rgba(0, 0, 0, (u8)(0.4f * 255 * al)));
            gfx_rect(kx, ky, kw, key_h, color_alpha(face, al));
            gfx_rect(kx, ky + key_h - 3, kw, 3, rgba(0xD3, 0xDA, 0xEA, (u8)(255 * al)));
            char lab[8];
            snprintf(lab, sizeof(lab), "%s", key->label);
            if (!key->special && (k->shift || k->caps) && !k->symbols) lab[0] = (char)toupper((unsigned char)lab[0]);
            if (key->special == K_SHIFT && k->caps) snprintf(lab, sizeof(lab), "CAPS");
            float fs = special ? 12 : 15;
            ui_text_center(FONT_ROUND, fs, 0, kx + kw / 2, ky, key_h - 2, special ? rgba(0x4A, 0x51, 0x70, (u8)(255 * al)) : rgba(0x2A, 0x2F, 0x45, (u8)(255 * al)), lab);
            if (hot) ui_gold_ring(kx, ky, kw, key_h, 0.5f + 0.5f * sinf(g_time * 4), al);
            kx += kw + gap;
        }
    }
}

static void insert(Kbd *k, char c) {
    int len = (int)strlen(k->buf);
    if (len + 1 >= k->cap) { sound_play(SFX_ERROR); return; }
    memmove(k->buf + k->cursor + 1, k->buf + k->cursor, (size_t)(len - k->cursor + 1));
    k->buf[k->cursor++] = c;
    sound_play(SFX_KEY);
}

static void del(Kbd *k) {
    if (k->cursor <= 0) return;
    int len = (int)strlen(k->buf);
    memmove(k->buf + k->cursor - 1, k->buf + k->cursor, (size_t)(len - k->cursor + 1));
    k->cursor--;
    sound_play(SFX_KEY);
}

bool kbd_update(Kbd *k, int clicked, int id_base) {
    bool changed = false;
    if (clicked >= id_base && clicked < id_base + ROWS * MAXK) {
        int r = (clicked - id_base) / MAXK, i = (clicked - id_base) % MAXK;
        const Key (*L)[MAXK] = layer(k);
        if (i < row_len(L[r])) {
            const Key *key = &L[r][i];
            switch (key->special) {
            case K_SHIFT:
                if (k->caps) { k->caps = false; k->shift = false; }
                else if (k->shift) { k->caps = true; k->shift = false; }
                else k->shift = true;
                sound_play(SFX_SELECT);
                break;
            case K_SYM: k->symbols = !k->symbols; sound_play(SFX_SELECT); break;
            case K_DEL: del(k); changed = true; break;
            case K_SPACE: if (!k->no_space) { insert(k, ' '); changed = true; } break;
            case K_RET: if (!k->no_space) { insert(k, '\n'); changed = true; } break;
            default: {
                char c = key->ch;
                if ((k->shift || k->caps) && !k->symbols) c = (char)toupper((unsigned char)c);
                insert(k, c);
                changed = true;
                if (k->shift) k->shift = false;
            }
            }
        }
    }
    if (input_repeat(BTN_X)) { del(k); changed = true; }
    if (input_pressed(BTN_Y) && !k->no_space) { insert(k, ' '); changed = true; }
    if (input_repeat(BTN_L) && k->cursor > 0) k->cursor--;
    if (input_repeat(BTN_R) && k->cursor < (int)strlen(k->buf)) k->cursor++;
    if (input_pressed(BTN_Z)) { k->shift = !k->shift; k->caps = false; sound_play(SFX_SELECT); }
    return changed;
}

void kbd_draw_field(const Kbd *k, float x, float y, float w, float h, float size, bool focused, bool mask, float al) {
    /* a solid 2-unit frame: 1-pixel lines can vanish when 16:9 is squeezed */
    gfx_rect(x - 2, y - 2, w + 4, h + 4, focused ? rgba(0x29, 0xA8, 0xE0, (u8)(255 * al)) : rgba(0xA6, 0xB0, 0xC8, (u8)(255 * al)));
    gfx_rect(x, y, w, h, rgba(255, 255, 255, (u8)(255 * al)));
    char shown[256];
    int len = (int)strlen(k->buf);
    if (len > 250) len = 250;
    for (int i = 0; i < len; i++) shown[i] = mask ? '*' : (k->buf[i] == '\n' ? ' ' : k->buf[i]);
    shown[len] = 0;
    float pad = 10, room = w - 2 * pad;
    int cur = k->cursor > len ? len : k->cursor;
    /* scroll so the cursor shows */
    int start = 0;
    while (start < cur && gfx_text_width(FONT_ROUND, size, shown + start, cur - start) > room - 6) start++;
    int n = gfx_text_fit(FONT_ROUND, size, shown + start, -1, room);
    gfx_set_clip(x + 2, y, w - 4, h);
    float ty = y + (h - gfx_line_height(FONT_ROUND, size)) / 2;
    Color ink = rgba(0x2A, 0x2F, 0x45, (u8)(255 * al));
    gfx_text(FONT_ROUND, size, x + pad, ty, ink, shown + start, n);
    if (focused && fmodf(g_time, 1.0f) < 0.6f) {
        float cx = x + pad + gfx_text_width(FONT_ROUND, size, shown + start, cur - start);
        gfx_rect(cx, y + h * 0.2f, 2, h * 0.6f, rgba(0x29, 0xA8, 0xE0, (u8)(255 * al)));
    }
    gfx_clear_clip();
}
