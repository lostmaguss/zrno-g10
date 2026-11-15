#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build card/CHDK/FILM/PRINT.BIN: the tables of the Print engine (camera/film_riso.c).

Everything the camera cannot cheaply work out itself is computed here, once:
for each ink set the colour separation (which coverage of each ink prints
nearest to every photo colour), the duotone curves and the colours of every
overprint on each paper; the grain screens (blue noise) and the dot screens.
The camera adds the shot's own levels, noise, misregistration and print flaws.

Layout (little-endian, offsets in camera/film_riso.c):
  0   "G12RS02\\0", u16 sets, u16 tile (128), u32 set bytes
  16  gamma[256]: the midtone lift applied after the levels (0.7)
  272 grain tiles: 3 x 128 x 128 thresholds 0..254
      dots 45 deg: 16 x 16, dots 0 deg: 8 x 8 (the photo's screens)
      live dots 45 deg: 8 x 8, live dots 0 deg: 4 x 4 (the live view's)
      flat (blue-noise jitter round the middle), engraving lines and woodcut
      gouges: 3 x 128 x 128; comic dots 45 deg: 32 x 32, 0 deg: 16 x 16
      text glyphs: 16 x 16 rows of 8 bits, lightest to darkest (CHDK's 8x16 font)
  then per set (the Riso ink sets first, then the styles' palettes, film_riso.c
  STYLE_BASE): inks (0 for text), style, register mask (bit k: ink k may be
  out of register), flags (1 keylines, 2 dark key, 4 colour separation, 8
  coloured text), screen per ink x 3 (255: the Screen row's), 9 reserved,
      3 papers x 8 x (y, u, v, 0) colours (index: bit k = ink k printed;
      text: 0 background, 1.. the characters' colours),
      3 x 32 densities by luma (the duotone curves; styles' own curves),
      3 x 8192 colour separation: [(rawU >> 4) << 4 | (rawV >> 4)][Y >> 3]
  Ink 0 of a Riso set is its darkest (the 45 degree screen, never shifted).

  riso_pack.py [--out card/CHDK/FILM/PRINT.BIN]
"""
import argparse, math, re, struct
from pathlib import Path
import os
import numpy as np

LAB = Path(__file__).resolve().parents[1]
TILE = 128
MAX_INKS = 3

# Approximate sRGB of the printed inks (mattdesl/riso-colors, from Stencil's list).
INK = {'black': '#000000', 'fpink': '#FF48B0', 'blue': '#0078BF', 'mblue': '#3255A4', 'yellow': '#FFE800',
       'teal': '#00838A', 'forange': '#FF7477', 'green': '#00A95C'}
PAPERS = ['#F2EDE4', '#FAF9F6', '#F1E6CC']                     # Off-white, White, Cream
# The sets, in the order of the Inks row (film_riso.c names them). Ink 0 is
# the darkest. duo: each ink's ramp over darkness (0 = paper .. 1 = black).
SETS = [
    ('Pink + Blue', ['mblue', 'fpink'], [(0.40, 1.0), (0.22, 0.85)]),
    ('Teal + Orange', ['teal', 'forange'], [(0.40, 1.0), (0.20, 0.85)]),
    ('Blue + Orange', ['blue', 'forange'], [(0.40, 1.0), (0.20, 0.85)]),
    ('Blue + Yellow', ['mblue', 'yellow'], [(0.40, 1.0), (0.10, 0.75)]),
    ('Green + Pink', ['green', 'fpink'], [(0.40, 1.0), (0.22, 0.85)]),
    ('Black + Pink', ['black', 'fpink'], [(0.45, 1.0), (0.12, 0.75)]),
    ('Yellow + Pink + Blue', ['mblue', 'fpink', 'yellow'], [(0.45, 1.0), (0.25, 0.85), (0.05, 0.6)]),
]
MAX_D = 0.85                     # solids stop at 85% coverage, as riso shops ask


def hexrgb(h):
    h = h.lstrip('#'); return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], np.float64)


def srgb_to_lin(c):
    c = np.asarray(c, np.float64) / 255
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(c):
    c = np.clip(c, 0, 1)
    return 255 * np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def lab(rgb):
    l = srgb_to_lin(rgb)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = l @ M.T / np.array([0.9505, 1.0, 1.089])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def mix(paper, inks, d):
    """Mean colour at coverages d[..., k]: the inks overprint like multiply
    (semi-transparent), screens independent: paper * prod(1 - d + d * ink/paper)."""
    P = srgb_to_lin(paper); out = np.broadcast_to(P, d.shape[:-1] + (3,)).copy()
    for k, ink in enumerate(inks):
        r = srgb_to_lin(ink) / P
        out *= 1 - d[..., k:k + 1] + d[..., k:k + 1] * r
    return lin_to_srgb(out)


def rgb_to_yuv(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    y = 0.299 * r + 0.587 * g + 0.114 * b
    return y, (b - y) / 1.772, (r - y) / 1.402


def yuv_to_rgb(y, u, v):
    return np.stack([y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u], -1)


def separation(paper, inks, luma_w, steps, sat=1.3):
    """[256 raw uv][32 luma][3] densities 0..255, nearest printed colour in Lab
    (lightness weighted luma_w). The uv index is the frame's raw signed bytes >> 4."""
    n = len(inks)
    yy = np.arange(32) * 8 + 4.0
    signed = (np.r_[np.arange(0, 128, 16), np.arange(-128, 0, 16)] + 8.0)   # raw byte >> 4 -> signed centre
    U, V, Y = np.meshgrid(signed, signed, yy, indexing='ij')
    rgb = np.clip(yuv_to_rgb(Y, U * sat, V * sat), 0, 255)
    g = np.linspace(0, MAX_D, steps)
    grid = np.stack(np.meshgrid(*([g] * n), indexing='ij'), -1).reshape(-1, n)
    cand = lab(mix(paper, inks, grid))
    target = lab(rgb).reshape(-1, 3)
    w = np.array([luma_w, 1, 1])
    best = np.empty((target.shape[0], n))
    for i in range(0, target.shape[0], 512):
        dist = (((target[i:i + 512, None, :] - cand[None]) ** 2) * w).sum(-1)
        best[i:i + 512] = grid[dist.argmin(1)]
    out = np.zeros((256, 32, MAX_INKS), np.uint8)
    out[..., :n] = np.clip(best.reshape(256, 32, n) * 255 + 0.5, 0, 255)
    return out


def duotone(curves):
    t = 1 - (np.arange(32) * 8 + 4.0) / 255
    out = np.zeros((MAX_INKS, 32), np.uint8)
    for k, (a, b) in enumerate(curves):
        out[k] = np.clip(np.clip((t - a) / (b - a), 0, 1) * MAX_D * 255 + 0.5, 0, 255)
    return out


def overprints(paper, inks):
    """Colour of each ink mask (bit k = ink k) as BT.601 full-range Y and signed U, V."""
    n = len(inks); out = np.zeros((8, 4), np.int64)
    for m in range(1 << n):
        d = np.array([[(m >> k) & 1 for k in range(n)]], np.float64)
        y, u, v = rgb_to_yuv(mix(paper, inks, d)[0])
        out[m] = [np.clip(round(float(y)), 0, 255), np.clip(round(float(u)), -128, 127), np.clip(round(float(v)), -128, 127), 0]
    return out


def blue_noise(n=64, seed=1, sigma=1.5):
    """Void-and-cluster threshold ranks, n x n."""
    rng = np.random.default_rng(seed)
    k = np.fft.fftfreq(n)
    G = np.exp(-2 * math.pi ** 2 * sigma ** 2 * (k[:, None] ** 2 + k[None, :] ** 2))
    blur = lambda m: np.real(np.fft.ifft2(np.fft.fft2(m) * G))
    pat = rng.random((n, n)) < 0.1
    while True:
        e = blur(pat.astype(float)); c = np.argmax(np.where(pat, e, -1e9)); pat.flat[c] = False
        e = blur(pat.astype(float)); v = np.argmin(np.where(~pat, e, 1e9)); pat.flat[v] = True
        if c == v: break
    rank = np.zeros(n * n, int); ones = int(pat.sum()); p = pat.copy()
    for r in range(ones - 1, -1, -1):
        e = blur(p.astype(float)); c = np.argmax(np.where(p, e, -1e9)); p.flat[c] = False; rank[c] = r
    p = pat.copy()
    for r in range(ones, n * n):
        e = blur(p.astype(float)); v = np.argmin(np.where(~p, e, 1e9)); p.flat[v] = True; rank[v] = r
    return rank.reshape(n, n)


def grain_tile(seed):
    """RISO's Grain Touch is a randomised error-diffusion dither: blue noise
    grown 2x with a wrapped bilinear step and a little white noise, re-ranked
    so coverage stays uniform. 128 x 128; blue noise does not show its repeat."""
    n, f = 64, 2
    b = blue_noise(n, seed).astype(np.float64)
    c = (np.arange(n * f) + 0.5) / f - 0.5
    i0 = np.floor(c).astype(int); w = c - i0; i0 %= n; i1 = (i0 + 1) % n
    rows = b[i0] * (1 - w)[:, None] + b[i1] * w[:, None]
    t = rows[:, i0] * (1 - w)[None] + rows[:, i1] * w[None]
    t += np.random.default_rng(seed + 99).normal(0, 18, t.shape)
    order = np.argsort(t.ravel(), kind='stable'); r = np.empty(t.size); r[order] = np.arange(t.size)
    return (r * 255 / t.size).astype(np.uint8).reshape(t.shape)


def am_screen(a, b):
    """Round-dot thresholds for the lattice (a, b), (-b, a): pitch sqrt(a^2 + b^2)
    px; one period of the tile (a, 0 deg or a = b, 45 deg)."""
    N = a if b == 0 else (a * a + b * b) // math.gcd(a, b)
    y, x = np.mgrid[0:N, 0:N] + 0.5
    s = a * a + b * b
    u = (x * a + y * b) / s; v = (-x * b + y * a) / s
    f = np.cos(2 * math.pi * u) + np.cos(2 * math.pi * v)
    order = np.argsort(-f.ravel(), kind='stable'); t = np.empty(N * N); t[order] = np.arange(N * N)
    return (t * 255 / (N * N)).astype(np.uint8).reshape(N, N)


# ---- Styles (the round 3 and 4 picks) ------------------------------------
# Screen ids: 1 flat, 2 engraving, 3 woodcut, 4 comic 45, 5 comic 0, 6 comic 0
# half a cell over; 255: the Screen row's (Riso sets only).
F_KEY, F_DARKKEY, F_COLSEP, F_TEXTCOL = 1, 2, 4, 8
STYLES = ['Riso', 'Screenprint', 'Engraving', 'Woodcut', 'Photocopy', 'Comic', 'Text mode']


def tone_index():
    """Darkness d (0 paper .. 1 black) for each of the 32 curve entries: the
    camera looks curves up by gamma(levels(Y)) >> 3, the 0.7 lift applied."""
    g = (np.arange(32) * 8 + 4.0) / 255
    return 1 - g ** (1 / 0.7)


def ramp(d, a, w=0.04):
    return np.clip((d - a) / w + 0.5, 0, 1)


def curves(*fns):
    d = tone_index(); out = np.zeros((MAX_INKS, 32), np.uint8)
    for k, f in enumerate(fns):
        out[k] = np.clip(np.round(f(d) * 255), 0, 255)
    return out


def yuv_entry(rgb):
    y, u, v = rgb_to_yuv(np.asarray(rgb, np.float64))
    return (int(np.clip(round(float(y)), 0, 255)), int(np.clip(round(float(u)), -128, 127)), int(np.clip(round(float(v)), -128, 127)))


def palette_from(fn, n):
    """8 entries, fn(mask) -> hex colour."""
    return [yuv_entry(hexrgb(fn(m))) if m < (1 << max(n, 3)) else (0, 0, 0) for m in range(8)]


def screenprint(c0, c1, c2, key):
    # ink 0: first band (d > 0.30), ink 1: the key (d > 0.80, out of register),
    # ink 2: second band (d > 0.58).
    pal = lambda m: key if m & 2 else c2 if m & 4 else c1 if m & 1 else c0
    return dict(n=3, reg=2, flags=0, screens=(1, 1, 1), pal=pal,
                duo=curves(lambda d: ramp(d, 0.30), lambda d: ramp(d, 0.80), lambda d: ramp(d, 0.58)))


def engraving(ink, paper):
    return dict(n=1, reg=0, flags=0, screens=(2, 255, 255), pal=lambda m: ink if m & 1 else paper,
                duo=curves(lambda d: np.clip(d * 1.05, 0, 1)))


def engraving_two():
    pal = lambda m: '#1A1714' if m & 1 else '#E8A87C' if m & 2 else '#F3EAD3'
    return dict(n=2, reg=2, flags=0, screens=(2, 1, 255), pal=pal,
                duo=curves(lambda d: np.clip(d * 1.05, 0, 1), lambda d: ramp(d, 0.25) * (1 - ramp(d, 0.75))))


def woodcut(red=False):
    if not red:
        return dict(n=1, reg=0, flags=0, screens=(3, 255, 255), pal=lambda m: '#16120F' if m & 1 else '#EDE2CB',
                    duo=curves(lambda d: d))
    pal = lambda m: '#16120F' if m & 1 else '#C23B22' if m & 2 else '#EDE2CB'
    return dict(n=2, reg=2, flags=0, screens=(3, 1, 255), pal=pal, duo=curves(lambda d: d, lambda d: ramp(d, 0.40)))


def photocopy(toner, paper):
    def dens(d):
        y = (1 - d) ** 1.3
        y = (y - 0.5) * 2.2 + 0.5
        return np.clip((0.42 - y) / 0.12 + 0.5, 0, 1)
    return dict(n=1, reg=0, flags=0, screens=(1, 255, 255), pal=lambda m: toner if m & 1 else paper, duo=curves(dens))


COMIC_INKS = ['#00A0E0', '#E5007E', '#FFE000']


def comic(dark_key, lift):
    paper = hexrgb('#F4EAD2')
    cols = [hexrgb(h) for h in COMIC_INKS]
    def pal(m):
        if m == 7: return '#141018'                      # all three: the key colour (keylines use it too)
        c = paper.copy()
        for k in range(3):
            if m >> k & 1: c = c * cols[k] / 255
        return '#%02X%02X%02X' % tuple(int(round(v)) for v in c)
    return dict(n=3, reg=6, flags=F_KEY | (F_DARKKEY if dark_key else 0) | F_COLSEP, screens=(4, 5, 6), pal=pal,
                duo=curves(), lift=lift)


def comic_sep(lift):
    """[256 uv][32 luma][3]: cyan from red, magenta from green, yellow from blue,
    each in four flat tints (Ben-Day: equal dots per tint)."""
    yy = np.arange(32) * 8 + 4.0
    signed = (np.r_[np.arange(0, 128, 16), np.arange(-128, 0, 16)] + 8.0)
    U, V, Y = np.meshgrid(signed, signed, yy, indexing='ij')
    g = (Y / 255) ** (1 / 0.7) * 255                      # undo the lift the camera applies to the luma axis
    rgb = np.clip(yuv_to_rgb(g, U * 1.3, V * 1.3), 0, 255)
    if lift: rgb = rgb * 0.75 + 70
    cov = np.clip(((1 - rgb / 255) - 0.08) / 0.8, 0, 1)
    tint = np.select([cov < 0.18, cov < 0.45, cov < 0.75], [0.0, 0.3, 0.6], 1.0)
    out = np.zeros((256, 32, MAX_INKS), np.uint8)
    out[..., :3] = np.clip(np.round(tint.reshape(256, 32, 3) * 255), 0, 255)
    return out


TEXT = {'green': ('#050A06', ['#4BFF7A']), 'amber': ('#0C0702', ['#FFB347']),
        'colour': ('#06060A', ['#FF5555', '#55FF55', '#FFFF55', '#5555FF', '#FF55FF', '#55FFFF', '#FFFFFF'])}


def text(kind):
    bg, fg = TEXT[kind]
    pal = lambda m: bg if m == 0 else fg[min(m, len(fg)) - 1]
    return dict(n=0, reg=0, flags=F_TEXTCOL if kind == 'colour' else 0, screens=(255, 255, 255), pal=pal, duo=curves())


STYLE_SETS = [   # (style, variant name, record); the order is film_riso.c's
    (1, 'Pop', screenprint('#FFE45E', '#FF5EA8', '#26C4B4', '#1B1B3A')),
    (1, 'Sunset', screenprint('#F7F3E3', '#F2A541', '#C8553D', '#28262C')),
    (1, 'Mint + Crimson', screenprint('#C6F0E7', '#7FB7BE', '#D3365F', '#1A1C3A')),
    (2, 'Aubergine', engraving('#2E2340', '#F2EAD6')),
    (2, 'Steel blue', engraving('#1D3557', '#EEF1E6')),
    (2, 'Two plates', engraving_two()),
    (3, 'Black', woodcut()),
    (3, 'Two blocks', woodcut(True)),
    (4, 'White paper', photocopy('#2B2B2B', '#F5F5F5')),
    (4, 'Pink paper', photocopy('#3B2A24', '#F7C6D0')),
    (5, 'Full key', comic(True, False)),
    (5, 'Outlines only', comic(False, True)),
    (6, 'Green', text('green')),
    (6, 'Amber', text('amber')),
    (6, 'Colour', text('colour')),
]


def flat_tile():
    """Thresholds round the middle with a little blue-noise jitter, so hard
    edges come out slightly ragged, as a squeegee leaves them."""
    b = np.tile(blue_noise(64, 21), (2, 2)).astype(np.float64) / 4095
    return np.clip(np.round(128 + (b - 0.5) * 40), 0, 254).astype(np.uint8)


def engraving_tile():
    """Lines at about 61 degrees, 10.2 px apart, swelling with the tone; in the
    shadows a second set at about -34 degrees crosses them. As thresholds:
    inked where the tone exceeds min(line, 0.58 + 0.42 * cross line)."""
    N = TILE
    y, x = np.mgrid[0:N, 0:N] + 0.5
    v = (6 * x + 11 * y) / N + 0.18 * np.sin(2 * math.pi * (11 * x - 6 * y) / (N * 3))
    t1 = np.abs(v - np.floor(v) - 0.5) * 2
    w = (9 * x - 6 * y) / N
    t2 = np.abs(w - np.floor(w) - 0.5) * 2
    t = np.minimum(t1 / 1.0, 0.58 + 0.42 * t2)
    return np.clip(np.round(t * 254), 0, 254).astype(np.uint8)


def woodcut_tile():
    """Wavy gouges 16 px apart carry the midtones; above 0.82 the block is
    solid but for pale wood grain. All periods divide 128."""
    rng = np.random.default_rng(5)
    N = TILE
    y, x = np.mgrid[0:N, 0:N] + 0.5
    wav = y / 16 + 0.35 * np.sin(2 * math.pi * x / N + 2 * math.pi * y / N) + 0.15 * np.sin(2 * math.pi * 5 * x / N)
    line = np.abs(wav - np.floor(wav) - 0.5) * 2
    t = np.minimum(line * 0.9 + 0.08, 0.82)
    k = np.fft.fftfreq(N)
    G = np.exp(-2 * math.pi ** 2 * (1.0 ** 2 * k[:, None] ** 2 + 6.0 ** 2 * k[None, :] ** 2))
    grain = np.real(np.fft.ifft2(np.fft.fft2(rng.standard_normal((N, N))) * G))
    grain = (grain - grain.min()) / (grain.max() - grain.min())
    t = np.where(grain > 0.72, 0.985, t)                  # grain: only the very darkest tone prints there
    return np.clip(np.round(t * 254), 0, 254).astype(np.uint8)


def glyphs():
    """16 characters of CHDK's 8x16 font, by how much ink they carry."""
    src = (Path(os.environ.get('CHDK_BASE', LAB / 'build/chdk-r6355')) / 'lib/font/font_8x16_uni.h').read_text(encoding='latin-1')
    chars = {}
    for m in re.finditer(r'\{ 0x([0-9A-Fa-f]{4}), \{(.*?)\}', src, re.S):
        code = int(m.group(1), 16)
        if 0x20 <= code < 0x7F:
            chars[code] = [int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(2))][:16]
    dens = {c: sum(bin(b).count('1') for b in rows) for c, rows in chars.items()}
    order = sorted(chars, key=lambda c: (dens[c], c))
    top = dens[order[-1]]
    pick = []
    for i in range(16):
        want = top * i / 15
        c = min((c for c in order if c not in pick), key=lambda c: abs(dens[c] - want))
        pick.append(c)
    pick.sort(key=lambda c: dens[c])
    print('text ramp:', repr(''.join(chr(c) for c in pick)))
    return bytes(b for c in pick for b in chars[c])


def build():
    gamma = np.round(255 * (np.arange(256) / 255) ** 0.7).astype(np.uint8)
    head = bytearray()
    body = bytearray(gamma.tobytes())
    for k in range(3):
        body += grain_tile(k + 1).tobytes()
    for t, size in ((am_screen(8, 8), 16), (am_screen(8, 0), 8), (am_screen(4, 4), 8), (am_screen(4, 0), 4)):
        assert t.shape == (size, size); body += t.tobytes()
    body += flat_tile().tobytes() + engraving_tile().tobytes() + woodcut_tile().tobytes()
    for t, size in ((am_screen(16, 16), 32), (am_screen(16, 0), 16)):
        assert t.shape == (size, size); body += t.tobytes()
    body += glyphs()
    sets = bytearray()
    set_bytes = None
    for name, inks, duo in SETS:
        cols = [hexrgb(INK[i]) for i in inks]
        n = len(inks)
        rec = bytearray([n, 0, 0, 0, 255, 255, 255]) + bytes(9)
        for p in PAPERS:
            rec += b''.join(struct.pack('<Bbbb', int(y), int(u), int(v), 0) for y, u, v, _ in overprints(hexrgb(p), cols))
        rec += duotone(duo).tobytes()
        sep = separation(hexrgb(PAPERS[0]), cols, luma_w=2.0 if n < 3 else 1.5, steps=13 if n < 3 else 11)
        rec += np.ascontiguousarray(sep.transpose(2, 0, 1)).tobytes()   # [ink][uv][luma]
        set_bytes = set_bytes or len(rec)
        assert len(rec) == set_bytes
        sets += rec
        print(f'{name}: {n} inks')
    for style, name, r in STYLE_SETS:
        n = r['n']
        rec = bytearray([n, style, r['reg'], r['flags']] + list(r['screens'])) + bytes(9)
        pal = palette_from(r['pal'], n)
        for p in PAPERS:                                  # the styles bring their own paper
            rec += b''.join(struct.pack('<Bbbb', y, u, v, 0) for y, u, v in pal)
        rec += r['duo'].tobytes()
        sep = comic_sep(r['lift']) if r['flags'] & F_COLSEP else np.zeros((256, 32, MAX_INKS), np.uint8)
        rec += np.ascontiguousarray(sep.transpose(2, 0, 1)).tobytes()
        assert len(rec) == set_bytes
        sets += rec
        print(f'{STYLES[style]} / {name}: {n} inks')
    head += b'G12RS02\0' + struct.pack('<HHI', len(SETS) + len(STYLE_SETS), TILE, set_bytes)
    return bytes(head + body + sets)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, default=LAB / 'card/CHDK/FILM/PRINT.BIN')
    a = ap.parse_args()
    data = build()
    a.out.write_bytes(data)
    print(a.out, len(data), 'bytes')


if __name__ == '__main__':
    main()
