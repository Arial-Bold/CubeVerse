/* musicsynth.h - CubeVerse's menu music: an original, light and mellow tune
 * (bass, electric piano, marimba, whistle and soft percussion), played by a
 * small synthesiser in real time. Also used by the PC preview tool. */
#pragma once
#include <stdint.h>

/* The output rate: 3/8 of the DSP's own rate on the GameCube, so the voice
 * that plays it moves through its buffer exactly 36 samples per 2 ms block. */
#define MS_RATE 18016

/* The piece: a short intro, then a part that repeats (with a small change
 * every other time, so two times round loop seamlessly). */
#define MS_INTRO_SECONDS (2 * 4 * 60.0 / 98.0)
#define MS_LOOP_SECONDS (2 * 32 * 4 * 60.0 / 98.0)

void ms_reset(void);   /* back to the start of the piece */
void ms_set_lite(int on);   /* a lighter mix for when the processor is busy */
/* Renders the next `frames` stereo frames (left, right, left...) at `gain` (0..1). */
void ms_render(int16_t *out, int frames, float gain);
