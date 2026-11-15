#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""GPL-2.0-or-later. Original Warm 200 artistic sRGB recipe, version 1.

Independently authored for Zrno; no external recipe, measured film
response or film-engine code/data. A warm daylight look, not a calibrated
emulation of any physical film stock. GOLD17 is a legacy settings identifier.
"""
from pathlib import Path
from cube_pack import pack


def clamp(x):
    return max(0.0, min(1.0, x))


def transform(rgb):
    r, g, b = rgb
    luma = .2126 * r + .7152 * g + .0722 * b
    # Moderate contrast with soft endpoint lift/compression.
    tone = clamp(.018 + .965 * (luma + .19 * luma * (1-luma) * (2*luma-1)))
    # Gently restrain saturated colours, especially bright highlights.
    saturation = .92 - .08 * luma * luma
    base = [tone + saturation * (c-luma) for c in rgb]
    middle = 4 * luma * (1-luma)
    highlight = luma * luma
    shadow = (1-luma) ** 3
    # Warm mids/highlights and subtle cool shadows. Cross-channel colour
    # mixing in daylight foliage/blues makes this a true 3D transform.
    foliage = max(0.0, g-max(r,b))
    blue = max(0.0, b-max(r,g))
    return [clamp(base[0] + .026*middle + .014*highlight - .004*shadow + .035*foliage),
            clamp(base[1] + .009*middle + .004*highlight - .014*blue),
            clamp(base[2] - .028*middle - .021*highlight + .006*shadow - .018*foliage)]


def cube(size=33):
    return (size, [0.,0.,0.], [1.,1.,1.],
            [transform((r/(size-1),g/(size-1),b/(size-1)))
             for b in range(size) for g in range(size) for r in range(size)])


def bake(lab):
    source = cube()
    for n in (17,33):
        (lab / f'luts/GOLD{n}.LUT').write_bytes(pack(source,n))


if __name__ == '__main__':
    bake(Path(__file__).resolve().parents[1])
    print('Baked independent Warm 200 recipe v1 (legacy GOLD17/GOLD33 filenames).')
