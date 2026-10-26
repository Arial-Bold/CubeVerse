/* musicsynth.h - CubeVerse's menu music: an original, slow ambient piece
 * (soft pads, a glassy pulse, a few bells and a low hum of air), played by
 * a small synthesiser in real time. Also used by the PC preview tool. */
#pragma once
#include <stdint.h>

/* The output rate: 3/8 of the DSP's own rate on the GameCube, so the voice
 * that plays it moves through its buffer exactly 36 samples per 2 ms block. */
#define MS_RATE 18016

void ms_reset(void);   /* back to the start of the piece */
void ms_set_lite(int on);   /* a lighter mix for when the processor is busy */
/* Renders the next `frames` stereo frames (left, right, left...) at `gain` (0..1). */
void ms_render(int16_t *out, int frames, float gain);
