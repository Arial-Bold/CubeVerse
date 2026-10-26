/* sound.h - menu sounds (made in the background at start-up, see sfxsynth.c) played on the
 * DSP's hardware voices. */
#pragma once
#include "sfxsynth.h"
#include <stdbool.h>

void sound_init(void);
void sound_play(Sfx s);
/* The sound has been made (they're made in the background at start-up). */
bool sound_loaded(Sfx s);
void sound_shutdown(void);   /* before launching another program */
