"""Voice building blocks for the opening's crowd (crowd.py): breath noise and
voiced sounds shaped by the mouth.
A whisper is breath noise shaped by the mouth: each sound here is noise
through three formant resonators (vowels) or a noise band (s, f, sh), or a
short burst (k, b, t). Voices differ by mouth size, speed, pan and level."""
import numpy as np
from scipy.signal import lfilter, butter

SR = 32000
rng = np.random.default_rng(7)

# vowels: F1, F2, F3 (Hz)
V = {
    'i': (280, 2250, 2900), 'u': (320, 870, 2250), 'er': (490, 1350, 1690), 'a': (730, 1090, 2440),
    'e': (530, 1840, 2480), 'o': (500, 900, 2400), 'ae': (660, 1720, 2410), 'uh': (620, 1200, 2550),
}
# fricatives: band (lo, hi)
FR = {'s': (4200, 9000), 'sh': (2000, 5500), 'f': (1500, 7000), 'v': (1200, 6000), 'h': (500, 4000), 'th': (1800, 8000)}
# plosives: burst band
PL = {'k': (1500, 3500), 'b': (200, 1500), 't': (3000, 7000), 'p': (300, 2000), 'g': (1200, 3000), 'd': (2500, 6000)}


def reson(x, f, bw):
    """a band-pass resonance (zeros at 0 and at the top, so no broad hiss leaks past)"""
    r = np.exp(-np.pi * bw / SR)
    th = 2 * np.pi * f / SR
    g = (1 - r * r) / 2
    return lfilter([g, 0, -g], [1, -2 * r * np.cos(th), r * r], x) * 3.0


def band(x, lo, hi):
    hi = min(hi, SR / 2 * 0.95)
    b, a = butter(2, [lo / (SR / 2), hi / (SR / 2)], btype='band')
    return lfilter(b, a, x)


def env(n, att=0.15, rel=0.25):
    e = np.ones(n)
    a, r = max(1, int(n * att)), max(1, int(n * rel))
    e[:a] = np.linspace(0, 1, a) ** 1.5
    e[-r:] *= np.linspace(1, 0, r) ** 1.5
    return e


def vowel(v1, v2, dur, scale):
    """a whispered vowel gliding from v1 to v2 (done in a few steps)"""
    n = int(dur * SR)
    out = np.zeros(n)
    steps = 6
    seg = n // steps + 1
    noise = rng.standard_normal(n + 2048)
    for k in range(steps):
        t = k / max(1, steps - 1)
        F = [(a + (b - a) * t) * scale for a, b in zip(V[v1], V[v2])]
        a0, a1 = max(0, k * seg - 256), min(n, (k + 1) * seg + 256)
        x = noise[a0:a1 + 0]
        y = reson(x, F[0], 90) * 1.0 + reson(x, F[1], 120) * 0.7 + reson(x, F[2], 170) * 0.35
        w = np.ones(a1 - a0)
        if a0 > 0: w[:512] = np.linspace(0, 1, 512) if a1 - a0 > 512 else w[:512]
        if a1 < n: w[-512:] = np.linspace(1, 0, 512) if a1 - a0 > 512 else w[-512:]
        out[a0:a1] += y[:a1 - a0] * w
    return out * env(n, 0.2, 0.3) * 0.6


def fric(f, dur, scale):
    n = int(dur * SR)
    lo, hi = FR[f]
    g = {'s': 0.55, 'sh': 0.5, 'f': 0.18, 'v': 0.16, 'h': 0.25, 'th': 0.15}[f]
    return band(rng.standard_normal(n), lo * scale, hi * scale) * env(n, 0.25, 0.35) * g


def plos(p, scale):
    lo, hi = PL[p]
    gap = np.zeros(int(0.035 * SR))
    n = int(0.03 * SR)
    burst = band(rng.standard_normal(n), lo * scale, hi * scale) * np.exp(-np.linspace(0, 6, n)) * 0.6
    asp = band(rng.standard_normal(int(0.03 * SR)), 800, 5000) * env(int(0.03 * SR), 0.1, 0.8) * 0.15
    return np.concatenate([gap, burst, asp])


def say(phones, scale=1.0, speed=1.0):
    """phones: list of ('v', v1, v2, dur) | ('f', name, dur) | ('p', name)"""
    parts = []
    for ph in phones:
        if ph[0] == 'v': parts.append(vowel(ph[1], ph[2], ph[3] / speed, scale))
        elif ph[0] == 'f': parts.append(fric(ph[1], ph[2] / speed, scale))
        elif ph[0] == 'p': parts.append(plos(ph[1], scale))
        elif ph[0] == '_': parts.append(np.zeros(int(ph[1] / speed * SR)))
    # small overlaps so it flows
    out = parts[0]
    for p in parts[1:]:
        ov = min(int(0.012 * SR), len(out), len(p))
        out = np.concatenate([out[:-ov], out[-ov:] + p[:ov], p[ov:]]) if ov else np.concatenate([out, p])
    return out


# "CubeVerse", whispered: k-yoo-b-v-er-s
CUBEVERSE = [('p', 'k'), ('v', 'i', 'u', 0.20), ('p', 'b'), ('f', 'v', 0.07), ('v', 'er', 'er', 0.20), ('f', 's', 0.18)]

SYL_C = ['k', 'b', 't', 'p', 'g', 'd', 's', 'sh', 'f', 'h', 'th', 'v']
SYL_V = list(V.keys())


def babble_word(scale):
    """a few nonsense syllables: sounds like talk, says nothing"""
    ph = []
    for _ in range(rng.integers(1, 4)):
        c = rng.choice(SYL_C)
        ph.append(('p', c) if c in PL else ('f', c, 0.06 + 0.05 * rng.random()))
        a, b = rng.choice(SYL_V), rng.choice(SYL_V)
        ph.append(('v', a, b, 0.08 + 0.12 * rng.random()))
    if rng.random() < 0.4: ph.append(('f', 's' if rng.random() < .6 else 'sh', 0.08))
    return say(ph, scale, 0.9 + 0.4 * rng.random())


def place(track, sig, t, pan, gain):
    i = int(t * SR)
    if i >= track.shape[1] or i + len(sig) <= 0: return
    j = min(track.shape[1], i + len(sig))
    s = sig[max(0, -i):j - i] * gain
    a = (pan + 1) * np.pi / 4
    track[0, max(0, i):j] += s * np.cos(a)
    track[1, max(0, i):j] += s * np.sin(a)


def room(st, wet=0.25):
    out = st.copy()
    for d, g in ((0.031, .35), (0.047, .28), (0.071, .2), (0.113, .14), (0.167, .09)):
        k = int(d * SR)
        out[0, k:] += st[1, :-k] * g * wet * 2
        out[1, k:] += st[0, :-k] * g * wet * 2
    return out


def voiced(vowel_name, f0, dur, scale=1.0):
    """a soft sung vowel (for the choir option): a breathy pulse train through the formants"""
    n = int(dur * SR)
    t = np.arange(n) / SR
    vib = 1 + 0.006 * np.sin(2 * np.pi * 5.2 * t + rng.random() * 6)
    ph = np.cumsum(f0 * vib / SR)
    src = np.zeros(n)
    for h in range(1, 24):
        if f0 * h > SR / 2.2: break
        src += np.sin(2 * np.pi * h * ph) / h ** 1.2
    src += rng.standard_normal(n) * 0.15
    F = [f * scale for f in V[vowel_name]]
    y = reson(src, F[0], 80) + reson(src, F[1], 110) * 0.6 + reson(src, F[2], 160) * 0.3
    return y * env(n, 0.35, 0.4) * 0.2
