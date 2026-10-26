/* musicsynth.c - see musicsynth.h.
 *
 * The piece is CubeVerse's own: sixteen slow chords (two bars each at 70
 * beats a minute, about 110 seconds before it repeats), mostly open "sus"
 * and added-note chords that drift rather than resolve. Over them:
 *  - pads: three slightly detuned voices per note through a soft low-pass
 *    that slowly opens and closes;
 *  - a glassy pulse of eighth notes picking out the chord, with an echo;
 *  - now and then a high bell;
 *  - a low hum under each chord and a breath of filtered noise;
 *  - all of it in a large, dark reverb.
 * Nothing is sampled; every sound is made here, sample by sample. */
#include "musicsynth.h"
#include <math.h>
#include <string.h>
#include <stdbool.h>

#define RATE ((float)MS_RATE)
#define TAU 6.28318531f

/* ------------------------------------------------------------- the score */

typedef struct { int bass; int pad[4]; } Chord;
static const Chord chords[16] = {
    /* first half */
    { 38, { 57, 62, 64, 66 } },   /* D add9 */
    { 35, { 54, 57, 62, 64 } },   /* B minor 11 */
    { 43, { 54, 59, 61, 66 } },   /* G major 7 #11 */
    { 45, { 57, 62, 64, 69 } },   /* A sus4 */
    { 40, { 55, 59, 62, 66 } },   /* E minor 9 */
    { 36, { 55, 59, 64, 66 } },   /* C major 7 #11 */
    { 43, { 55, 57, 62, 67 } },   /* G sus2 */
    { 45, { 55, 62, 64, 69 } },   /* A 7sus4 */
    /* second half */
    { 35, { 57, 61, 62, 66 } },   /* B minor 9 */
    { 43, { 54, 57, 59, 62 } },   /* G major 9 */
    { 42, { 57, 61, 64, 66 } },   /* D / F# */
    { 40, { 57, 59, 62, 67 } },   /* E minor 11 */
    { 41, { 57, 60, 64, 67 } },   /* F major 7 */
    { 36, { 55, 62, 64, 67 } },   /* C add9 */
    { 43, { 55, 57, 62, 66 } },   /* G major 7 sus2 */
    { 45, { 57, 62, 64, 69 } },   /* A sus4 */
};
#define STEPS_PER_CHORD 16
#define STEP_SECONDS (60.0 / 70.0 / 2.0)   /* eighth notes at 70 bpm */

/* which chord note the pulse plays on each eighth (-1: rest) */
static const signed char pulse_a[16] = { 0, 1, 2, 3, 2, 1, 3, 2, 0, 2, 1, 3, 3, 2, 1, -1 };
static const signed char pulse_b[16] = { 3, 2, 0, 1, 2, 3, 1, 2, 3, 1, 2, 0, 1, 2, 3, -1 };
static const float pulse_vel[16] = { 1, .55f, .75f, .6f, .9f, .5f, .7f, .55f, .95f, .55f, .75f, .6f, .85f, .5f, .65f, 0 };

/* --------------------------------------------------------------- tables */

#define TAB 4096   /* big enough to read without interpolating */
#define TSHIFT 20     /* 32-bit phase -> table index */
static float sin_t[TAB], saw_t[TAB], noise_t[TAB];
static int tabs_ready;

static void make_tables(void) {
    unsigned r = 987654321u;
    for (int i = 0; i < TAB; i++) {
        r = r * 1664525u + 1013904223u;
        noise_t[i] = (float)(r >> 8) / 8388608.0f - 1.0f;
        float p = (float)i / TAB, s = 0;
        sin_t[i] = sinf(TAU * p);
        for (int h = 1; h <= 14; h++) s += sinf(TAU * h * p) / h * (1.0f - h / 18.0f);
        saw_t[i] = s * 0.55f;
    }
    tabs_ready = 1;
}

/* Phases are 32-bit fractions of a turn, so they wrap by themselves and need
 * no float-to-integer conversions (slow on the GameCube's processor). */
static inline uint32_t phase_inc(float hz) { return (uint32_t)(hz / RATE * 4294967296.0f); }
static inline float mtof(float m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }
static inline float decay_k(float seconds) { return expf(-1.0f / (seconds * RATE)); }

static unsigned rng = 1234567;
static inline float frand(void) {   /* 0..1 */
    rng = rng * 1664525u + 1013904223u;
    return (float)(rng >> 8) / 16777216.0f;
}

/* --------------------------------------------------------------- voices */

enum { V_OFF, V_PAD, V_BASS, V_PLUCK, V_BELL };
typedef struct {
    int kind;
    uint32_t ph[3], inc[3], mph, minc;   /* oscillator phases; FM modulator phase */
    float a, a_inc;          /* attack progress 0..1 */
    float lvl, dk, rel_k;    /* level after the attack; decay while held; decay once let go */
    int hold;                /* samples until let go (-1: held until told) */
    float gain, pl, pr;      /* loudness and pan */
    float idx, idx_k;        /* FM brightness and its decay */
} Voice;

#define NV 32
static Voice v[NV];

static int lite;   /* the lighter mix: fewer notes at once, a smaller hall */
void ms_set_lite(int on) { lite = on; }

static Voice *grab(void) {
    int busy = 0;
    for (int i = 0; i < NV; i++) if (v[i].kind != V_OFF) busy++;
    if (!lite || busy < 16)
        for (int i = 0; i < NV; i++) if (v[i].kind == V_OFF) return &v[i];
    /* all busy: take the quietest plucked note */
    Voice *best = NULL;
    for (int i = 0; i < NV; i++)
        if ((v[i].kind == V_PLUCK || v[i].kind == V_BELL) && (!best || v[i].lvl < best->lvl)) best = &v[i];
    return best;
}

static void pan(Voice *x, float p) {   /* -1 left .. +1 right, equal power */
    float a = (p + 1) * 0.25f * 3.14159265f;
    x->pl = cosf(a);
    x->pr = sinf(a);
}

static void start_pad(int note, float vel) {
    Voice *x = grab();
    if (!x) return;
    memset(x, 0, sizeof(*x));
    float f = mtof((float)note);
    static const float det[3] = { -0.0041f, 0.0003f, 0.0046f };
    for (int k = 0; k < 3; k++) { x->inc[k] = phase_inc(f * (1 + det[k])); x->ph[k] = (uint32_t)(frand() * 4294967295.0f); }
    x->kind = V_PAD;
    x->a_inc = 1.0f / (2.4f * RATE);
    x->lvl = 1;
    x->dk = 1;
    x->rel_k = decay_k(1.1f);
    x->hold = -1;
    x->gain = 0.09f * vel;
}

static void start_bass(int note) {
    Voice *x = grab();
    if (!x) return;
    memset(x, 0, sizeof(*x));
    x->kind = V_BASS;
    x->inc[0] = phase_inc(mtof((float)note));
    x->a_inc = 1.0f / (1.6f * RATE);
    x->lvl = 1;
    x->dk = 1;
    x->rel_k = decay_k(0.9f);
    x->hold = -1;
    x->gain = 0.15f;
    pan(x, 0);
}

static void start_pluck(int note, float vel, float p) {
    Voice *x = grab();
    if (!x) return;
    memset(x, 0, sizeof(*x));
    x->kind = V_PLUCK;
    x->inc[0] = phase_inc(mtof((float)note));
    x->a_inc = 1.0f / (0.004f * RATE);
    x->lvl = 1;
    x->dk = decay_k(0.42f);
    x->rel_k = x->dk;
    x->hold = -1;
    x->gain = 0.075f * vel;
    x->minc = phase_inc(mtof((float)note) * 2.0f);
    x->idx = 1.6f * vel;
    x->idx_k = decay_k(0.10f);
    pan(x, p);
}

static void start_bell(int note, float vel) {
    Voice *x = grab();
    if (!x) return;
    memset(x, 0, sizeof(*x));
    x->kind = V_BELL;
    x->inc[0] = phase_inc(mtof((float)note));
    x->a_inc = 1.0f / (0.003f * RATE);
    x->lvl = 1;
    x->dk = decay_k(1.25f);
    x->rel_k = x->dk;
    x->hold = -1;
    x->gain = 0.055f * vel;
    x->minc = phase_inc(mtof((float)note) * 3.5f);
    x->idx = 1.3f;
    x->idx_k = decay_k(0.5f);
    pan(x, frand() * 1.2f - 0.6f);
}

static void let_go(int kind) {
    for (int i = 0; i < NV; i++) if (v[i].kind == kind) v[i].hold = 0;
}

/* ---------------------------------------------------------------- effects */

typedef struct { float *buf; int len, pos; float lp; } Comb;
typedef struct { float *buf; int len, pos; } Allpass;
static float rv_mem[2][4 * 900 + 2 * 340];
static Comb combs[2][4];
static Allpass aps[2][2];
#define DELAY_LEN ((int)(MS_RATE * 0.642857f))   /* a dotted eighth */
static float dly[2][MS_RATE * 2 / 3];
static int dly_pos;
static float dly_lp[2];

static void fx_init(void) {
    static const int cl[4] = { 1116, 1277, 1422, 1557 }, al[2] = { 556, 441 };
    memset(rv_mem, 0, sizeof(rv_mem));
    memset(dly, 0, sizeof(dly));
    dly_pos = 0;
    dly_lp[0] = dly_lp[1] = 0;
    for (int c = 0; c < 2; c++) {
        float *p = rv_mem[c];
        for (int k = 0; k < 4; k++) {
            combs[c][k] = (Comb){ p, (cl[k] + c * 23) * MS_RATE / 44100, 0, 0 };
            p += 900;
        }
        for (int k = 0; k < 2; k++) {
            aps[c][k] = (Allpass){ p, (al[k] + c * 23) * MS_RATE / 44100, 0 };
            p += 340;
        }
    }
}

static inline float comb(Comb *c, float x) {
    float y = c->buf[c->pos];
    c->lp = y * 0.62f + c->lp * 0.38f;
    c->buf[c->pos] = x + c->lp * 0.86f;
    if (++c->pos >= c->len) c->pos = 0;
    return y;
}
static inline float allpass(Allpass *a, float x) {
    float b = a->buf[a->pos];
    a->buf[a->pos] = x + b * 0.5f;
    if (++a->pos >= a->len) a->pos = 0;
    return b - x;
}

/* ------------------------------------------------------------ sequencing */

static long step;             /* the next eighth to play */
static double to_step;        /* samples until it */
static double t_sec;          /* time, for the slow movements */
static float air_l[4], air_r[4];
static float pad_l1, pad_l2, pad_r1, pad_r2;
static unsigned noise_pos;

void ms_reset(void) {
    if (!tabs_ready) make_tables();
    memset(v, 0, sizeof(v));
    fx_init();
    step = 0;
    to_step = 0;
    t_sec = 0;
    rng = 1234567;
    memset(air_l, 0, sizeof(air_l));
    memset(air_r, 0, sizeof(air_r));
    pad_l1 = pad_l2 = pad_r1 = pad_r2 = 0;
}

static void play_step(long s) {
    int ci = (int)((s / STEPS_PER_CHORD) % 16), pos = (int)(s % STEPS_PER_CHORD);
    long pass = s / (STEPS_PER_CHORD * 16);
    const Chord *c = &chords[ci];
    bool second = ci >= 8;
    if (pos == 0) {
        let_go(V_PAD);
        let_go(V_BASS);
        for (int k = 0; k < 4; k++) start_pad(c->pad[k], k == 3 ? 0.8f : 1.0f);
        if (!(pass == 0 && ci == 0)) start_bass(c->bass);
    }
    /* the pulse: sparse at the very start, then every eighth */
    int idx = (second ? pulse_b : pulse_a)[pos];
    bool intro = pass == 0 && ci < 2;
    if ((pass & 1) && (ci == 4 || ci == 5)) idx = -1;   /* a quiet moment every other time round */
    if (idx >= 0 && !(intro && (pos & 3))) {
        if (!(pass == 0 && ci == 0 && pos < 8)) {
            float vel = pulse_vel[pos] * (0.82f + 0.3f * frand());
            int note = c->pad[idx] + 12;
            if (second && pos >= 12 && (pass & 1)) note += 12;   /* now and then up high */
            start_pluck(note, vel, (pos & 1) ? 0.35f : -0.35f);
        }
    }
    /* bells, every other chord once things are going */
    if (!(pass == 0 && ci < 2) && !(ci & 1)) {
        if (pos == 0) start_bell(c->pad[3] + 24, 0.9f);
        if (pos == 10) start_bell(c->pad[1] + 24, 0.6f);
        if (second && pos == 5) start_bell(c->pad[2] + 24, 0.5f);
    }
}

static inline float soft_clip(float x) {
    if (x > 3) x = 3;
    if (x < -3) x = -3;
    return x * (27 + x * x) / (27 + 9 * x * x);
}

/* One voice's envelope for the next sample (0 once it has finished). */
static inline float envelope(Voice *x) {
    if (x->a < 1) {
        x->a += x->a_inc;
        if (x->a > 1) x->a = 1;
        return x->a * x->a * (3 - 2 * x->a);
    }
    if (x->hold == 0) x->lvl *= x->rel_k;
    else {
        x->lvl *= x->dk;
        if (x->hold > 0) x->hold--;
    }
    return x->lvl;
}

#define BLOCK 64

void ms_render(int16_t *out, int frames, float gain) {
    if (!tabs_ready) ms_reset();
    const double step_len = STEP_SECONDS * RATE;
    float padL[BLOCK], padR[BLOCK], dryL[BLOCK], dryR[BLOCK], send[BLOCK], echo[BLOCK];
    int done = 0;
    while (done < frames) {
        if (to_step <= 0) {
            play_step(step++);
            to_step += step_len;
        }
        int n = frames - done;
        if (n > BLOCK) n = BLOCK;
        if (n > (int)ceil(to_step)) n = (int)ceil(to_step);
        if (n < 1) n = 1;
        memset(padL, 0, sizeof(float) * n);
        memset(padR, 0, sizeof(float) * n);
        memset(dryL, 0, sizeof(float) * n);
        memset(dryR, 0, sizeof(float) * n);
        memset(send, 0, sizeof(float) * n);
        memset(echo, 0, sizeof(float) * n);
        /* the voices, one at a time through the block */
        for (int k = 0; k < NV; k++) {
            Voice *x = &v[k];
            if (x->kind == V_OFF) continue;
            float g = x->gain;
            switch (x->kind) {
            case V_PAD:
                for (int i = 0; i < n; i++) {
                    float e = envelope(x) * g;
                    float o0 = saw_t[x->ph[0] >> TSHIFT], o1 = saw_t[x->ph[1] >> TSHIFT], o2 = saw_t[x->ph[2] >> TSHIFT];
                    x->ph[0] += x->inc[0]; x->ph[1] += x->inc[1]; x->ph[2] += x->inc[2];
                    padL[i] += (o0 * 0.8f + o1 * 0.5f + o2 * 0.2f) * e;
                    padR[i] += (o0 * 0.2f + o1 * 0.5f + o2 * 0.8f) * e;
                }
                break;
            case V_BASS:
                for (int i = 0; i < n; i++) {
                    float e = envelope(x) * g;
                    float s = sin_t[x->ph[0] >> TSHIFT] + 0.22f * sin_t[(x->ph[0] << 1) >> TSHIFT];
                    x->ph[0] += x->inc[0];
                    dryL[i] += s * e;
                    dryR[i] += s * e;
                    send[i] += s * e * 0.1f;
                }
                break;
            default: {   /* plucks and bells: two-operator FM */
                float sl = x->kind == V_PLUCK ? 0.45f : 0.7f, el = x->kind == V_PLUCK ? 0.5f : 0.2f;
                for (int i = 0; i < n; i++) {
                    float e = envelope(x) * g;
                    float m = sin_t[x->mph >> TSHIFT];
                    int32_t pm = (int32_t)(x->idx * m * 683565275.0f);   /* idx/(2 pi) of a turn */
                    float s = sin_t[(x->ph[0] + (uint32_t)pm) >> TSHIFT] * e;
                    x->ph[0] += x->inc[0];
                    x->mph += x->minc;
                    x->idx *= x->idx_k;
                    dryL[i] += s * x->pl;
                    dryR[i] += s * x->pr;
                    send[i] += s * sl;
                    echo[i] += s * el;
                }
            }
            }
            if (x->a >= 1 && x->lvl < 0.003f) x->kind = V_OFF;
        }
        /* slow movements, once per block */
        long ci = (step / STEPS_PER_CHORD) % 16;
        float cut = 600 + 400 * (0.5f + 0.5f * sinf((float)(TAU * t_sec / 23.0))) + (ci >= 8 ? 160 : 0);
        float fa = 1 - expf(-TAU * cut / RATE);
        float air_amp = 0.010f + 0.008f * (0.5f + 0.5f * sinf((float)(TAU * t_sec / 13.0 + 1.0)));
        const float aa = 1 - expf(-TAU * 1400 / RATE), ab = 1 - expf(-TAU * 260 / RATE);
        for (int i = 0; i < n; i++) {
            /* the pads share one soft low-pass */
            pad_l1 += (padL[i] - pad_l1) * fa; pad_l2 += (pad_l1 - pad_l2) * fa;
            pad_r1 += (padR[i] - pad_r1) * fa; pad_r2 += (pad_r1 - pad_r2) * fa;
            float dl = dryL[i] + pad_l2, dr = dryR[i] + pad_r2;
            float sn = send[i] + (pad_l2 + pad_r2) * 0.35f;
            /* air: two bands of noise, gently breathing */
            for (int c = 0; c < 2; c++) {
                float *a = c ? air_r : air_l;
                noise_pos = noise_pos * 1664525u + 1013904223u;
                float nz = noise_t[noise_pos >> TSHIFT];
                a[0] += (nz - a[0]) * aa; a[1] += (a[0] - a[1]) * aa;
                a[2] += (a[1] - a[2]) * ab;
                float band = (a[1] - a[2]) * air_amp * 2.2f;
                if (c) dr += band; else dl += band;
                sn += band * 0.3f;
            }
            /* ping-pong echo of the pulse */
            float el = dly[0][dly_pos], er = dly[1][dly_pos];
            dly_lp[0] += (er - dly_lp[0]) * 0.35f;   /* cross-fed: echoes swap sides */
            dly_lp[1] += (el - dly_lp[1]) * 0.35f;
            dly[0][dly_pos] = echo[i] + dly_lp[0] * 0.40f;
            dly[1][dly_pos] = dly_lp[1] * 0.40f;
            if (++dly_pos >= DELAY_LEN) dly_pos = 0;
            dl += el * 0.42f;
            dr += er * 0.42f;
            sn += (el + er) * 0.25f;
            /* the hall */
            float in = sn * 0.22f, wl = 0, wr = 0;
            if (lite) {
                wl = comb(&combs[0][0], in) + comb(&combs[0][2], in);
                wr = comb(&combs[1][1], in) + comb(&combs[1][3], in);
                wl *= 1.6f; wr *= 1.6f;
            } else {
                for (int k = 0; k < 4; k++) { wl += comb(&combs[0][k], in); wr += comb(&combs[1][k], in); }
            }
            for (int k = 0; k < 2; k++) { wl = allpass(&aps[0][k], wl); wr = allpass(&aps[1][k], wr); }
            float L = soft_clip((dl + wl * 0.6f) * 1.6f) * gain, R = soft_clip((dr + wr * 0.6f) * 1.6f) * gain;
            out[(done + i) * 2] = (int16_t)(L * 30000);
            out[(done + i) * 2 + 1] = (int16_t)(R * 30000);
        }
        done += n;
        to_step -= n;
        t_sec += n / RATE;
    }
}
