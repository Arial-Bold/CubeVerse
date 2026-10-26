# Makes source/textures.h: the CubeVerse mark (from the design's A2 SVG,
# rendered big and shrunk, so its fine lines survive) as a tiled RGB5A3
# texture. Run in tools/art: python3 ../make_textures.py ../../source/textures.h
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
logo = big.resize((LW, LH), Image.LANCZOS)
# a see-through border all round, so filtering at the edges never picks up
# picture from the opposite side
PAD = 4
padded = Image.new('RGBA', (LW + 2 * PAD, LH + 2 * PAD), (0, 0, 0, 0)); padded.paste(logo, (PAD, PAD))
logo = bleed(padded)
logo.save('logo_tex.png')
L = ['/* textures.h - made by tools/make_textures.py: the CubeVerse mark as a',
     ' * tiled RGB5A3 texture (drawn shrunk, so it stays smooth), with a',
     ' * see-through border LOGO_PAD texels wide round the picture. */',
     '#pragma once', '#include <stdint.h>', '']
for name, im, extra in (('LOGO', logo, '#define LOGO_PAD %d\n' % PAD),):
    tw, th, data = tiled_rgb5a3(im)
    L.append('#define %s_W %d\n#define %s_H %d\n#define %s_TW %d\n#define %s_TH %d\n%s' % (name, im.width, name, im.height, name, tw, name, th, extra))
    L.append('static const uint16_t %s_TEX[%d] __attribute__((aligned(32))) = {' % (name, len(data)))
    for i in range(0, len(data), 16): L.append('    ' + ', '.join('0x%04x' % v for v in data[i:i + 16]) + ',')
    L.append('};\n')
open(sys.argv[1], 'w').write('\n'.join(L))
print(logo.size)
