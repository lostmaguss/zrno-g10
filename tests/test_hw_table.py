# SPDX-License-Identifier: GPL-2.0-or-later
"""DIGIC hardware colour tables: format, packing, Canon's identity, and that
every card look has a current .HWT that reproduces its .LUT closely."""
from pathlib import Path
import sys, tempfile
import numpy as np
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import hw_table as W
from cube_pack import pack as pack_lut

identity = (2, [0., 0., 0.], [1., 1., 1.], [(r, g, b) for b in (0., 1.) for g in (0., 1.) for r in (0., 1.)])
with tempfile.TemporaryDirectory() as tmp:
    lib = W.colour_library(tmp)
    # An identity look gives Canon's own "My Colors Off" table: Y = 2 * BT.601
    # luma, U/V likewise, along the grey axis exactly 0, 64, ..., 512, 512.
    words = W.pack(W.fit_nodes(lib, pack_lut(identity, 17)))
    assert words.dtype == np.dtype('<u4') and len(words) == 1000 and not (words >> 30).any()
    grey = (words[[111 * k for k in range(10)]] >> 20) & 0x3ff
    assert list(grey) == [0, 64, 128, 192, 256, 320, 384, 448, 512, 512], list(grey)   # as Canon's preset 0
    assert not (((words[[111 * k for k in range(9)]] >> 10) & 0x3ff) % 1024).any()
    k = np.arange(1000); r, g, b = k % 10, (k // 10) % 10, k // 100
    inside = (r < 9) & (g < 9) & (b < 9)
    yuv = W.unpack(words).reshape(-1, 3)
    expect = np.stack([np.minimum(r, 8), np.minimum(g, 8), np.minimum(b, 8)], 1)[inside] * 32.0
    expect = expect @ W.BT601.T                         # node 8 sits at 256, as in Canon's table
    assert np.abs(yuv - expect).max() <= 1.0, np.abs(yuv - expect).max()
    # Node 9 repeats node 8 on every axis.
    grid = words.reshape(10, 10, 10)
    assert (grid[9] == grid[8]).all() and (grid[:, 9] == grid[:, 8]).all() and (grid[:, :, 9] == grid[:, :, 8]).all()
    # No look's glow: an identity look keeps today's red + green/5 linear push.
    tint = tuple(W.halation_tint(lib, pack_lut(identity, 17)))
    assert abs(tint[0] - 256) <= 8 and abs(tint[1] - 51) <= 6 and tint[2] <= 4, tint
    # Amber: red + green/2, so on the identity about 256, 128, 0.
    amber = tuple(W.halation_tint(lib, pack_lut(identity, 17), W.GREEN['amber']))
    assert abs(amber[0] - 256) <= 8 and abs(amber[1] - 128) <= 8 and amber[2] <= 4, amber
    hl = W.graded_highlight(lib, pack_lut(identity, 17))   # identity: today's highlight table
    assert np.abs(hl.astype(int) - W.camera_table('halo_highlight', 256)).max() <= 8
    # Signed fields round-trip.
    nodes = np.zeros((729, 3)); nodes[:, 1] = -200; nodes[:, 2] = 199.5
    back = W.unpack(W.pack(nodes))
    assert (back[:, 1] == -200).all() and (back[:, 2] == 199.5).all()
    # Deterministic, and every card look's .HWT is current and close.
    report = []
    for lut_path in sorted(W.folder.glob('*.LUT')):
        lut = lut_path.read_bytes()
        stored, tint, highlight = W.read_hwt(lut_path.with_suffix('.HWT'))
        fresh = W.pack(W.fit_nodes(lib, lut))
        assert (stored == fresh).all() and tint == tuple(W.halation_tint(lib, lut)) and (highlight == W.graded_highlight(lib, lut)).all(), \
            f'{lut_path.stem}.HWT is stale: run make luts'
        assert (np.diff(highlight.astype(int)) >= 0).all() and highlight[0] == 0
        assert all(0 <= t <= 1024 for t in tint) and max(tint) > 0
        amber = W.read_hwa(lut_path.with_suffix('.HWA'))
        assert amber == tuple(W.halation_tint(lib, lut, W.GREEN['amber'])), f'{lut_path.stem}.HWA is stale: run make luts'
        # Amber is the yellower glow: more green against red than Red's (grey looks: brighter).
        assert all(0 <= t <= 1024 for t in amber) and (amber[1] * tint[0] > tint[1] * amber[0] or (tint[0] == tint[1] and amber[0] > tint[0])), (lut_path.stem, tint, amber)
        rng = np.random.default_rng(5)
        rgb = np.clip(rng.integers(0, 256, (50000, 1)) + rng.normal(0, 28, (50000, 3)), 0, 255).astype(np.int64)
        e = np.abs(W.apply(stored, rgb) - W.look_rgb(lib, lut, rgb)).max(1)
        assert np.percentile(e, 50) <= 2 and np.percentile(e, 99) <= 12, (lut_path.stem, np.percentile(e, 99))
        report.append(f'{lut_path.stem} p99 {np.percentile(e, 99):.0f}')
print('Hardware colour tables: Canon identity reproduced, node 9 clamps, signed packing, all', len(report),
      'card looks current and close, Red and Amber halation tints current (near-grey p99:', ', '.join(report) + ')')
