/* sfxsynth.h - the menu sounds, synthesised (see sfxsynth.c). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SFX_RATE 32000

typedef enum {
    SFX_HOVER,        /* moving between options (three sounds, used in turn) */
    SFX_HOVER2, SFX_HOVER3,
    SFX_SELECT,       /* choosing an option */
    SFX_CONFIRM,      /* confirming / saving */
    SFX_CANCEL,       /* cancelling an option */
    SFX_BACK,         /* leaving a screen */
    SFX_TURN_SIDE,    /* the cube turning to a side */
    SFX_TURN_CENTER,  /* the cube turning back to the front */
    SFX_OPEN,         /* opening a side of the cube */
    SFX_START,        /* (no longer played: the opening's clip has its own chord) */
    SFX_CLOSE, SFX_ERROR, SFX_KEY, SFX_LOADED,
    SFX_ASSEMBLE,     /* the opening: the haze, the light and the logo's chord */
    SFX_DIZZY,        /* home cubes turned round and round: a woozy slide whistle */
    SFX_YAWN,         /* home cubes waking up */
    SFX_POKE,         /* the logo poked */
    SFX_WHEE,         /* the logo's cubes tumbling out */
    SFX_BOING,        /* one of them bouncing */
    SFX_COUNT
} Sfx;

/* Samples in a sound (mono 16-bit at SFX_RATE). */
int  sfx_length(int id);
/* Makes the sound into out (n = sfx_length(id) samples). */
bool sfx_render(int id, int16_t *out, int n);
