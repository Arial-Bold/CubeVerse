#!/usr/bin/env python3
"""Makes source/icons.h: the block icons inside the home cubes. Each icon is
two little pictures made of cubes (A and B); every cube is paired with one in
the other picture (the nearest, overall), so the icon can change shape by
sliding its cubes. Usage: python3 tools/make_icons.py source/icons.h"""
import sys
import numpy as np
from scipy.optimize import linear_sum_assignment

PITCH = 9.4                 # grid spacing (the area is about 103 x 75)
SIZE = 8.2                  # one cube
CX, CY = 51.5, 36           # where the pictures are centred

ICONS = {
    # Activity: speech bubble -> feed
    'activity': ("""
.#########.
#.........#
#.........#
#.........#
#.........#
.##..#####.
...#.#.....
...##......
""", """
##.#######.
##.#####...
...........
##.#######.
##.######..
...........
##.#####...
##.........
"""),
    # Communities: tiles -> three cubes
    'communities': ("""
###.###.###
###.###.###
...........
...........
###.###.###
###.###.###
""", """
...#####...
...#.#.#...
...#...#...
##.#####.##
##.......##
##.......##
"""),
    # Notifications: bell -> "!"
    'notifications': ("""
.....#.....
...##.##...
..#.....#..
..#.....#..
.#.......#.
.#########.
...........
....###....
""", """
....###....
.#..###..#.
..#.###.#..
....###....
##..###..##
...........
....###....
"""),
    # My page: person -> profile card
    'mypage': ("""
....###....
...#...#...
....###....
...........
..#######..
.#.......#.
#.........#
###########
""", """
###########
#.........#
#.###.###.#
#.###.##..#
#.###.###.#
#.........#
###########
"""),
}


def cells(pic):
    rows = [r for r in pic.strip('\n').split('\n')]
    pts = [(x, y) for y, r in enumerate(rows) for x, ch in enumerate(r) if ch == '#']
    w = max(len(r) for r in rows) * PITCH - (PITCH - SIZE)
    h = len(rows) * PITCH - (PITCH - SIZE)
    ox, oy = CX - w / 2, CY - h / 2
    return [(ox + x * PITCH, oy + y * PITCH) for x, y in pts]


def pair(a, b):
    """every cube in the bigger picture gets a partner (some double up in the smaller)"""
    n = max(len(a), len(b))
    def grow(p):
        p = list(p)
        reps = [p[i % len(p)] for i in range(n - len(p))]
        return p + reps
    A, B = np.array(grow(a)), np.array(grow(b))
    d = ((A[:, None, :] - B[None, :, :]) ** 2).sum(-1)
    r, c = linear_sum_assignment(d)
    out = [(A[i][0], A[i][1], B[j][0], B[j][1]) for i, j in zip(r, c)]
    out.sort(key=lambda t: (round(t[1], 1), t[0]))   # top to bottom, so the change ripples down
    return out


L = ['/* icons.h - made by tools/make_icons.py: the block icons inside the home',
     ' * cubes. Each block is ICON_BLOCK across, at (ax,ay), sliding to (bx,by) as',
     ' * the icon changes shape: Activity speech bubble -> feed, Communities',
     ' * tiles -> three cubes, Notifications bell -> "!", My page person ->',
     ' * profile card. */',
     '#pragma once',
     'typedef struct { float ax, ay, bx, by; } IconBlock;',
     '#define ICON_BLOCK %.1ff' % SIZE, '']
for name, (a, b) in ICONS.items():
    p = pair(cells(a), cells(b))
    print(name, len(cells(a)), len(cells(b)), '->', len(p))
    L.append('static const IconBlock icon_%s[%d] = {' % (name, len(p)))
    for i in range(0, len(p), 4):
        L.append('    ' + ' '.join('{%.1ff, %.1ff, %.1ff, %.1ff},' % t for t in p[i:i + 4]))
    L.append('};')
open(sys.argv[1], 'w').write('\n'.join(L) + '\n')
