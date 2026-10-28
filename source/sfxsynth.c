/* sfxsynth.c - CubeVerse's menu sounds, made from scratch at start-up.
 *
 * Each sound is a few soft tones (with a slight chorus), an optional breath
 * of filtered noise, and a small room reverb, in the warm, low, airy style
 * of the GameCube's menus: turning the cube swells a chord over a whoosh,
 * moving between options is a short soft bell, confirming climbs an
 * arpeggio. Nothing is sampled. Also used by the PC preview tool. */
#include "sfxsynth.h"
#include "intro_clip.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float f0, f1;        /* pitch at start and end (Hz) */
    float at, att, dec;  /* start, attack, decay length (seconds) */
    float gain;
    int wave;            /* 0 sine, 1 triangle-ish (sine + soft odd harmonics), 2 bell (sine + 2nd harmonic) */
} Tone;

typedef struct {
    float at, att, dec, gain;
    float cut0, cut1;    /* low-pass corner, start to end (Hz): a whoosh sweeps it */
} Breath;

typedef struct {
    const Tone *tones;
    int ntones;
    Breath breath;
    float length;        /* seconds */
    float wet;           /* reverb amount */
    float level;         /* final peak (0..1) */
} Recipe;

#define T(f0, f1, at, att, dec, g, w) { f0, f1, at, att, dec, g, w }

/* option moves: three soft bells, used in turn */
static const Tone move1[] = { T(328, 328, 0, .006f, .5f, .8f, 2), T(656, 656, 0, .004f, .26f, .22f, 0) };
static const Tone move2[] = { T(260, 260, 0, .006f, .42f, .8f, 2), T(521, 521, 0, .004f, .24f, .22f, 0), T(391, 391, .01f, .01f, .22f, .12f, 0) };
static const Tone move3[] = { T(222, 222, 0, .008f, .45f, .7f, 2), T(197, 197, 0, .008f, .40f, .4f, 0), T(444, 444, 0, .004f, .16f, .15f, 0) };
/* option select: a fifth that swells in */
static const Tone sel[] = { T(258, 258, 0, .26f, .85f, .7f, 1), T(388, 388, .06f, .3f, .8f, .6f, 1), T(776, 776, .1f, .3f, .55f, .1f, 0) };
/* confirm: a rising major arpeggio that rings on */
static const Tone conf[] = { T(258, 258, 0, .01f, .9f, .55f, 2), T(328, 328, .09f, .01f, .9f, .5f, 2), T(388, 388, .18f, .01f, .9f, .5f, 2),
                             T(518, 518, .27f, .01f, 1.1f, .45f, 2) };
/* cancel: a short soft fall */
static const Tone canc[] = { T(636, 632, 0, .006f, .45f, .45f, 2), T(503, 500, .01f, .006f, .45f, .45f, 2), T(386, 384, .02f, .01f, .4f, .3f, 1) };
/* menu back: a hushed, slightly unsettled chord */
static const Tone back[] = { T(352, 350, 0, .1f, .55f, .55f, 1), T(364, 362, .02f, .1f, .55f, .35f, 0), T(398, 396, .04f, .12f, .55f, .5f, 1) };
/* turning to a side: a whoosh and a warm chord (B-flat) */
static const Tone side[] = { T(233, 233, .05f, .38f, 1.25f, .55f, 1), T(292, 292, .07f, .38f, 1.25f, .45f, 1), T(350, 350, .09f, .38f, 1.25f, .4f, 1),
                             T(466, 466, .12f, .3f, .7f, .1f, 0) };
/* turning back to the front: a brighter chord (F) */
static const Tone center[] = { T(350, 350, .05f, .38f, 1.3f, .5f, 1), T(437, 437, .07f, .38f, 1.3f, .45f, 1), T(524, 524, .09f, .38f, 1.3f, .4f, 1),
                               T(700, 700, .12f, .3f, .8f, .1f, 0) };
/* opening a side: a bright shimmer that settles lower */
static const Tone openm[] = { T(668, 640, 0, .15f, .9f, .45f, 1), T(750, 720, .02f, .16f, .9f, .4f, 1), T(556, 540, .1f, .2f, 1.0f, .4f, 1),
                              T(445, 440, .35f, .15f, .8f, .35f, 1) };
/* start (the logo is whole, then glides into the home cubes), in F like the
 * menu music. The moment the logo is whole has its sound in the opening's
 * clip (the "boing"); this carries on from there: a soft glassy F major 9
 * that shimmers in under the name and, as the cubes glide away to the home
 * screen, a breath of air with a run of bells climbing up */
static const Tone start[] = {
    /* the name: a soft F major 9 */
    T(174.6f, 174.6f, .32f, .55f, 2.3f, .26f, 1), T(220.0f, 220.0f, .36f, .55f, 2.3f, .24f, 1), T(261.6f, 261.6f, .40f, .55f, 2.2f, .22f, 1),
    T(329.6f, 329.6f, .44f, .55f, 2.2f, .20f, 1), T(392.0f, 392.0f, .48f, .55f, 2.1f, .18f, 1),
    T(2093.0f, 2093.0f, .70f, .015f, .9f, .05f, 0), T(1760.0f, 1760.0f, 1.05f, .015f, .9f, .04f, 0),   /* soft, not shrill */
    /* the glide: bells climbing (C F A C F) over a breath of air */
    T(523.3f, 523.3f, 3.05f, .004f, .7f, .20f, 2), T(698.5f, 698.5f, 3.15f, .004f, .7f, .20f, 2),
    T(880.0f, 880.0f, 3.25f, .004f, .7f, .20f, 2), T(1046.5f, 1046.5f, 3.35f, .004f, .8f, .20f, 2),
    T(1396.9f, 1396.9f, 3.45f, .004f, 1.2f, .22f, 2),
    T(349.2f, 349.2f, 3.45f, .25f, 1.4f, .16f, 1), T(440.0f, 440.0f, 3.45f, .25f, 1.4f, .14f, 1), T(523.3f, 523.3f, 3.45f, .25f, 1.4f, .12f, 1),
};
static const Tone close_[] = { T(450, 330, 0, .01f, .3f, .5f, 2), T(300, 260, .05f, .02f, .3f, .3f, 0) };
static const Tone err[] = { T(220, 214, 0, .01f, .22f, .6f, 1), T(196, 190, .12f, .01f, .3f, .6f, 1) };
static const Tone key[] = { T(880, 880, 0, .003f, .08f, .3f, 2) };
static const Tone loaded[] = { T(524, 524, 0, .01f, .5f, .45f, 2), T(660, 660, .1f, .01f, .5f, .4f, 2), T(784, 784, .2f, .01f, .7f, .4f, 2) };

/* easter eggs (home.c) */
/* dizzy: a slide whistle wandering up and down, slower and lower as it goes */
static const Tone dizzy[] = { T(620, 940, 0, .02f, .34f, .45f, 0), T(940, 560, .3f, .02f, .36f, .45f, 0), T(560, 860, .62f, .02f, .38f, .42f, 0),
                              T(860, 470, .96f, .02f, .5f, .42f, 0), T(470, 330, 1.42f, .03f, .45f, .35f, 0) };
/* yawn: a long soft fall with breath, then a little contented bell */
static const Tone yawn[] = { T(470, 290, 0, .3f, 1.0f, .5f, 1), T(705, 440, .02f, .3f, .9f, .14f, 0), T(523, 523, 1.15f, .006f, .5f, .25f, 2) };
static const Tone poke[] = { T(1250, 1500, 0, .002f, .07f, .45f, 2) };
static const Tone whee[] = { T(480, 1150, 0, .01f, .38f, .45f, 0), T(720, 1720, .02f, .01f, .36f, .25f, 0), T(1046, 1046, .36f, .004f, .4f, .25f, 2) };
static const Tone boing[] = { T(260, 640, 0, .003f, .1f, .55f, 2), T(640, 470, .09f, .004f, .3f, .4f, 2) };

#define N(a) (int)(sizeof(a) / sizeof(a[0]))
static const Recipe recipes[SFX_COUNT] = {
    [SFX_HOVER] = { move1, N(move1), { 0, .002f, .05f, .04f, 2500, 2000 }, .7f, .25f, .55f },
    [SFX_HOVER2] = { move2, N(move2), { 0, .002f, .05f, .04f, 2500, 2000 }, .55f, .25f, .55f },
    [SFX_HOVER3] = { move3, N(move3), { 0, .002f, .05f, .04f, 2200, 1800 }, .65f, .25f, .55f },
    [SFX_SELECT] = { sel, N(sel), { 0, .02f, .25f, .12f, 2500, 1200 }, 1.1f, .3f, .6f },
    [SFX_CONFIRM] = { conf, N(conf), { 0, .005f, .1f, .08f, 3000, 2000 }, 1.2f, .3f, .6f },
    [SFX_CANCEL] = { canc, N(canc), { 0, .003f, .06f, .05f, 2500, 1500 }, .7f, .25f, .55f },
    [SFX_BACK] = { back, N(back), { 0, .03f, .3f, .12f, 1800, 900 }, .95f, .3f, .55f },
    [SFX_TURN_SIDE] = { side, N(side), { 0, .14f, .7f, .3f, 700, 1600 }, 1.7f, .35f, .62f },
    [SFX_TURN_CENTER] = { center, N(center), { 0, .14f, .7f, .3f, 800, 1800 }, 1.8f, .35f, .62f },
    [SFX_OPEN] = { openm, N(openm), { 0, .06f, .4f, .15f, 2500, 1200 }, 1.4f, .32f, .6f },
    [SFX_START] = { start, N(start), { 2.85f, .45f, 1.1f, .11f, 700, 3200 }, 5.4f, .32f, .62f },
    [SFX_CLOSE] = { close_, N(close_), { 0, .01f, .1f, .06f, 2500, 1500 }, .5f, .22f, .5f },
    [SFX_ERROR] = { err, N(err), { 0, 0, 0, 0, 1000, 1000 }, .55f, .15f, .55f },
    [SFX_KEY] = { key, N(key), { 0, 0, 0, 0, 1000, 1000 }, .15f, .1f, .35f },
    [SFX_LOADED] = { loaded, N(loaded), { 0, .005f, .1f, .05f, 3000, 2000 }, 1.0f, .3f, .55f },
    [SFX_DIZZY] = { dizzy, N(dizzy), { 0, 0, 0, 0, 1000, 1000 }, 2.1f, .3f, .5f },
    [SFX_YAWN] = { yawn, N(yawn), { 0, .35f, 1.0f, .22f, 1800, 600 }, 2.0f, .3f, .5f },
    [SFX_POKE] = { poke, N(poke), { 0, 0, 0, 0, 1000, 1000 }, .2f, .15f, .35f },
    [SFX_WHEE] = { whee, N(whee), { 0, .01f, .3f, .1f, 1500, 4000 }, 1.0f, .3f, .5f },
    [SFX_BOING] = { boing, N(boing), { 0, 0, 0, 0, 1000, 1000 }, .55f, .2f, .45f },
};

/* ---------------------------------------------------------------- tools */

#define SINE_N 4096
static float sine_tab[SINE_N + 1];

static inline float sine(float phase) {   /* phase in turns */
    phase -= floorf(phase);
    float x = phase * SINE_N;
    int i = (int)x;
    float f = x - i;
    return sine_tab[i] + (sine_tab[i + 1] - sine_tab[i]) * f;
}

/* An envelope: a smooth rise over att seconds starting at at, then a decay
 * of about -40 dB over dec seconds. Stepped one sample at a time; the
 * decay is a running multiplication (the GameCube's expf() was most of
 * the time it took to make the sounds). */
typedef struct { float at, att, mul, cur; } Env;
static void env_init(Env *g, float at, float att, float dec) {
    g->at = at;
    g->att = att;
    g->mul = expf(-4.6f / ((dec > 1e-4f ? dec : 1e-4f) * SFX_RATE));
    g->cur = 1.0f;
}
static inline float env_next(Env *g, float t) {   /* t: this sample's time */
    t -= g->at;
    if (t < 0) return 0;
    if (t < g->att) {   /* smooth rise */
        float k = t / g->att;
        return k * k * (3 - 2 * k);
    }
    float e = g->cur;
    g->cur *= g->mul;
    return e;
}

static unsigned rng = 22222;
static inline float noise(void) {
    rng = rng * 1664525u + 1013904223u;
    return (float)(rng >> 9) / (float)(1u << 22) - 1.0f;
}

/* small Schroeder room: four combs into two all-passes */
typedef struct { float *buf; int len, pos; float fb, lp; } Comb;
static float comb_run(Comb *c, float x) {
    float y = c->buf[c->pos];
    c->lp = y * 0.6f + c->lp * 0.4f;            /* darker echoes */
    c->buf[c->pos] = x + c->lp * c->fb;
    if (++c->pos >= c->len) c->pos = 0;
    return y;
}
typedef struct { float *buf; int len, pos; } Allpass;
static float ap_run(Allpass *a, float x) {
    float b = a->buf[a->pos];
    float y = -x + b;
    a->buf[a->pos] = x + b * 0.5f;
    if (++a->pos >= a->len) a->pos = 0;
    return y;
}

int sfx_length(int id) {
    if (id == SFX_ASSEMBLE) return INTRO_CLIP_SAMPLES * 2;
    if (id < 0 || id >= SFX_COUNT || !recipes[id].tones) return 0;
    return (int)(recipes[id].length * SFX_RATE);
}

/* The opening (SFX_ASSEMBLE): wind rising through the haze and an airy pad
 * with a slow heartbeat as the blocks fly in, a soft bloom of air as the
 * light bursts behind the logo, a wide chord, sparkles of dust, and a breath
 * as the cubes glide home. It's a clip made beforehand
 * (tools/sound/make_intro_clip.py, from tools/sound/haze.py), kept as 4-bit
 * IMA ADPCM at 16 kHz and unpacked here to the sounds' 32 kHz. */
static bool render_assemble(int16_t *out, int n) {
    static const int16_t step_tab[89] = {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
        118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
        1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
        6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
        32767 };
    static const int8_t idx_tab[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
    int pred = 0, idx = 0, prev = 0;
    for (int i = 0; i < INTRO_CLIP_SAMPLES; i++) {
        int code = (INTRO_CLIP[i >> 1] >> ((i & 1) * 4)) & 15;
        int step = step_tab[idx], d = step >> 3;
        if (code & 4) d += step;
        if (code & 2) d += step >> 1;
        if (code & 1) d += step >> 2;
        pred += (code & 8) ? -d : d;
        if (pred > 32767) pred = 32767;
        if (pred < -32768) pred = -32768;
        idx += idx_tab[code];
        if (idx < 0) idx = 0;
        if (idx > 88) idx = 88;
        /* 16 kHz to 32 kHz: this sample, and halfway from the last one */
        if (2 * i < n) out[2 * i] = (int16_t)((prev + pred) / 2);
        if (2 * i + 1 < n) out[2 * i + 1] = (int16_t)pred;
        prev = pred;
    }
    return true;
}

bool sfx_render(int id, int16_t *out, int n) {
    if (sine_tab[SINE_N / 4] == 0)
        for (int i = 0; i <= SINE_N; i++) sine_tab[i] = sinf(2.0f * (float)M_PI * i / SINE_N);
    if (id == SFX_ASSEMBLE) return render_assemble(out, n);
    if (id < 0 || id >= SFX_COUNT || !recipes[id].tones) return false;
    const Recipe *r = &recipes[id];
    float *mix = calloc((size_t)n, sizeof(float));
    if (!mix) return false;
    rng = 22222u + (unsigned)id * 977u;
    /* tones, each as two voices a few cents apart (a soft chorus) */
    for (int v = 0; v < r->ntones; v++) {
        const Tone *t = &r->tones[v];
        for (int d = 0; d < 2; d++) {
            float det = d ? 1.0035f : 0.9975f, ph = d * 0.37f;
            Env eg;
            env_init(&eg, t->at, t->att, t->dec);
            for (int i = 0; i < n; i++) {
                float s = (float)i / SFX_RATE;
                float e = env_next(&eg, s);
                if (e < 1e-4f && s > t->at + t->att) break;
                float k = (s - t->at) / (r->length > 0 ? r->length : 1);
                float f = (t->f0 + (t->f1 - t->f0) * (k < 0 ? 0 : k > 1 ? 1 : k)) * det;
                ph += f / SFX_RATE;
                float w = sine(ph);
                if (t->wave == 1) w += 0.18f * sine(ph * 3) + 0.06f * sine(ph * 5);
                else if (t->wave == 2) w += 0.30f * sine(ph * 2) * e + 0.08f * sine(ph * 3.01f) * e * e;
                mix[i] += w * e * t->gain * 0.5f;
            }
        }
    }
    /* the breath: noise through a swept low-pass */
    if (r->breath.gain > 0) {
        float lp1 = 0, lp2 = 0, a = 0;
        Env eg;
        env_init(&eg, r->breath.at, r->breath.att, r->breath.dec);
        for (int i = 0; i < n; i++) {
            float s = (float)i / SFX_RATE;
            float e = env_next(&eg, s);
            if (e < 1e-4f && s > r->breath.at + r->breath.att) break;
            if ((i & 15) == 0) {   /* the sweep moves slowly: every 16 samples is plenty */
                float k = s / (r->breath.att + r->breath.dec);
                float cut = r->breath.cut0 + (r->breath.cut1 - r->breath.cut0) * (k > 1 ? 1 : k);
                a = 1 - expf(-2 * (float)M_PI * cut / SFX_RATE);
            }
            lp1 += (noise() - lp1) * a;
            lp2 += (lp1 - lp2) * a;
            mix[i] += lp2 * e * r->breath.gain * 2.5f;
        }
    }
    /* the room */
    if (r->wet > 0) {
        static const int cl[4] = { 1116, 1188, 1277, 1356 }, al[2] = { 556, 441 };
        Comb c[4];
        Allpass a[2];
        float *mem = calloc(1116 + 1188 + 1277 + 1356 + 556 + 441, sizeof(float));
        if (mem) {
            float *p = mem;
            for (int k = 0; k < 4; k++) { c[k].buf = p; c[k].len = cl[k] * SFX_RATE / 44100; c[k].pos = 0; c[k].fb = 0.78f; c[k].lp = 0; p += cl[k]; }
            for (int k = 0; k < 2; k++) { a[k].buf = p; a[k].len = al[k] * SFX_RATE / 44100; a[k].pos = 0; p += al[k]; }
            for (int i = 0; i < n; i++) {
                float x = mix[i] * 0.25f, y = 0;
                for (int k = 0; k < 4; k++) y += comb_run(&c[k], x);
                y = ap_run(&a[0], y);
                y = ap_run(&a[1], y);
                mix[i] = mix[i] * (1 - r->wet * 0.5f) + y * r->wet;
            }
            free(mem);
        }
    }
    /* level, and a gentle fade at the very end */
    float peak = 1e-6f;
    for (int i = 0; i < n; i++) if (fabsf(mix[i]) > peak) peak = fabsf(mix[i]);
    float g = r->level / peak * 32767.0f;
    int fade = SFX_RATE / 20;
    for (int i = 0; i < n; i++) {
        float v = mix[i] * g;
        if (i > n - fade) v *= (float)(n - i) / fade;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out[i] = (int16_t)v;
    }
    free(mix);
    return true;
}
