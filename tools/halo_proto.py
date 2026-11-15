#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Halation prototype: the camera's halation in numpy, with its knobs exposed.

The camera adds halation to the frame Canon has already graded with the
look's colour table (camera/film_colour.c, film_uyvy_graded_retro):

  1. a highlight mask per pixel pair: the look's highlight weight (the .HWT
     trailer: a ramp from luma 226 to 255 before the look, 210 since the
     CineStill change) of the graded luma, on a half-resolution grid;
  2. the mask blurred: three box blurs per axis, each a sixth of the radius
     (72 px at High), close to a Gaussian;
  3. glow where the blurred mask exceeds the pixel's own highlight weight:
     energy = (blur - own) * strength * 2.57 in linear light (strength 48 at
     High), coloured by the look's tint (.HWT Red or .HWA Amber) and added
     to each channel in linear light.

This file reproduces the kernel as it was before the CineStill change
(--check compared it with that C kernel: added red 7.92 vs 7.93, p99
difference 3; against today's kernel it no longer matches), then
renders variants with other knobs: radius, strength, the threshold where
the mask starts, the blur's shape (one box: the square glow before commit
790c24e; three boxes: today's; or today's plus a wide faint tail), and the
colour. Nothing here runs on the camera.

  halo_proto.py CR2 [--look SUPERIA.LUT] [--check] [--variant NAME=key:v,...] [--out DIR]

Variant keys: radius (px), strength, threshold (graded luma where the mask
starts; omit for the look's own table), shape (box, tri, tail), tail_radius
(px), tail_weight (0-1), colour (red, amber).
"""
import argparse, ctypes as C, sys, tempfile
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import acceptance as A
import hw_table

LIN = hw_table.camera_table('halo_linear', 256).astype(np.float64)
ENC_EDGES = (LIN[1:] + LIN[:-1]) / 2

TODAY = dict(radius=72, strength=48, threshold=None, shape='tri', tail_radius=0, tail_weight=0.0, colour='red')
VARIANTS = {
    'Today (High)': {},
    'Before 790c24e (square)': dict(shape='box'),
    'Strength x2': dict(strength=96),
    'Radius x2': dict(radius=144),
    'Threshold 200': dict(threshold=200),
    'Wide tail': dict(shape='tail', tail_radius=320, tail_weight=0.5),
    'Strong: x2, r144, thr 210': dict(strength=96, radius=144, threshold=210),
    'CineStill: tail + x2 + thr 210': dict(strength=96, threshold=210, shape='tail', tail_radius=360, tail_weight=0.6),
}


def box1d(a, r, axis):
    """Box blur of radius r (window 2r+1) along axis, edges repeated."""
    if r <= 0:
        return a
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r + 1, r)
    c = np.cumsum(np.pad(a, pad, mode='edge'), axis=axis, dtype=np.float64)
    n = a.shape[axis]
    hi = np.take(c, np.arange(2 * r + 1, 2 * r + 1 + n), axis=axis)
    lo = np.take(c, np.arange(0, n), axis=axis)
    return (hi - lo) / (2 * r + 1)


def blur(mask, radius_px, shape):
    """The mask (half-resolution cells) blurred as the camera does for a radius
    in full-resolution pixels."""
    cells = radius_px / 2
    if shape == 'box':                                   # one box per axis, radius = the reach
        r = int(round(cells))
        return box1d(box1d(mask, r, 0), r, 1)
    b = max(1, int(radius_px // 6))                      # three boxes, each a sixth of the radius
    out = mask
    for _ in range(3):
        out = box1d(box1d(out, b, 0), b, 1)
    return out


def halo(rgb, words_tint, highlight, p):
    """Halation on a graded uint8 RGB frame with settings p."""
    c64 = rgb.astype(np.int64)
    y = (19595 * c64[..., 0] + 38470 * c64[..., 1] + 7471 * c64[..., 2] + 32768) >> 16
    if p['threshold'] is None:
        weight = highlight[y].astype(np.float64)
    else:
        t = p['threshold']
        weight = np.clip((y - t) / (255.0 - t), 0, 1) * 255
    h, w = weight.shape
    cell = weight[:h // 2 * 2, :w // 2 * 2].reshape(h // 2, 2, w // 2, 2).mean((1, 3))
    b = blur(cell, p['radius'], p['shape'])
    if p['shape'] == 'tail' and p['tail_weight'] > 0:
        b = (1 - p['tail_weight']) * b + p['tail_weight'] * blur(cell, p['tail_radius'], 'tri') * 1.0
        b = np.maximum(b, blur(cell, p['radius'], 'tri'))  # the core never weaker than today's
    full = np.repeat(np.repeat(b, 2, 0), 2, 1)
    full = np.pad(full, ((0, h - full.shape[0]), (0, w - full.shape[1])), mode='edge')
    energy = np.clip(full - weight, 0, None) * p['strength'] * 257 / 100
    out = np.empty_like(rgb)
    for c in range(3):
        lin = LIN[rgb[..., c]] + energy * words_tint[c] / 256
        out[..., c] = np.searchsorted(ENC_EDGES, np.minimum(lin, 65535)).clip(0, 255)
    return out


def graded_frame(cr2, look, cache):
    """Canon's develop with the look's table, as the camera's halation sees it."""
    base = A.to_uyvy(A.develop(cr2, cache))
    words, tint, highlight = hw_table.read_hwt(look.with_suffix('.HWT'))
    graded = A.digic_grade(base, words)
    return base, graded, tint, np.asarray(highlight)


def c_render(base, look, colour):
    """The camera's own kernel on the same frame (exact), for --check."""
    fx = dict(toy=0, vignette=0, leaks=0, halation=3, bloom=0, grain=0, soft=0, seed=0x12345678, dust=0, text='',
              hw_mask=False, engine='digic', fast_bloom=False, fast_halation=False, halation_colour=colour,
              stamp=0, dust_dark=0, hwt=look.with_suffix('.HWT'))
    with tempfile.TemporaryDirectory() as tmp:
        lib = A.build_library(tmp, ROOT / 'camera', False)
        raw = look.read_bytes()
        lut = C.create_string_buffer(raw)
        assert lib.film_lut_prepare(lut, len(raw)) == 0
        return A.to_rgb(A.render(lib, base, lut, fx))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cr2', type=Path)
    ap.add_argument('--look', default='SUPERIA.LUT')
    ap.add_argument('--check', action='store_true', help='compare the prototype at today\'s settings with the C kernel')
    ap.add_argument('--variant', action='append', default=[], help='NAME=key:v,...')
    ap.add_argument('--crop', default=None, help='x,y,w,h of a detail crop (full-resolution pixels)')
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'halo-proto')
    a = ap.parse_args()
    look = Path(a.look) if Path(a.look).exists() else ROOT / 'card/CHDK/FILM/LUTS' / a.look
    a.out.mkdir(parents=True, exist_ok=True)
    stem = a.cr2.stem
    base, graded, red, highlight = graded_frame(a.cr2, look, a.out / f'{stem}-developed.png')
    rgb = np.asarray(A.to_rgb(graded).convert('RGB'))
    tints = {'red': red, 'amber': hw_table.read_hwa(look.with_suffix('.HWA'))}

    if a.check:
        cam = np.asarray(c_render(base, look, 'red').convert('RGB')).astype(int)
        mine = halo(rgb, tints['red'], highlight, TODAY).astype(int)
        glow_c = cam - rgb; glow_p = mine - rgb
        m = (glow_c.sum(-1) > 3) | (glow_p.sum(-1) > 3)
        print(f'check: glow pixels {m.mean() * 100:.1f}%, added red C {glow_c[..., 0][m].mean():.2f} vs prototype {glow_p[..., 0][m].mean():.2f}, '
              f'per-pixel |diff| p50 {np.percentile(np.abs(cam - mine)[m], 50):.0f} p99 {np.percentile(np.abs(cam - mine)[m], 99):.0f}')

    variants = dict(VARIANTS)
    if a.variant:
        variants = {'Today (High)': {}}
        for v in a.variant:
            name, spec = v.split('=', 1)
            d = {}
            for part in filter(None, spec.split(',')):
                k, val = part.split(':')
                d[k] = val if k in ('shape', 'colour') else (None if val == 'none' else float(val) if '.' in val else int(val))
            variants[name] = d
    tiles = []
    for name, d in variants.items():
        p = dict(TODAY, **d)
        out = halo(rgb, tints[p['colour']], highlight, p)
        added = (out.astype(int) - rgb)[..., 0]
        tiles.append((name, Image.fromarray(out), added.clip(0).mean()))
        print(f'{name:34s} mean added red {added.clip(0).mean():6.2f}  pixels > 8 levels {(added > 8).mean() * 100:5.1f}%')
    x, y, w, h = map(int, a.crop.split(',')) if a.crop else (0, 0, A.W, A.H)
    tw = 640
    th = round(h * tw / w)
    cols = 2
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * (tw + 8) - 8, rows * (th + 30)), 'white')
    d = ImageDraw.Draw(sheet)
    for i, (name, im, mean) in enumerate(tiles):
        cx, cy = (i % cols) * (tw + 8), (i // cols) * (th + 30)
        sheet.paste(im.crop((x, y, x + w, y + h)).resize((tw, th), Image.LANCZOS), (cx, cy + 30))
        d.text((cx + 4, cy + 8), f'{name}  (mean added red {mean:.2f})', fill='black')
    target = a.out / f'{stem}-{look.stem}-{"crop" if a.crop else "full"}.jpg'
    sheet.save(target, quality=90)
    print(target)


if __name__ == '__main__':
    main()
