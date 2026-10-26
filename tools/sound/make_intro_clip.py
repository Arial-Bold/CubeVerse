"""Usage: python3 tools/sound/make_intro_clip.py

Renders CubeVerse's opening sound (the crowd, the landing clicks, the hush
and the soft boing) to mono 16 kHz and packs it as 4-bit IMA ADPCM into
source/intro_clip.h. Same as crowd_3_boing_soft, mixed to one channel."""
import sys, wave, numpy as np
import os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import crowd as C
from crowd import SR
crowd = C.crowd_track().mean(0)
asm = C.rd(os.path.join(HERE, 'landing_clicks.wav'))   # the pieces' clicks (32 kHz)
n = int(8.0 * SR)
mix = np.zeros(n)
mix += crowd[:n] * 1.2
clicks = asm.copy(); clicks[int(C.T_HUSH * SR):] = 0
mix[:len(clicks)] += clicks * 0.35
f = C.fun_boing(); i = int(C.T_LOGO * SR)
mix[i:i + len(f)] += f[:n - i]
mix = np.tanh(mix * 1.1) * 0.9
mix[-800:] *= np.linspace(1, 0, 800)
# to 16 kHz (the clip has nothing above 8 kHz worth keeping)
from scipy.signal import resample_poly
m16 = resample_poly(mix, 1, 2)
pcm = np.clip(m16 * 32767, -32768, 32767).astype(np.int32)
w = wave.open(os.path.join(HERE, 'intro_clip_16k.wav'), 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
w.writeframes(pcm.astype(np.int16).tobytes()); w.close()
# IMA ADPCM
STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
IDX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]
pred, idx, out, nib = 0, 0, bytearray(), []
for s in pcm:
    step = STEP[idx]; diff = int(s) - pred; code = 0
    if diff < 0: code = 8; diff = -diff
    d = step >> 3
    if diff >= step: code |= 4; diff -= step; d += step
    if diff >= step >> 1: code |= 2; diff -= step >> 1; d += step >> 1
    if diff >= step >> 2: code |= 1; d += step >> 2
    pred = pred - d if code & 8 else pred + d
    pred = max(-32768, min(32767, pred))
    idx = max(0, min(88, idx + IDX[code]))
    nib.append(code)
if len(nib) & 1: nib.append(0)
for k in range(0, len(nib), 2): out.append(nib[k] | (nib[k + 1] << 4))
L = ['/* intro_clip.h - made by tools/sound/make_intro_clip.py: the opening\'s sound (a',
     ' * happy crowd getting busier, the pieces clicking into place, a hush and a',
     ' * soft "boing" when the logo is whole). CubeVerse\'s own, made from scratch.',
     ' * Mono, 16 kHz, 4-bit IMA ADPCM. */',
     '#pragma once', '#include <stdint.h>',
     '#define INTRO_CLIP_RATE 16000', '#define INTRO_CLIP_SAMPLES %d' % len(pcm),
     'static const uint8_t INTRO_CLIP[%d] = {' % len(out)]
for k in range(0, len(out), 32): L.append('    ' + ','.join(str(b) for b in out[k:k + 32]) + ',')
L.append('};')
open(os.path.join(HERE, '..', '..', 'source', 'intro_clip.h'), 'w').write('\n'.join(L) + '\n')
print(len(pcm), 'samples,', len(out), 'bytes')
