#!/usr/bin/env python3
"""
Makes source/font_jp.h: Japanese characters for CubeVerse's text (kana,
Japanese punctuation, full-width forms and the JIS X 0208 kanji), drawn as
4-bit (I4) GameCube textures in pages of 1024 pixels, compressed with DEFLATE.
CubeVerse unpacks them the first time Japanese text is shown.

Glyphs come from M PLUS 1p Medium; the rarer kanji it lacks come from Noto
Sans CJK JP Medium (both SIL Open Font License).

Usage:
    python3 tools/make_font_jp.py MPLUS1p-Medium.ttf NotoSansCJK-Medium.ttc source/font_jp.h

Requires Pillow and fontTools.
"""
import sys, zlib
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont, TTCollection

PX = 19          # the em box, in texels
CELL = 20        # a glyph's cell (a blank texel right and below, so filtering never reaches the next one)
PAGE_W = 1024
PER_ROW = PAGE_W // CELL
ROWS = 1024 // CELL
PER_PAGE = PER_ROW * ROWS
ASCENT = 0.88    # the baseline, as a part of the em box down from its top
SS = 4           # drawn this many times bigger, then shrunk (smooth edges)

mplus_path, noto_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
mcmap = TTFont(mplus_path).getBestCmap()
ncmap = TTCollection(noto_path).fonts[0].getBestCmap()   # 0: Japanese

def jis(cp):
    try:
        chr(cp).encode('shift_jis')
        return True
    except UnicodeEncodeError:
        return False

RANGES = [(0x3000, 0x30FF), (0x31F0, 0x31FF), (0x4E00, 0x9FFF), (0xFF00, 0xFFEF)]
cps = []
for a, b in RANGES:
    for cp in range(a, b + 1):
        if not (jis(cp) or 0xFF61 <= cp <= 0xFF9F):
            continue
        if cp in mcmap or cp in ncmap:
            cps.append(cp)
cps.sort()

mfont = ImageFont.truetype(mplus_path, PX * SS)
nfont = ImageFont.truetype(noto_path, PX * SS, index=0)

def glyph(cp):
    f = mfont if cp in mcmap else nfont
    big = Image.new('L', (PX * SS, PX * SS), 0)
    d = ImageDraw.Draw(big)
    d.text((0, ASCENT * PX * SS), chr(cp), font=f, fill=255, anchor='ls')
    return big.resize((PX, PX), Image.LANCZOS)

npages = (len(cps) + PER_PAGE - 1) // PER_PAGE
pages = []
for p in range(npages):
    chunk = cps[p * PER_PAGE:(p + 1) * PER_PAGE]
    rows = (len(chunk) + PER_ROW - 1) // PER_ROW
    h = (rows * CELL + 7) & ~7
    img = Image.new('L', (PAGE_W, h), 0)
    for i, cp in enumerate(chunk):
        img.paste(glyph(cp), ((i % PER_ROW) * CELL, (i // PER_ROW) * CELL))
    # I4, in 8x8 tiles of 32 bytes
    px = img.load()
    out = bytearray()
    for ty in range(0, h, 8):
        for tx in range(0, PAGE_W, 8):
            for y in range(8):
                for x in range(0, 8, 2):
                    a, b = px[tx + x, ty + y] >> 4, px[tx + x + 1, ty + y] >> 4
                    out.append((a << 4) | b)
    pages.append((h, bytes(out)))

raw = b''.join(d for _, d in pages)
comp = zlib.compressobj(9, zlib.DEFLATED, -15)   # raw DEFLATE, as inflate.c reads it
data = comp.compress(raw) + comp.flush()

L = ['/* font_jp.h - made by tools/make_font_jp.py: Japanese characters (M PLUS 1p',
     ' * and Noto Sans CJK JP, SIL Open Font License) as I4 texture pages,',
     ' * compressed. See gfx.c. */',
     '#pragma once', '#include <stdint.h>',
     '#define JP_PX %d' % PX, '#define JP_CELL %d' % CELL, '#define JP_PAGE_W %d' % PAGE_W,
     '#define JP_PER_ROW %d' % PER_ROW, '#define JP_PER_PAGE %d' % PER_PAGE, '#define JP_ASCENT %.3ff' % ASCENT,
     '#define JP_COUNT %d' % len(cps), '#define JP_PAGES %d' % npages, '#define JP_RAW_BYTES %d' % len(raw),
     'static const uint16_t JP_PAGE_H[JP_PAGES] = { %s };' % ', '.join(str(h) for h, _ in pages),
     'static const uint16_t JP_CODES[JP_COUNT] = {']
for i in range(0, len(cps), 16):
    L.append('    ' + ', '.join('0x%04x' % c for c in cps[i:i + 16]) + ',')
L.append('};')
L.append('static const uint32_t JP_DATA_BYTES = %d;' % len(data))
L.append('static const uint8_t JP_DATA[%d] = {' % len(data))
for i in range(0, len(data), 24):
    L.append('    ' + ','.join(str(b) for b in data[i:i + 24]) + ',')
L.append('};')
open(out_path, 'w').write('\n'.join(L) + '\n')
print(len(cps), 'characters,', npages, 'pages,', len(raw), 'bytes raw,', len(data), 'compressed')
