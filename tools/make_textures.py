# Makes source/textures.h: the CubeVerse mark (from the design's A2 SVG,
# rendered big and shrunk, so its fine lines survive) and the hand pointer
# (from the drawing), as tiled RGB5A3 textures.
import sys
from PIL import Image, ImageDraw
import numpy as np
def tiled_rgb5a3(im):
    w, h = im.size; tw, th = (w + 3) & ~3, (h + 3) & ~3
    a = np.zeros((th, tw, 4), np.uint8); a[:h, :w] = np.array(im)
    out = []
    for ty in range(0, th, 4):
        for tx in range(0, tw, 4):
            for y in range(4):
                for x in range(4):
                    r, g, b, al = (int(v) for v in a[ty + y, tx + x])
                    if al >= 248: v = 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
                    else: v = ((al >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)
                    out.append(v)
    return tw, th, out
def bleed(im):
    # give see-through pixels their neighbours' colour, so edges don't darken when filtered
    a = np.array(im).astype(np.float32); h, w = a.shape[:2]
    for _ in range(4):
        al = a[..., 3] > 8
        acc = np.zeros((h, w, 3)); cnt = np.zeros((h, w))
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                sh = np.roll(np.roll(a, dy, 0), dx, 1); m = np.roll(np.roll(al, dy, 0), dx, 1)
                acc += sh[..., :3] * m[..., None]; cnt += m
        fill = (~al) & (cnt > 0)
        a[fill, :3] = acc[fill] / cnt[fill][:, None]
        a[fill, 3] = np.maximum(a[fill, 3], 1)
    a[..., 3] = np.where(a[..., 3] <= 1, 0, a[..., 3])
    return Image.fromarray(a.astype(np.uint8), 'RGBA')
# the mark
big = Image.open('logo_big.png').convert('RGBA'); big = big.crop(big.getbbox())
LW = 176; LH = round(LW * big.height / big.width)
logo = bleed(big.resize((LW, LH), Image.LANCZOS))
# the hand: white inside, black outline, outside see-through
src = Image.open('hand_src.png').convert('L')
bw = src.point(lambda v: 255 if v > 128 else 0)
flood = bw.convert('RGB'); ImageDraw.floodfill(flood, (0, 0), (255, 0, 0))
f = np.array(flood); g = np.array(src)
outside = (f[..., 0] == 255) & (f[..., 1] == 0)
rgba = np.zeros(g.shape + (4,), np.uint8)
rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = g
rgba[..., 3] = np.where(outside, 255 - np.clip(255 - g.astype(int), 0, 255), 255)   # outside: only the dark outline shows
rgba[..., 3] = np.where(outside, 255 - g, 255)
hand = Image.fromarray(rgba, 'RGBA'); bb = hand.getbbox(); hand = hand.crop(bb)
# fingertip: the topmost opaque row, its middle
al = np.array(hand)[..., 3]; ys, xs = np.nonzero(al > 128); top = ys.min(); tipx = xs[ys == top].mean()
HH = 64; HW = round(HH * hand.width / hand.height)
sx = HW / hand.width; tip = (tipx * sx, top * sx)
hand = bleed(hand.resize((HW, HH), Image.LANCZOS))
logo.save('logo_tex.png'); hand.save('hand_tex.png')
L = ['/* textures.h - made by tools/make_textures.py: the CubeVerse mark and the',
     ' * hand pointer as tiled RGB5A3 textures (drawn shrunk, so they stay smooth). */',
     '#pragma once', '#include <stdint.h>', '']
for name, im, extra in (('LOGO', logo, ''), ('HAND', hand, '#define HAND_TIP_X %.2ff\n#define HAND_TIP_Y %.2ff\n' % tip)):
    tw, th, data = tiled_rgb5a3(im)
    L.append('#define %s_W %d\n#define %s_H %d\n#define %s_TW %d\n#define %s_TH %d\n%s' % (name, im.width, name, im.height, name, tw, name, th, extra))
    L.append('static const uint16_t %s_TEX[%d] __attribute__((aligned(32))) = {' % (name, len(data)))
    for i in range(0, len(data), 16): L.append('    ' + ', '.join('0x%04x' % v for v in data[i:i + 16]) + ',')
    L.append('};\n')
open(sys.argv[1], 'w').write('\n'.join(L))
print(logo.size, hand.size, tip)
