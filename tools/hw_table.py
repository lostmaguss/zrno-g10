#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Film looks as DIGIC 4 hardware colour tables (.HWT, next to each .LUT).

The G12's develop has a 3D colour table that My Colors uses: RGB in, YCbCr out, 9 nodes
per axis every 32 levels of 8-bit RGB, tetrahedral interpolation. A table is
1,000 little-endian 32-bit words on a 10x10x10 grid, index r + 10g + 100b,
with Y in bits 20-29 and signed U and V in bits 10-19 and 0-9, all on a 9-bit
scale (Canon's identity: Y = 2 * luma). Node 9 lies past 8-bit white; it
repeats node 8, which clamps as the CPU LUT does.

After the table comes a 272-byte trailer for halation, which the DIGIC engine
adds after Canon's grade: "HWTINT1\0"; the tint, three little-endian u16 Q8
gains (red, green, blue) and a zero u16, saying how the look turns today's
pre-grade glow (red linear light plus a fifth as much green) into output
colour; and 256 bytes of highlight weight per graded luma, today's
highlight weight of the luma before the look (the look's grey response
inverted), so the glow mask and threshold see the scene as before.

Beside it, a 16-byte .HWA holds the tint for the Amber halation colour:
"HWAMBR1\0", three u16 Q8 gains for glow that is red linear light plus half
as much green, and a zero u16. It is a file of its own so that firmware from
before the Halation colour row, which takes only a 4272-byte .HWT, still
reads every look.

Each look is fitted by least squares against the camera's own LUT code
(film_lut_pixel) on a fixed uniform RGB sample, so the result depends on the
.LUT file alone. Usage: hw_table.py [LUT ...] (default: every card look).
"""
from pathlib import Path
import ctypes as C
import subprocess
import sys
import tempfile
import re
import numpy as np

lab = Path(__file__).resolve().parents[1]
folder = lab / 'card/CHDK/FILM/LUTS'
BT601 = np.array([[.299, .587, .114], [-.168736, -.331264, .5], [.5, -.418688, -.081312]])
SAMPLES, SEED, RIDGE = 800000, 0x6712, 1e-3
WRAPPER = r'''
#include "film_colour.h"
void hw_batch(const void *lut, const unsigned char *in, unsigned char *out, int n) {
    for (int i = 0; i < n; i++) film_lut_pixel(lut, in + 3 * i, out + 3 * i);
}
'''


def colour_library(tmp):
    """The camera's colour code, with a batch entry point for film_lut_pixel."""
    tmp = Path(tmp)
    (tmp / 'batch.c').write_text(WRAPPER)
    out = tmp / 'colour.so'
    sources = [lab / 'camera' / s for s in ('film_colour.c', 'film_retro.c', 'film_toy.c', 'film_print.c')]
    subprocess.run(['cc', '-O2', '-shared', '-fPIC', '-I', str(lab / 'camera'), '-o', str(out), str(tmp / 'batch.c')] +
                   [str(s) for s in sources], check=True)
    lib = C.CDLL(str(out))
    lib.hw_batch.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_int]
    lib.film_lut_validate.argtypes = [C.c_void_p, C.c_size_t]
    return lib


def look_rgb(lib, lut, rgb):
    """The look's output for N x 3 uint8 RGB inputs, via the camera code."""
    buf = C.create_string_buffer(lut)
    assert lib.film_lut_validate(buf, len(lut)) == 0, 'not a valid G12 LUT'
    src = np.ascontiguousarray(rgb, np.uint8)
    out = np.empty_like(src)
    lib.hw_batch(buf, src.ctypes.data, out.ctypes.data, len(src))
    return out


def tetra(rgb):
    """Node indices (r + 9g + 81b) and weights of the hardware's tetrahedral
    interpolation for N x 3 integer RGB: cells of 64 on the 9-bit input."""
    t = np.asarray(rgb, np.int64) * 2
    cell = np.minimum(t >> 6, 7)
    frac = (t - cell * 64) / 64.0
    base = cell[:, 0] + 9 * cell[:, 1] + 81 * cell[:, 2]
    order = np.argsort(-frac, axis=1, kind='stable')
    f = np.take_along_axis(frac, order, 1)
    step = np.array([1, 9, 81])[order]
    idx = np.empty((len(t), 4), np.int64)
    idx[:, 0] = base
    idx[:, 1] = base + step[:, 0]
    idx[:, 2] = idx[:, 1] + step[:, 1]
    idx[:, 3] = idx[:, 2] + step[:, 2]
    w = np.stack([1 - f[:, 0], f[:, 0] - f[:, 1], f[:, 1] - f[:, 2], f[:, 2]], 1)
    return idx, w


NODE_RGB = np.stack([np.arange(729) % 9, (np.arange(729) // 9) % 9, np.arange(729) // 81], 1) * 32


def fit_nodes(lib, lut):
    """729 nodes of 8-bit-scale YCbCr minimising the squared error of the
    interpolated table against the look, lightly pulled toward the look's own
    value at each node (keeps nodes no sample reaches sensible)."""
    rng = np.random.default_rng(SEED)
    sample = rng.integers(0, 256, (SAMPLES, 3))
    target = look_rgb(lib, lut, sample).astype(np.float64) @ BT601.T
    idx, w = tetra(sample)
    A = np.zeros((729, 729))
    np.add.at(A, (idx[:, :, None].repeat(4, 2), idx[:, None, :].repeat(4, 1)), w[:, :, None] * w[:, None, :])
    b = np.zeros((729, 3))
    np.add.at(b, idx, w[..., None] * target[:, None, :])
    prior = look_rgb(lib, lut, np.minimum(NODE_RGB, 255)).astype(np.float64) @ BT601.T
    k = RIDGE * SAMPLES / 729
    return np.linalg.solve(A + k * np.eye(729), b + k * prior)


def pack(nodes):
    """1,000 hardware words from 729 nodes (flat index r + 9g + 81b)."""
    grid = np.zeros((10, 10, 10, 3))                      # [b][g][r]
    grid[:9, :9, :9] = nodes.reshape(9, 9, 9, 3)
    grid[9], grid[:, 9], grid[:, :, 9] = grid[8], grid[:, 8], grid[:, :, 8]
    v = np.rint(grid.reshape(-1, 3) * 2).astype(np.int64)
    y, u, w = np.clip(v[:, 0], 0, 1023), np.clip(v[:, 1], -512, 511), np.clip(v[:, 2], -512, 511)
    return ((y << 20) | ((u & 0x3ff) << 10) | (w & 0x3ff)).astype('<u4')


def unpack(words):
    """The 729 nodes (8-bit scale YCbCr) a packed table holds."""
    d = np.asarray(words, np.uint32)
    y = (d >> 20) & 0x3ff
    u = ((d >> 10) & 0x3ff).astype(np.int64); u[u >= 512] -= 1024
    v = (d & 0x3ff).astype(np.int64); v[v >= 512] -= 1024
    return np.stack([y, u, v], 1).reshape(10, 10, 10, 3)[:9, :9, :9].reshape(-1, 3) / 2.0


def apply_yuv(words, rgb):
    """The hardware's YCbCr output (8-bit scale, unrounded) for N x 3 RGB."""
    nodes = unpack(words)
    idx, w = tetra(rgb)
    return (nodes[idx] * w[..., None]).sum(1)


def apply(words, rgb):
    """What the hardware makes of N x 3 RGB, decoded back to 8-bit RGB."""
    nodes = unpack(words)
    idx, w = tetra(rgb)
    yuv = (nodes[idx] * w[..., None]).sum(1)
    return np.clip(np.rint(yuv @ np.linalg.inv(BT601).T), 0, 255)


def error_report(lib, lut, words):
    """Worst RGB channel against the look: on uniform RGB (every colour, the
    saturated extremes included) and near grey (+-28 levels, closer to photos)."""
    rng = np.random.default_rng(SEED + 1)
    uniform = rng.integers(0, 256, (200000, 3))
    grey = np.clip(rng.integers(0, 256, (200000, 1)) + rng.normal(0, 28, (200000, 3)), 0, 255).astype(np.int64)
    parts = []
    for name, rgb in (('uniform', uniform), ('near grey', grey)):
        e = np.abs(apply(words, rgb) - look_rgb(lib, lut, rgb)).max(1)
        parts.append(f'{name} p50 {np.percentile(e, 50):.0f} p99 {np.percentile(e, 99):.0f} max {e.max():.0f}')
    return '; '.join(parts)


TRAILER = b'HWTINT1\0'
AMBER = b'HWAMBR1\0'
# The glow's green before the look, as a divisor of its red energy: Red
# (today's, red-orange) and Amber.
GREEN = {'red': 5, 'amber': 2}


def halation_tint(lib, lut, green=GREEN['red']):
    """Q8 gains (red, green, blue) for glow added after the grade, fitted so
    that glow added to the look's output in linear light lands where the
    pre-grade glow (red E and green E/green added before the LUT; Red is
    E/5, Amber E/2) lands, in display levels. Each channel is a 1D search
    over 0..1024."""
    lin = camera_table('halo_linear', 256)
    code = np.arange(65536)
    encode = np.searchsorted((lin[1:] + lin[:-1]) / 2, code)          # nearest code per linear value
    enc = lambda x: encode[np.minimum(np.asarray(x, np.int64), 65535)]
    # Where glow lands and how strong it is decides the fit. On two test
    # shots at High, glow pixels before the look have luma median 181
    # (10th/90th percentile 36/224: mostly just below the highlights, some in
    # the shadows beside them) and mostly faint energy (median 2,600, 90% under
    # 7,500 of 65,535). The sample follows that: 70% of near-grey colours at
    # luma 150-230 and 30% at 16-150, energies log-uniform 300-12,000. Fitted
    # this way, gains on those photos' real glow pixels come out within ~10%.
    rng = np.random.default_rng(SEED + 2)
    n = 20000
    bright = rng.random(n) < 0.7
    grey = np.where(bright, rng.integers(150, 231, n), rng.integers(16, 151, n))[:, None]
    base = np.clip(grey + rng.normal(0, 12, (n, 3)), 0, 255).astype(np.int64)
    e = np.rint(np.exp(rng.uniform(np.log(300), np.log(12000), n))).astype(np.int64)
    pushed = np.stack([enc(lin[base[:, 0]] + e), enc(lin[base[:, 1]] + e // green), base[:, 2]], 1)
    out0 = look_rgb(lib, lut, base).astype(np.int64)
    target = look_rgb(lib, lut, pushed).astype(np.float64)
    gains = np.arange(0, 1025)
    tint = []
    for c in range(3):
        post = enc(lin[out0[:, c]][None, :] + (e[None, :] * gains[:, None] + 128) // 256)
        tint.append(int(gains[np.argmin(((post - target[:, c][None, :]) ** 2).sum(1))]))
    return np.array(tint)


def camera_table(name, count):
    """A const unsigned char/short table from camera/film_colour.c."""
    text = (lab / 'camera/film_colour.c').read_text()
    body = re.search(name + r'\[' + str(count) + r'\]=\{([^}]*)\}', text).group(1)
    return np.array([int(v) for v in body.replace('\n', '').split(',') if v.strip()], np.int64)


def graded_highlight(lib, lut):
    """For each graded luma, the highlight weight (film_colour.c halo_highlight)
    of the luma the look turned into it, along the grey axis."""
    g = np.arange(256)
    out = look_rgb(lib, lut, np.stack([g] * 3, 1)).astype(np.float64) @ BT601[0]
    out = np.maximum.accumulate(out)                       # monotone for the inversion
    # Each graded luma takes the average highlight weight of the lumas before
    # the look that come out at it: where a look clips a range of greys to
    # white, white weighs as that range does on average. Lumas no grey reaches
    # take the weight of the nearest luma before the look.
    hl = camera_table('halo_highlight', 256).astype(np.float64)
    first = np.searchsorted(out, g - 0.5, side='left')
    last = np.searchsorted(out, g + 0.5, side='right') - 1
    near = np.clip(np.rint(np.interp(g, out + np.arange(256) * 1e-6, g)).astype(int), 0, 255)
    cum = np.concatenate([[0], np.cumsum(hl)])
    mean = np.where(last >= first, (cum[np.minimum(last, 255) + 1] - cum[first]) / np.maximum(last - first + 1, 1), hl[near])
    return np.clip(np.rint(mean), 0, 255).astype(np.uint8)


def hwt_bytes(words, tint, highlight):
    return words.tobytes() + TRAILER + np.array(list(tint) + [0], '<u2').tobytes() + np.asarray(highlight, np.uint8).tobytes()


def hwa_bytes(tint):
    return AMBER + np.array(list(tint) + [0], '<u2').tobytes()


def read_hwa(path):
    """The Amber tint from a .HWA file."""
    data = Path(path).read_bytes()
    assert len(data) == 16 and data[:8] == AMBER and data[14:16] == b'\0\0', f'{path}: not a 16-byte .HWA'
    return tuple(int(v) for v in np.frombuffer(data[8:14], '<u2'))


def read_hwt(path):
    """(words, tint, highlight) from a .HWT file."""
    data = Path(path).read_bytes()
    assert len(data) == 4272 and data[4000:4008] == TRAILER, f'{path}: not a 4272-byte .HWT'
    return (np.frombuffer(data[:4000], '<u4'), tuple(int(v) for v in np.frombuffer(data[4008:4014], '<u2')),
            np.frombuffer(data[4016:4272], np.uint8))


def write_tables(names=None, quiet=False):
    names = names or sorted(p.name for p in folder.glob('*.LUT'))
    with tempfile.TemporaryDirectory() as tmp:
        lib = colour_library(tmp)
        for name in names:
            lut = (folder / name).read_bytes()
            words = pack(fit_nodes(lib, lut))
            tint = halation_tint(lib, lut)
            target = folder / (Path(name).stem + '.HWT')
            target.write_bytes(hwt_bytes(words, tint, graded_highlight(lib, lut)))
            amber = halation_tint(lib, lut, GREEN['amber'])
            target.with_suffix('.HWA').write_bytes(hwa_bytes(amber))
            if not quiet:
                print(f'{target.name} vs {name}: {error_report(lib, lut, words)}; halation tint red {tuple(int(v) for v in tint)}, amber {tuple(int(v) for v in amber)}')


if __name__ == '__main__':
    write_tables(sys.argv[1:] or None)
