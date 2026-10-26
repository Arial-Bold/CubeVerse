"""A happy theme-park crowd, made from scratch: voiced chatter (bright, with
lively ups and downs), laughs and cheers, built into the CubeVerse opening:
busier and busier, a cheering high point, a sudden hush, then a fun tone
when the logo is whole."""
import sys, wave, numpy as np
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from voices import SR, V, reson, band, env, place, room, PL, FR
rng = np.random.default_rng(21)
LEN = 8.0
T_PEAK, T_HUSH, T_LOGO = 4.95, 5.2, 5.6


def voiced(segs, f0s, scale, breath=0.12):
    """segs: list of (vowel_from, vowel_to, seconds); f0s: pitch (Hz) at each segment's start and the end"""
    out = []
    for k, (v1, v2, d) in enumerate(segs):
        n = int(d * SR)
        t = np.linspace(0, 1, n)
        f0 = f0s[k] + (f0s[k + 1] - f0s[k]) * t
        ph = np.cumsum(f0 / SR)
        src = np.zeros(n)
        for h in range(1, 30):
            if f0.max() * h > SR / 2.3: break
            src += np.sin(2 * np.pi * h * ph) / h ** 1.1
        src += rng.standard_normal(n) * breath
        y = np.zeros(n)
        steps = 4
        seg = n // steps + 1
        for s in range(steps):
            u = s / max(1, steps - 1)
            F = [(a + (b - a) * u) * scale for a, b in zip(V[v1], V[v2])]
            a0, a1 = s * seg, min(n, (s + 1) * seg)
            pad0, pad1 = max(0, a0 - 200), min(n, a1 + 200)
            x = src[pad0:pad1]
            yy = reson(x, F[0], 80) + reson(x, F[1], 100) * 0.7 + reson(x, F[2], 150) * 0.35
            y[a0:a1] += yy[a0 - pad0:a0 - pad0 + (a1 - a0)]
        out.append(y * env(n, 0.12, 0.2))
    return np.concatenate(out) * 0.25


def consonant(c, scale):
    if c in PL:
        n = int(0.025 * SR)
        lo, hi = PL[c]
        return band(rng.standard_normal(n), lo, hi) * np.exp(-np.linspace(0, 6, n)) * 0.12
    lo, hi = FR[c]
    n = int(0.05 * SR)
    return band(rng.standard_normal(n), lo, hi) * env(n, .3, .4) * (0.12 if c in ('s', 'sh') else 0.06)


def chatter(base, scale):
    """a short burst of talk: 2-5 syllables, a little melody of its own"""
    out = []
    f = base * (0.9 + 0.25 * rng.random())
    for k in range(rng.integers(2, 6)):
        c = rng.choice(['k', 'b', 't', 'p', 'g', 'd', 's', 'sh', 'f', 'h', 'n'])
        if c != 'n' and c != 'h': out.append(consonant(c, scale))
        v1, v2 = rng.choice(list(V.keys())), rng.choice(list(V.keys()))
        d = 0.08 + 0.1 * rng.random()
        f2 = f * (0.85 + 0.35 * rng.random())
        out.append(voiced([(v1, v2, d)], [f, f2], scale))
        f = f2
    return np.concatenate(out)


def laugh(base, scale):
    out = []
    f = base * 1.25
    for k in range(rng.integers(3, 6)):
        out.append(consonant('h', scale) * 1.5)
        out.append(voiced([('a', 'a', 0.09)], [f, f * 0.95], scale, breath=0.35))
        out.append(np.zeros(int(0.04 * SR)))
        f *= 0.93
    return np.concatenate(out) * 1.1


def cheer(base, scale):
    kind = rng.integers(0, 3)
    if kind == 0:   # "woo!"
        return voiced([('u', 'o', 0.42)], [base * 1.1, base * 1.9], scale) * 1.3
    if kind == 1:   # "yay!"
        return np.concatenate([consonant('h', scale) * 0.5, voiced([('e', 'i', 0.35)], [base * 1.3, base * 1.6], scale) * 1.2])
    return voiced([('a', 'ae', 0.3)], [base * 1.2, base * 1.7], scale) * 1.2   # "aah!"


SPEAKERS = [(130, 0.86), (150, 0.9), (210, 1.0), (235, 1.04), (260, 1.1), (285, 1.15), (320, 1.22), (190, 0.97)]


def crowd_track():
    tr = np.zeros((2, int((LEN + 1) * SR)))
    t = 0.3
    while t < T_HUSH:
        k = min(1, (t - 0.3) / (T_PEAK - 0.3))
        dens = 5 + 30 * k ** 1.4
        base, scale = SPEAKERS[rng.integers(0, len(SPEAKERS))]
        r = rng.random()
        late = t > T_PEAK - 1.1
        if late and r < 0.45: sig = cheer(base, scale)
        elif r < 0.12: sig = laugh(base, scale)
        else: sig = chatter(base, scale)
        gain = (0.45 + 0.55 * k) * (0.6 + 0.6 * rng.random())
        place(tr, sig, t, rng.random() * 2 - 1, gain)
        t += rng.exponential(1 / dens)
    # the hush: everything fades out fast just before the logo is whole
    n = tr.shape[1]
    i0, i1 = int(T_HUSH * SR), int((T_HUSH + 0.12) * SR)
    fade = np.ones(n)
    fade[i0:i1] = np.linspace(1, 0, i1 - i0) ** 2
    fade[i1:] = 0
    tr = room(tr, 0.3) * fade
    return tr / np.abs(tr).max() * 0.6


# ---- fun tones for the logo ----
def tone(f0, f1, dur, wave='bell', gain=0.3, att=0.004, dec=None):
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = f0 + (f1 - f0) * np.clip(t / dur, 0, 1)
    ph = np.cumsum(f / SR)
    if wave == 'bell': y = np.sin(2 * np.pi * ph) + 0.35 * np.sin(4 * np.pi * ph) * np.exp(-t * 6) + 0.12 * np.sin(2 * np.pi * 3.01 * ph) * np.exp(-t * 9)
    elif wave == 'mar': y = np.sin(2 * np.pi * ph) + 0.5 * np.sin(2 * np.pi * 3.93 * ph) * np.exp(-t * 40)
    else: y = np.sin(2 * np.pi * ph) + 0.15 * np.sin(4 * np.pi * ph)
    e = np.minimum(1, t / att) * np.exp(-t / (dec or dur / 3))
    return y * e * gain


def mono(n): return np.zeros(int(n * SR))


def put(buf, sig, t):
    i = int(t * SR); j = min(len(buf), i + len(sig)); buf[i:j] += sig[:j - i]


def fun_tada():
    """a bouncy marimba run up an F chord and a sparkling 'ding' on top"""
    b = mono(2.6)
    for k, f in enumerate([349.2, 440.0, 523.3, 698.5]):
        put(b, tone(f, f, 0.6, 'mar', 0.35, dec=0.18), k * 0.075)
    put(b, tone(1396.9, 1396.9, 2.0, 'bell', 0.3, dec=0.6), 0.32)
    put(b, tone(1760.0, 1760.0, 1.8, 'bell', 0.16, dec=0.5), 0.36)
    put(b, tone(2093.0, 2093.0, 1.6, 'bell', 0.12, dec=0.45), 0.40)
    for f in (174.6, 261.6, 349.2):
        put(b, tone(f, f, 2.2, 'soft', 0.16, att=0.02, dec=0.7), 0.32)
    return b


def fun_slide():
    """a cartoon slide whistle up, a 'pop', and a bright chord"""
    b = mono(2.6)
    n = int(0.32 * SR); t = np.arange(n) / SR
    f = 480 * (1600 / 480) ** (t / 0.32) * (1 + 0.02 * np.sin(2 * np.pi * 9 * t))
    put(b, np.sin(2 * np.pi * np.cumsum(f / SR)) * np.minimum(1, t / 0.03) * 0.25, 0)
    pn = int(0.03 * SR)
    put(b, band(rng.standard_normal(pn), 300, 3000) * np.exp(-np.linspace(0, 8, pn)) * 0.8, 0.31)
    for f, g in ((698.5, .22), (880.0, .2), (1046.5, .2), (1396.9, .16)):
        put(b, tone(f, f, 2.0, 'bell', g, dec=0.55), 0.33)
    put(b, tone(174.6, 174.6, 1.8, 'soft', 0.2, att=0.01, dec=0.6), 0.33)
    return b


def fun_boing():
    """a springy 'boing' that bounces into a happy chord with sparkles"""
    b = mono(2.6)
    n = int(0.45 * SR); t = np.arange(n) / SR
    f = 220 * (1 + 0.9 * np.exp(-t * 9)) * (1 + 0.06 * np.sin(2 * np.pi * 16 * t) * np.exp(-t * 4))
    put(b, (np.sin(2 * np.pi * np.cumsum(f / SR)) + 0.3 * np.sin(4 * np.pi * np.cumsum(f / SR))) * np.exp(-t * 5) * 0.3, 0)
    for k, f in enumerate([523.3, 659.3, 784.0]):
        put(b, tone(f, f, 1.4, 'bell', 0.18, dec=0.4), 0.3 + k * 0.06)
    put(b, tone(1046.5, 1046.5, 1.4, 'soft', 0.12, att=0.012, dec=0.45), 0.48)   # the top note, rounder
    # soft twinkles: round sine tones a step above the chord, eased in (no sharp bells)
    for k, f in enumerate([880.0, 784.0, 1046.5, 880.0, 1174.7, 1046.5]):
        put(b, tone(f, f, 0.6, 'soft', 0.05 - k * 0.004, att=0.015, dec=0.16), 0.58 + k * 0.09)
    put(b, tone(130.8, 130.8, 1.8, 'soft', 0.2, att=0.01, dec=0.6), 0.3)
    return b


def rd(p):
    w = wave.open(p)
    return np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32) / 32768


