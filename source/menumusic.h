/* menumusic.h - CubeVerse's background music in the menus (musicsynth.c),
 * made in real time by a thread and played on two DSP voices. It fades out
 * while a song from the music folder is playing or paused, and when it's
 * turned off in Settings. */
#pragma once

void menumusic_init(void);              /* after midi_init */
void menumusic_start(float delay);      /* the menu is ready: begin after `delay` seconds */
void menumusic_update(void);            /* once per frame */
void menumusic_shutdown(void);          /* before the sound library is reset */
/* How hard the processor works on the music (% of real time), and how
 * many times it fell behind (shown in Settings, to track down problems). */
void menumusic_stats(int *load_pct, int *gap_count);
