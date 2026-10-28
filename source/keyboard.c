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

/* special keys (K_GAP: an empty space that indents a row) */
enum { K_SHIFT = 1, K_DEL, K_SYM, K_ABC, K_SPACE, K_RET, K_CAPS, K_OK, K_GAP };

typedef struct { const char *label; char ch; int special; float units; } Key;

#define ROWS 5
#define MAXK 13
#define PAD 8.0f

/* Laid out like the Wii U's keyboard, with a row of numbers above the
 * letters: Delete beside @ at the end of the top letter row, Return at the
 * end of the bottom one, and along the bottom shift, caps, a wide space,
 * the letters / symbols pages and OK (Post, Sign in...). Every row is 12
 * units wide. */
static const Key LETTERS[ROWS][MAXK] = {
    { {"1",'1'},{"2",'2'},{"3",'3'},{"4",'4'},{"5",'5'},{"6",'6'},{"7",'7'},{"8",'8'},{"9",'9'},{"0",'0'},{"-",'-'},{"/",'/'} },
    { {"q",'q'},{"w",'w'},{"e",'e'},{"r",'r'},{"t",'t'},{"y",'y'},{"u",'u'},{"i",'i'},{"o",'o'},{"p",'p'},{"@",'@'},{"Del",0,K_DEL} },
    { {"",0,K_GAP,0.5f},{"a",'a'},{"s",'s'},{"d",'d'},{"f",'f'},{"g",'g'},{"h",'h'},{"j",'j'},{"k",'k'},{"l",'l'},{"'",'\''},{"!",'!'} },
    { {"",0,K_GAP,0.5f},{"z",'z'},{"x",'x'},{"c",'c'},{"v",'v'},{"b",'b'},{"n",'n'},{"m",'m'},{",",','},{".",'.'},{"?",'?'},{"Return",0,K_RET,1.5f} },
    { {"Shift",0,K_SHIFT,1.4f},{"aA",0,K_CAPS,1.4f},{"Space",0,K_SPACE,4.6f},{"abc",0,K_ABC,1.4f},{"#!?",0,K_SYM,1.4f},{"OK",0,K_OK,1.8f} },
};
static const Key SYMBOLS[ROWS][MAXK] = {
    { {"1",'1'},{"2",'2'},{"3",'3'},{"4",'4'},{"5",'5'},{"6",'6'},{"7",'7'},{"8",'8'},{"9",'9'},{"0",'0'},{"-",'-'},{"/",'/'} },
    { {"!",'!'},{"?",'?'},{"#",'#'},{"$",'$'},{"%",'%'},{"&",'&'},{"*",'*'},{"(",'('},{")",')'},{"=",'='},{"+",'+'},{"Del",0,K_DEL} },
    { {"",0,K_GAP,0.5f},{":",':'},{";",';'},{"\"",'"'},{"'",'\''},{"<",'<'},{">",'>'},{"[",'['},{"]",']'},{"{",'{'},{"}",'}'},{"_",'_'} },
    { {"",0,K_GAP,0.5f},{"~",'~'},{"^",'^'},{"|",'|'},{"\\",'\\'},{"`",'`'},{"@",'@'},{"$",'$'},{",",','},{".",'.'},{"?",'?'},{"Return",0,K_RET,1.5f} },
    { {"Shift",0,K_SHIFT,1.4f},{"aA",0,K_CAPS,1.4f},{"Space",0,K_SPACE,4.6f},{"abc",0,K_ABC,1.4f},{"#!?",0,K_SYM,1.4f},{"OK",0,K_OK,1.8f} },
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
    k->ok_label = "OK";
}

float kbd_height(float key_h, float gap) { return ROWS * key_h + (ROWS - 1) * gap + 2 * PAD; }

static const Key (*layer(const Kbd *k))[MAXK] { return k->symbols ? SYMBOLS : LETTERS; }

void kbd_draw(Kbd *k, float x, float y, float w, float key_h, float gap, int id_base, float al) {
    const Key (*L)[MAXK] = layer(k);
    /* the panel the keys sit in, light like the Wii U's */
    float ph = kbd_height(key_h, gap);
    gfx_rect(x, y, w, ph, rgba(0xEC, 0xEE, 0xF2, (u8)(255 * al)));
    gfx_rect(x, y, w, 2, rgba(255, 255, 255, (u8)(255 * al)));
    float unit = (w - 2 * PAD - 11 * gap) / 12;
    for (int r = 0; r < ROWS; r++) {
        int n = row_len(L[r]);
        float kx = x + PAD, ky = y + PAD + r * (key_h + gap);
        for (int i = 0; i < n; i++) {
            const Key *key = &L[r][i];
            float u = key->units > 0 ? key->units : 1;
            if (key->special == K_GAP) { kx += u * (unit + gap); continue; }
            float kw = u * unit + (u - 1) * gap;
            int id = id_base + r * MAXK + i;
            bool hot = ptr_hot(id, kx, ky, kw, key_h);
            bool mod = key->special != 0 && key->special != K_SPACE;
            bool lit = (key->special == K_SHIFT && k->shift) || (key->special == K_CAPS && k->caps) ||
                       (key->special == K_ABC && !k->symbols) || (key->special == K_SYM && k->symbols);
            bool blue = lit || key->special == K_OK;
            Color face = blue ? rgba(0x29, 0xA8, 0xE0, 255) : mod ? rgba(0xE0, 0xE4, 0xEC, 255) : rgba(255, 255, 255, 255);
            /* the key: a darker edge under it, a soft band at its foot */
            gfx_rect(kx, ky + 2, kw, key_h, rgba(0xA9, 0xB0, 0xC0, (u8)(255 * al)));
            gfx_rect(kx, ky, kw, key_h, color_alpha(face, al));
            gfx_rect(kx, ky + key_h - 3, kw, 3, rgba((u8)(face.r * 0.88f), (u8)(face.g * 0.88f), (u8)(face.b * 0.88f), (u8)(255 * al)));
            char lab[16];
            snprintf(lab, sizeof(lab), "%s", key->special == K_OK ? k->ok_label : key->label);
            if (!key->special && (k->shift || k->caps) && !k->symbols) lab[0] = (char)toupper((unsigned char)lab[0]);
            float fs = mod || key->special == K_SPACE ? 12 : 16;
            Color ink = blue ? rgba(255, 255, 255, (u8)(255 * al)) : mod ? rgba(0x4A, 0x51, 0x70, (u8)(255 * al)) : rgba(0x2A, 0x2F, 0x45, (u8)(255 * al));
            ui_text_center(mod ? FONT_MENU_BOLD : FONT_ROUND, fs, 0, kx + kw / 2, ky, key_h - 2, ink, lab);
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
            case K_SHIFT: k->shift = !k->shift; k->caps = false; sound_play(SFX_SELECT); break;
            case K_CAPS: k->caps = !k->caps; k->shift = false; sound_play(SFX_SELECT); break;
            case K_SYM: if (!k->symbols) { k->symbols = true; sound_play(SFX_SELECT); } break;
            case K_ABC: if (k->symbols) { k->symbols = false; sound_play(SFX_SELECT); } break;
            case K_DEL: del(k); changed = true; break;
            case K_SPACE: if (!k->no_space) { insert(k, ' '); changed = true; } break;
            case K_RET: if (!k->no_space) { insert(k, '\n'); changed = true; } break;
            case K_OK: k->ok = true; break;
            case K_GAP: break;
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
