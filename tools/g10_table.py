#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Convert Zrno's G12 colour tables (.HWT) to the G10 1.03B format (.G10).

G12: 1,000 words, a 10 x 10 x 10 grid (index r + 10g + 100b, the last layer a
copy of node 8), BT.601 at x2 (grey node k = Y 64k).
G10: 729 words, a 9 x 9 x 9 grid (index r + 9g + 81b), BT.601 at x2.5 (grey
node k = Y 80k). Both pack Y in bits 20-29 and signed U, V in bits 10-19 and
0-9 (docs/PORT-G10.md).

Writes card-g10/CHDK/MAGUS/<NAME>.G10 (2,916 bytes), with the look's .HWT and
.HWA beside it (halation tint), for every look in
card/CHDK/FILM/LUTS.TXT, and LOOKS.TXT listing them in menu order.

Usage: python3 tools/g10_table.py [--check-rom-identity rom/probe1/RAMHEAP.BIN]
"""
from pathlib import Path
import argparse, sys
import numpy as np

LAB = Path(__file__).resolve().parents[1]
SRC = LAB / 'card/CHDK/FILM'
OUT = LAB / 'card-g10/CHDK/MAGUS'


def unpack(words):
    d = np.asarray(words, np.uint32)
    y = ((d >> 20) & 0x3ff).astype(np.int64)
    u = ((d >> 10) & 0x3ff).astype(np.int64); u[u >= 512] -= 1024
    v = (d & 0x3ff).astype(np.int64); v[v >= 512] -= 1024
    return y, u, v


def pack(y, u, v):
    y = np.clip(y, 0, 1023); u = np.clip(u, -512, 511); v = np.clip(v, -512, 511)
    return ((y << 20) | ((u & 0x3ff) << 10) | (v & 0x3ff)).astype('<u4')


def g12_to_g10(words):
    """1,000 G12 words -> 729 G10 words."""
    assert len(words) == 1000
    y, u, v = unpack(words)
    i = np.arange(729)
    r, g, b = i % 9, (i // 9) % 9, i // 81
    src = r + 10 * g + 100 * b
    k = 1.25
    return pack(np.rint(y[src] * k).astype(np.int64), np.rint(u[src] * k).astype(np.int64),
                np.rint(v[src] * k).astype(np.int64))


def g12_identity():
    """BT.601 x2 on nodes at 32k (node 8 = 256), as tests/test_grade.c."""
    i = np.arange(1000)
    R, G, B = 64.0 * (i % 10), 64.0 * ((i // 10) % 10), 64.0 * (i // 100)
    R, G, B = [np.minimum(x, 512) for x in (R, G, B)]
    y = .299 * R + .587 * G + .114 * B
    u = -.168736 * R - .331264 * G + .5 * B
    v = .5 * R - .418688 * G - .081312 * B
    return pack(np.rint(y).astype(np.int64), np.rint(u).astype(np.int64), np.rint(v).astype(np.int64))


def looks():
    for line in (SRC / 'LUTS.TXT').read_text().splitlines():
        if '|' in line:
            lut, name = line.strip().split('|', 1)
            yield Path(lut).stem, name


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--check-rom-identity', type=Path,
                   help='RAMHEAP.BIN from the RAM probe: compare the converted identity with Canon\'s table 0')
    a = p.parse_args()

    if a.check_rom_identity:
        canon = np.frombuffer(a.check_rom_identity.read_bytes()[:729 * 4], '<u4')
        mine = g12_to_g10(g12_identity())
        worst = max(int(np.abs(x - y).max()) for x, y in zip(unpack(mine), unpack(canon)))
        print(f'converted identity vs Canon\'s G10 table 0: worst {worst} step(s)')
        if worst > 4:
            sys.exit('too far off: the conversion is wrong')

    OUT.mkdir(parents=True, exist_ok=True)
    listing = []
    for stem, name in looks():
        data = (SRC / 'LUTS' / (stem + '.HWT')).read_bytes()
        assert len(data) == 4272, f'{stem}.HWT: not a 4272-byte .HWT'
        words = g12_to_g10(np.frombuffer(data[:4000], '<u4'))
        y, u, v = unpack(words)
        # Grey rises (a look that clips the highlights may wobble by a step or two).
        assert y[0] <= 80 and all(y[91 * k] <= y[91 * (k + 1)] + 4 for k in range(8)), f'{stem}: grey not in order'
        (OUT / (stem + '.G10')).write_bytes(words.tobytes())
        # Halation's tint and highlight weights (.HWT trailer) and Amber tint (.HWA),
        # read by camera/g10_magus.c; the colour table inside the .HWT is the G12's.
        (OUT / (stem + '.HWT')).write_bytes(data)
        hwa = SRC / 'LUTS' / (stem + '.HWA')
        if hwa.is_file():
            (OUT / (stem + '.HWA')).write_bytes(hwa.read_bytes())
        listing.append(f'{stem}|{name}')
        print(f'{stem:8} {name:16} grey Y {y[0]:4} .. {y[728]:4}')
    (OUT / 'LOOKS.TXT').write_text('\n'.join(listing) + '\n')
    print(f'{len(listing)} looks in {OUT.relative_to(LAB)}')


if __name__ == '__main__':
    main()
