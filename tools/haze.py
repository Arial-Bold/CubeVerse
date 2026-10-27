"""haze.py - CubeVerse's opening sound (all original, made from scratch):
wind rising through the haze, an airy pad of open fifths and a slow soft
heartbeat while the blocks fly in; a soft bloom of air as the light bursts
behind the logo, then a wide F chord; sparkles of dust in the light; a
gentle breath as the cubes glide home. Timed to splash.c: blocks fly in
from 0.3 s, the logo is whole at 5.6 s, the name at 5.9 s, the cubes glide
home from 8.7 s. Used by make_intro_clip.py."""
import wave, numpy as np
from scipy.signal import fftconvolve, butter, sosfilt
SR = 32000
LEN = 11.5
N = int(LEN * SR)
t = np.arange(N) / SR
rng = np.random.default_rng(11)
T_LOGO, T_NAME, T_GLIDE = 5.6, 5.9, 8.7


def env(t0, att, hold, rel, shape=2.0):
    e = np.zeros(N)
    a = (t >= t0) & (t < t0 + att)
    e[a] = ((t[a] - t0) / att) ** shape
    h = (t >= t0 + att) & (t < t0 + att + hold)
    e[h] = 1
    r = t >= t0 + att + hold
    e[r] = np.exp(-(t[r] - t0 - att - hold) / rel)
    return e


def lp(x, f, order=2):
    return sosfilt(butter(order, min(f, SR * 0.45), 'low', fs=SR, output='sos'), x)


def bp(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo, min(hi, SR * 0.45)], 'band', fs=SR, output='sos'), x)


def sweep_bp(x, f0, f1, t0, t1, q=3.0, steps=60):
    """noise through a band that sweeps from f0 to f1 (log), in steps"""
    out = np.zeros(N)
    i0, i1 = int(t0 * SR), int(t1 * SR)
    edges = np.linspace(i0, i1, steps + 1).astype(int)
    for k in range(steps):
        f = f0 * (f1 / f0) ** (k / steps)
        a, b = max(0, edges[k] - 800), min(N, edges[k + 1] + 800)
        y = bp(x[a:b], f / (1 + 1 / q), f * (1 + 1 / q))
        w = np.ones(b - a)
        out[a:b] += y * w
    return out


def sine(f, phase=0):
    return np.sin(2 * np.pi * np.cumsum(np.broadcast_to(f, (N,)) / SR) + phase)


def reverb(x, secs=2.8, wet=0.35, dark=4000, seed=0):
    r = np.random.default_rng(seed)
    n = int(secs * SR)
    ir = r.standard_normal(n) * np.exp(-np.arange(n) / SR * 6.9 / secs)
    ir = lp(ir, dark)
    ir /= np.sqrt((ir ** 2).sum())
    y = fftconvolve(x, ir)[:N]
    return x * (1 - wet) + y * wet * 3


def stereo(l, r):
    return np.stack([l, r])


def out(name, st):
    st = st / (np.abs(st).max() + 1e-9) * 0.85
    st = np.tanh(st * 1.15) / np.tanh(1.15)
    st[:, -2000:] *= np.linspace(1, 0, 2000)
    w = wave.open(name + '.wav', 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((st.T * 32767 * 0.95).astype(np.int16).tobytes()); w.close()


F1 = 43.65   # a low F
noise = rng.standard_normal(N)
noise2 = rng.standard_normal(N)


def pad(freqs, e, bright=1200, det=0.004):
    """a soft, wide chord: each note a few slightly detuned sines with a little 2nd/3rd harmonic"""
    l = np.zeros(N); r = np.zeros(N)
    for i, f in enumerate(freqs):
        for d, side in ((-det, 0), (det, 1), (0, 2)):
            s = sine(f * (1 + d), rng.random() * 6) + 0.25 * sine(2 * f * (1 + d)) + 0.08 * sine(3 * f * (1 + d))
            if side == 0: l += s
            elif side == 1: r += s
            else: l += s * 0.5; r += s * 0.5
    return lp(l, bright) * e, lp(r, bright) * e


# ---------------------------------------------------------------- the opening
def opening():
    """the opening, in stereo (2 x N), before levelling"""
    rise = np.clip((t - 0.2) / 5.4, 0, 1)
    wind = lp(noise, 900) * (0.6 + 0.6 * rise) * (0.7 + 0.3 * np.sin(2 * np.pi * 0.23 * t))
    wind2 = lp(noise2, 1400) * (0.6 + 0.6 * rise) * (0.7 + 0.3 * np.sin(2 * np.pi * 0.31 * t + 1))
    # an airy choir-like pad (open fifths) swelling with the haze
    ce = env(0.3, 2.5, 2.6, 0.4)
    cl, cr = pad([F1 * 4, F1 * 6, F1 * 8, F1 * 12], ce * 0.6, bright=1300, det=0.006)
    beat = np.zeros(N)
    for k in range(9):   # a slow, soft heartbeat under it
        t0 = 1.2 + k * (0.62 - 0.03 * k)
        if t0 > T_LOGO - 0.3: break
        beat += sine(55 * np.exp(-np.clip(t - t0, 0, None) * 8) + 40) * env(t0, 0.01, 0, 0.18) * (0.5 + 0.06 * k)
    # the burst of light behind the logo: a big soft bloom, not a hit
    bloom = sweep_bp(noise, 300, 5000, T_LOGO - 0.8, T_LOGO + 0.3, q=1.5) * env(T_LOGO - 0.8, 0.85, 0.05, 0.5, 3) * 0.7
    pe = env(T_LOGO, 0.5, 2.6, 1.8)
    pl, pr = pad([F1 * 2, F1 * 4, F1 * 5, F1 * 6, F1 * 9, F1 * 10], pe, bright=2600)
    dust = np.zeros(N)
    for k in range(18):   # sparkles of dust in the light
        t0 = 2.2 + rng.random() * 6.5
        f = rng.choice([1046.5, 1174.7, 1318.5, 1568.0])
        dust += sine(f) * env(t0, 0.02, 0, 0.35) * 0.05
    breath = sweep_bp(noise2, 600, 2000, T_GLIDE - 0.3, T_GLIDE + 1.5, q=2) * env(T_GLIDE - 0.3, 0.9, 0.3, 0.7) * 0.25
    l = reverb(wind + beat + bloom + cl + pl + dust + breath, 3.8, 0.4, seed=3)
    r = reverb(wind2 + beat + bloom + cr + pr + dust * 0.8 + breath, 3.8, 0.4, seed=4)
    return stereo(l, r)



if __name__ == '__main__':
    out('haze_preview', opening())
