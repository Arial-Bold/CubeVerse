/* sound.c - plays the menu sounds (synthesised at start-up by sfxsynth.c) on
 * the DSP's hardware voices through libogc2's AESND library, which the music
 * player uses too. From CubeBrowser. */
#include "sound.h"
#include "settings.h"
#include "menumusic.h"
#include "thr.h"
#include <gccore.h>
#include <aesndlib.h>
#include <ogc/aram.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define RATE SFX_RATE

static s16 *buffers[SFX_COUNT];
static u32 sizes[SFX_COUNT];
static bool ready;

static void synth(int id) {
    int n = sfx_length(id);
    if (n <= 0) return;
    u32 bytes = ((u32)n * 2 + 31) & ~31u;
    s16 *buf = memalign(32, bytes);
    if (!buf) return;
    memset(buf, 0, bytes);
    if (!sfx_render(id, buf, n)) { free(buf); return; }
    DCFlushRange(buf, bytes);
    sizes[id] = bytes;
    __sync_synchronize();   /* the size is in place before the sound counts as ready */
    buffers[id] = buf;
}

/* The sounds are made on a thread of their own while the opening plays,
 * instead of before anything is on screen (that took seconds, all of it a
 * black screen). The opening's own sounds first: the pieces' whoosh and
 * clicks start at once, the chime 5 seconds in. */
static const Sfx synth_order[] = {
    SFX_ASSEMBLE, SFX_START, SFX_HOVER, SFX_HOVER2, SFX_HOVER3, SFX_SELECT, SFX_BACK, SFX_TURN_SIDE, SFX_TURN_CENTER,
    SFX_OPEN, SFX_CLOSE, SFX_CONFIRM, SFX_CANCEL, SFX_LOADED, SFX_KEY, SFX_ERROR,
};
static thr_t synth_th;
static bool synth_started;
static volatile bool synth_quit;

static void *synth_main(void *arg) {
    (void)arg;
    bool done[SFX_COUNT] = { false };
    for (unsigned k = 0; k < sizeof(synth_order) / sizeof(synth_order[0]) && !synth_quit; k++) {
        synth(synth_order[k]);
        done[synth_order[k]] = true;
    }
    for (int i = 0; i < SFX_COUNT && !synth_quit; i++) if (!done[i]) synth(i);   /* any not in the list */
    return NULL;
}

bool sound_loaded(Sfx s) { return s >= 0 && s < SFX_COUNT && buffers[s] != NULL; }

/* Before each of a voice's copies to sound memory, wait for any copy still
 * in progress: the sound library copies every voice through one shared
 * buffer (this is what the music player's voices do too). */
static void sfx_cb(AESNDPB *pb, u32 st) {
    (void)pb;
    if (st == VOICE_STATE_RUNNING)
        for (int k = 0; k < 20000 && AR_GetDMAStatus(); k++) {}
}

/* A few voices of their own; each plays one sound once and stops itself. */
#define SFX_VOICES 6
static AESNDPB *sfx_voice[SFX_VOICES];
static u64 sfx_busy_until[SFX_VOICES];
static int sfx_next;

void sound_init(void) {
    AESND_Init();
    AESND_Pause(false);
    for (int i = 0; i < SFX_VOICES; i++) sfx_voice[i] = AESND_AllocateVoice(sfx_cb);
    ready = true;
    /* below the main loop (64), above the photo scan and thumbnails */
    synth_started = thr_create(&synth_th, synth_main, NULL, 32 * 1024, 60);
    if (!synth_started) for (int i = 0; i < SFX_COUNT; i++) synth(i);
}

void sound_shutdown(void) {
    if (synth_started) { synth_quit = true; thr_join(synth_th); synth_started = false; }
    if (!ready) return;
    ready = false;
    menumusic_shutdown();
    AESND_Pause(true);
    AESND_Reset();
}

void sound_play(Sfx s) {
    /* option moves take turns between three sounds, like the system menu */
    static int move_turn;
    if (s == SFX_HOVER) { s = (Sfx)(SFX_HOVER + move_turn); move_turn = (move_turn + 1) % 3; }
    if (!ready || !g_set.sounds || g_set.volume == 0 || !buffers[s]) return;
    /* prefer a voice whose last sound has finished: the audio interrupt
     * isn't using it, so starting it can't collide */
    u64 now = gettime();
    int v = -1;
    for (int k = 0; k < SFX_VOICES && v < 0; k++) {
        int i = (sfx_next + k) % SFX_VOICES;
        if (sfx_voice[i] && now >= sfx_busy_until[i]) v = i;
    }
    if (v < 0) return;
    sfx_next = (v + 1) % SFX_VOICES;
    u16 vol = (u16)(g_set.volume * 255 / 10);
    AESND_SetVoiceVolume(sfx_voice[v], vol, vol);
    AESND_PlayVoice(sfx_voice[v], VOICE_MONO16, buffers[s], sizes[s], RATE, 0, false);
    sfx_busy_until[v] = now + millisecs_to_ticks((sizes[s] / 2) * 1000 / RATE + 30);
}
