/* PC stand-in for sound.c (no DSP). */
#include "sound.h"
void sound_init(void) {}
void sound_play(Sfx s) { (void)s; }
bool sound_loaded(Sfx s) { (void)s; return true; }
void sound_shutdown(void) {}
