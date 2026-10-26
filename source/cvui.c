/* cvui.c - see cvui.h. Sizes and colours follow the CubeVerse mockups. */
#include "cvui.h"
#include "pointer.h"
#include "imgcache.h"
#include "m3d.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const char *const CV_FEELINGS[6] = { "Normal", "Happy", "Wink", "Surprised", "Frustrated", "Puzzled" };

void cv_background(float al) {
    if (al <= 0) return;
    float fx, fy, fw, fh;
    gfx_full_area(&fx, &fy, &fw, &fh);
    u8 A = (u8)(255 * clamp01(al));
    /* deep blue-black, a little lighter at the top */
    gfx_rect_vgrad(fx, fy, fw, fh, rgba(0x0e, 0x12, 0x18, A), rgba(0x06, 0x08, 0x0b, A));
    /* a cool glow from above and a fainter one from below */
    enum { C = 12, R = 8 };
    float xy[(C + 1) * (R + 1) * 2];
    Color col[(C + 1) * (R + 1)];
    for (int pass = 0; pass < 2; pass++) {
        for (int j = 0; j <= R; j++)
            for (int i = 0; i <= C; i++) {
                int k = j * (C + 1) + i;
                float x = fx + fw * i / C, y = fy + fh * j / R;
                xy[k * 2] = x;
                xy[k * 2 + 1] = y;
                float dx = (x - (fx + fw / 2)) / (fw * (pass ? 0.45f : 0.6f));
                float dy = pass ? (y - (fy + fh * 1.18f)) / (fh * 0.55f) : (y - (fy - fh * 0.12f)) / (fh * 0.7f);
                float d = sqrtf(dx * dx + dy * dy);
                float v = clamp01(1 - d / (pass ? 0.66f : 0.62f));
                col[k] = pass ? rgba(60, 110, 175, (u8)(v * 0.13f * 255 * al)) : rgba(120, 170, 225, (u8)(v * 0.17f * 255 * al));
            }
        gfx_mesh(C, R, xy, col);
    }
    /* pinstripes, a hint of the Wii menu: every 6 units, 2 thick */
    Color line = rgba(255, 255, 255, (u8)(0.045f * 255 * al));
    for (float y = fy; y < fy + fh; y += 6) gfx_rect(fx, gfx_snap_y(y), fw, 2, line);
    /* a vignette: the edges fade to black */
    {
        enum { VC = 16, VR = 12 };
        float vxy[(VC + 1) * (VR + 1) * 2];
        Color vc[(VC + 1) * (VR + 1)];
        for (int j = 0; j <= VR; j++)
            for (int i = 0; i <= VC; i++) {
                int k = j * (VC + 1) + i;
                float u = (float)i / VC * 2 - 1, v = (float)j / VR * 2 - 1;
                vxy[k * 2] = fx + fw * i / VC;
                vxy[k * 2 + 1] = fy + fh * j / VR;
                float d = sqrtf(u * u * 0.8f + v * v);   /* a little wider than tall */
                float t = clamp01((d - 0.4f) / 0.7f);   /* black in the corners */
                vc[k] = rgba(0, 0, 0, (u8)(powf(t, 1.5f) * 255 * al));
            }
        gfx_mesh(VC, VR, vxy, vc);
    }
}

void cv_title(const char *left, Color c, const char *right, float al) {
    if (al <= 0) return;
    float x0 = CV_L + 6, x1 = UI_W - CV_L - 6, y = CV_TOP + 9;
    c.a = (u8)(255 * al);
    ui_text(FONT_MENU, 15, 1.6f, x0 + 10, y, 15, c, left);
    if (right && right[0]) {
        Color s = COL_SOFT;
        s.a = (u8)(255 * al);
        ui_text_right(FONT_MENU, 15, 1.6f, x1 - 10, y, 15, s, right);
    }
    gfx_rect(CV_L + 10, CV_TOP + 31, UI_W - 2 * CV_L - 20, 1, rgba(200, 210, 230, (u8)(0.28f * 255 * al)));
}

float cv_tabs(float x, float y, const char *const *labels, int n, int on, int base, float al) {
    float x0 = x;
    for (int i = 0; i < n; i++) {
        float tw = gfx_text_width_sp(FONT_MENU, 13, 1.4f, labels[i], -1) + 24;
        bool hot = ptr_hot(base + i, x, y, tw, 25);
        if (i == on) {
            gfx_rect_vgrad(x, y, tw, 25, rgba(140, 160, 180, (u8)(0.34f * 255 * al)), rgba(70, 90, 110, (u8)(0.22f * 255 * al)));
            gfx_rrect_outline(x, y, tw, 25, 0, 1, rgba(215, 225, 240, (u8)(0.6f * 255 * al)));
        } else if (hot) {
            gfx_rect(x, y, tw, 25, rgba(140, 160, 180, (u8)(0.16f * 255 * al)));
        }
        ui_text(FONT_MENU, 13, 1.4f, x + 12, y, 25, rgba(0xdf, 0xe4, 0xee, (u8)(255 * al)), labels[i]);
        x += tw + 6;
    }
    return x - x0;
}

void cv_card(float x, float y, float w, float h, bool hot, float al) {
    if (al <= 0) return;
    gfx_shadow(x, y + 3, w, h, 0, 8, rgba(0, 0, 0, (u8)(0.45f * 255 * al)));
    gfx_rect(x - 1, y - 1, w + 2, h + 2, rgba(0, 0, 0, (u8)(0.4f * 255 * al)));
    gfx_rect(x, y, w, h, rgba(255, 255, 255, (u8)(255 * al)));
    if (hot) ui_gold_ring(x, y, w, h, 0.5f + 0.5f * sinf(g_time * 4), al);
}

float cv_pill(float x, float y, float w, float h, const char *label, Color bg, Color fg, bool hot, float al) {
    float tw = gfx_text_width(FONT_ROUND, 13, label, -1);
    if (w <= 0) w = tw + 28;
    bg.a = (u8)(bg.a * al);
    fg.a = (u8)(fg.a * al);
    gfx_rect(x, y, w, h, bg);
    ui_text_center(FONT_ROUND, 13, 0, x + w / 2, y, h, fg, label);
    if (hot) ui_gold_ring(x, y, w, h, 0.5f + 0.5f * sinf(g_time * 4), al);
    return w;
}

void cv_face(const char *url, const char *name, float x, float y, float size, float al) {
    GfxImage *img = img_get(url, IMG_SMALL);
    if (img) {
        gfx_rect(x, y, size, size, rgba(240, 243, 250, (u8)(255 * al)));
        gfx_image_draw(img, x, y, size, size, rgba(255, 255, 255, (u8)(255 * al)));
        return;
    }
    /* a letter on a soft colour picked from the name */
    static const Color bgs[] = { { 0xFF, 0xD7, 0xB8, 255 }, { 0xC9, 0xE8, 0xFF, 255 }, { 0xD9, 0xF2, 0xD0, 255 },
                                 { 0xE3, 0xD7, 0xFF, 255 }, { 0xFF, 0xE3, 0xEF, 255 } };
    static const Color fgs[] = { { 0x9A, 0x4A, 0x14, 255 }, { 0x1B, 0x6A, 0x99, 255 }, { 0x2F, 0x7A, 0x1F, 255 },
                                 { 0x5A, 0x44, 0xC8, 255 }, { 0xB2, 0x3A, 0x72, 255 } };
    unsigned hsh = 0;
    for (const char *p = name ? name : ""; *p; p++) hsh = hsh * 31 + (unsigned char)*p;
    int k = (int)(hsh % 5);
    gfx_rect(x, y, size, size, color_alpha(bgs[k], al));
    char l[2] = { name && name[0] ? name[0] : '?', 0 };
    if (l[0] >= 'a' && l[0] <= 'z') l[0] = (char)(l[0] - 32);
    ui_text_center(FONT_CUBE, size * 0.45f, 0, x + size / 2, y, size, color_alpha(fgs[k], al), l);
}

int cv_wrap(FontId f, float size, float sp, const char *s, float x, float y, float w, float line_h, int max_lines,
            Color c, bool draw) {
    int lines = 0;
    const char *p = s;
    char buf[256];
    while (*p && lines < max_lines) {
        if (*p == '\n') { p++; lines++; continue; }
        int len = (int)strcspn(p, "\n");
        int n = len;
        /* fit: shrink to the last space that fits */
        while (n > 0 && gfx_text_width_sp(f, size, sp, p, n) > w) {
            int k = n - 1;
            while (k > 0 && p[k] != ' ') k--;
            if (k == 0) {   /* one long word: cut it */
                n = gfx_text_fit(f, size, p, n, w);
                if (n < 1) n = 1;
                break;
            }
            n = k;
        }
        bool last = lines == max_lines - 1;
        bool more_after = p[n] && !(p[n] == '\n' && !p[n + 1]);
        if (n > (int)sizeof(buf) - 4) n = (int)sizeof(buf) - 4;
        memcpy(buf, p, (size_t)n);
        buf[n] = 0;
        if (last && more_after) {
            /* make room for "..." */
            while (n > 0 && gfx_text_width_sp(f, size, sp, buf, -1) + gfx_text_width_sp(f, size, sp, "...", -1) > w) buf[--n] = 0;
            while (n > 0 && buf[n - 1] == ' ') buf[--n] = 0;
            strcat(buf, "...");
        }
        if (draw) gfx_text_sp(f, size, sp, x, y + lines * line_h + (line_h - gfx_line_height(f, size)) / 2, c, buf, -1);
        lines++;
        p += n;
        while (*p == ' ') p++;
        if (*p == '\n') p++;
    }
    return lines;
}

static float list_x, list_y, list_w, list_b;

void cv_list_begin(float x, float y, float w, float bottom) {
    list_x = x; list_y = y; list_w = w; list_b = bottom;
    gfx_set_base_clip(x - 12, y, w + 24, bottom - y);
}

void cv_list_end(bool more_below, float al) {
    gfx_clear_base_clip();
    if (!more_below || al <= 0) return;
    /* fade the last part into the background, then the light line */
    float fx, fy, fw, fh;
    gfx_full_area(&fx, &fy, &fw, &fh);
    gfx_rect_vgrad(list_x - 12, list_b - 30, list_w + 24, 30, rgba(8, 10, 14, 0), rgba(8, 10, 14, (u8)(255 * al)));
    float cx = list_x + list_w / 2, half = list_w / 2 + 12;
    Color mid = rgba(200, 222, 255, (u8)(0.85f * 255 * al)), clear = rgba(190, 215, 255, 0);
    gfx_rect_hgrad(cx - half, list_b - 1, half, 1, clear, mid);
    gfx_rect_hgrad(cx, list_b - 1, half, 1, mid, clear);
    Color glow = rgba(160, 200, 255, (u8)(0.22f * 255 * al)), g0 = rgba(160, 200, 255, 0);
    gfx_rect_hgrad(cx - half, list_b - 4, half, 6, g0, glow);
    gfx_rect_hgrad(cx, list_b - 4, half, 6, glow, g0);
}

void cv_center_note(float x, float y, float w, float h, const char *title, const char *text, bool spinner, float al) {
    float cy = y + h / 2;
    if (spinner) {
        ui_spinner(x + w / 2, cy - 22, 12, g_time, al);
        cy += 6;
    }
    if (title && title[0]) {
        ui_text_center(FONT_ROUND, 16, 0, x + w / 2, cy - 12, 24, rgba(255, 255, 255, (u8)(255 * al)), title);
        cy += 20;
    }
    if (text && text[0]) {
        float tw = w - 80 > 460 ? 460 : w - 80;
        int lines = cv_wrap(FONT_MENU, 13, 1.0f, text, 0, 0, tw, 20, 4, COL_SOFT, false);
        float ty = cy;
        /* centre each line */
        const char *p = text;
        char buf[256];
        for (int i = 0; i < lines && *p; i++) {
            int len = (int)strcspn(p, "\n");
            int n = len;
            while (n > 0 && gfx_text_width_sp(FONT_MENU, 13, 1.0f, p, n) > tw) {
                int k = n - 1;
                while (k > 0 && p[k] != ' ') k--;
                if (!k) break;
                n = k;
            }
            if (n > 250) n = 250;
            memcpy(buf, p, (size_t)n);
            buf[n] = 0;
            ui_text_center(FONT_MENU, 13, 1.0f, x + w / 2, ty, 20, rgba(0xc9, 0xd2, 0xee, (u8)(255 * al)), buf);
            ty += 20;
            p += n;
            while (*p == ' ' || *p == '\n') p++;
        }
    }
}

void cv_feeling(int feeling, float cx, float cy, float r, Color ink, float al) {
    ink.a = (u8)(ink.a * al);
    float t = r * 0.16f;
    if (t < 1.4f) t = 1.4f;
    gfx_ring(cx, cy, r, t, 0, 2 * (float)M_PI, ink);
    float ex = r * 0.36f, ey = cy - r * 0.25f, er = t * 0.75f;
    /* eyes */
    if (feeling == 2) {
        gfx_circle(cx - ex, ey, er, ink);
        gfx_line(cx + ex - er * 1.6f, ey, cx + ex + er * 1.6f, ey, t, ink);
    } else if (feeling == 5) {
        gfx_line(cx - ex - er, ey - er, cx - ex + er, ey + er * 0.5f, t, ink);
        gfx_circle(cx + ex, ey, er, ink);
    } else {
        gfx_circle(cx - ex, ey, er, ink);
        gfx_circle(cx + ex, ey, er, ink);
    }
    /* mouth */
    float my = cy + r * 0.3f;
    switch (feeling) {
    case 1: case 2: gfx_ring(cx, my - r * 0.2f, r * 0.42f, t, 0.35f, (float)M_PI - 0.35f, ink); break;
    case 3: gfx_circle(cx, my + r * 0.05f, r * 0.17f, ink); break;
    case 4: gfx_ring(cx, my + r * 0.32f, r * 0.42f, t, (float)M_PI + 0.45f, 2 * (float)M_PI - 0.45f, ink); break;
    case 5: gfx_line(cx - r * 0.3f, my, cx + r * 0.3f, my - r * 0.1f, t, ink); break;
    default: gfx_line(cx - r * 0.34f, my, cx + r * 0.34f, my, t, ink); break;
    }
}

float cv_side(void) { return CV_L + (UI_W > 700 ? 120 : 14); }
