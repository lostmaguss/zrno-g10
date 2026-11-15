# SPDX-License-Identifier: GPL-2.0-or-later
"""Recipe structure, endpoints, grayscale continuity and packed provenance."""
from pathlib import Path
import random
import sys
import math
lab=Path(__file__).resolve().parents[1];sys.path.insert(0,str(lab/'tools'))
from bake_warm import transform,cube
from cube_pack import pack
source=cube()
for n in (17,33):assert (lab/f'luts/GOLD{n}.LUT').read_bytes()==pack(source,n)
rng=random.Random(8912)
for c in [(0,0,0),(1,1,1)]+[tuple(rng.random() for _ in range(3)) for i in range(1000)]:
    assert all(math.isfinite(v) and 0<=v<=1 for v in transform(c))
neutral=[transform((i/255,)*3) for i in range(256)]
assert all(neutral[i+1][c]>=neutral[i][c] for i in range(255) for c in range(3))
assert neutral[128][0]>neutral[128][1]>neutral[128][2]
assert all(v<.04 for v in neutral[0]) and all(v>.94 for v in neutral[-1])
assert (lab/'card/CHDK/FILM/LOOK.LUT').read_bytes()==(lab/'luts/GOLD17.LUT').read_bytes()
assert 'GOLD17.LUT|Warm 200' in (lab/'card/CHDK/FILM/LUTS.TXT').read_text()
print('Warm 200: deterministic recipe/17+33 packing, bounded output, monotone neutral ramp, warm midtones and compatible legacy filename passed')
