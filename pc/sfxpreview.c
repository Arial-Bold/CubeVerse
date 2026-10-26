/* sfxpreview.c - writes each menu sound to a .wav file, to listen to on a PC. */
#include "sfxsynth.h"
#include "musicsynth.h"
#include <stdio.h>
#include <stdlib.h>
static const char *names[SFX_COUNT] = { "move_1", "move_2", "move_3", "select", "confirm", "cancel", "back", "turn_side",
                                        "turn_center", "open_side", "start", "close", "error", "key", "loaded" };
static void w32(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void w16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }
int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "out/sfx";
    for (int i = 0; i < SFX_COUNT; i++) {
        int n = sfx_length(i);
        int16_t *b = malloc(sizeof(int16_t) * n);
        sfx_render(i, b, n);
        char p[256];
        snprintf(p, sizeof(p), "%s/%02d_%s.wav", dir, i, names[i]);
        FILE *f = fopen(p, "wb");
        fwrite("RIFF", 1, 4, f); w32(f, 36 + n * 2); fwrite("WAVEfmt ", 1, 8, f); w32(f, 16); w16(f, 1); w16(f, 1);
        w32(f, SFX_RATE); w32(f, SFX_RATE * 2); w16(f, 2); w16(f, 16); fwrite("data", 1, 4, f); w32(f, n * 2);
        for (int k = 0; k < n; k++) w16(f, (unsigned)(uint16_t)b[k]);
        fclose(f);
        free(b);
    }
    /* the menu music: the piece repeats every two times round (the second
     * time has small changes), so passes 1 and 2 loop seamlessly; pass 0,
     * with its gentle start, plays only once and is skipped here */
    double pass_frames = 256 * (60.0 / 70.0 / 2.0) * MS_RATE;
    int skip = (int)(pass_frames + 0.5), n = (int)(pass_frames * 3 + 0.5) - skip;
    int16_t *b = malloc(sizeof(int16_t) * 2 * (size_t)(skip > n ? skip : n));
    ms_reset();
    for (int at = 0; at < skip; at += 512) ms_render(b, skip - at < 512 ? skip - at : 512, 1.0f);
    for (int at = 0; at < n; at += 512) ms_render(b + at * 2, n - at < 512 ? n - at : 512, 1.0f);
    char p[256];
    snprintf(p, sizeof(p), "%s/menu_music_loop.wav", dir);
    FILE *f = fopen(p, "wb");
    fwrite("RIFF", 1, 4, f); w32(f, 36 + n * 4); fwrite("WAVEfmt ", 1, 8, f); w32(f, 16); w16(f, 1); w16(f, 2);
    w32(f, MS_RATE); w32(f, MS_RATE * 4); w16(f, 4); w16(f, 16); fwrite("data", 1, 4, f); w32(f, n * 4);
    for (int k = 0; k < n * 2; k++) w16(f, (unsigned)(uint16_t)b[k]);
    fclose(f);
    free(b);
    return 0;
}
