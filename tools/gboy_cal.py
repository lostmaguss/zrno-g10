#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""ZrnoBoy colour calibration: what Canon's JPEG step does to the colours
ZrnoBoy writes, and the values to write so each palette comes out right.

A ZrnoBoy shot with CHDK/FILM/GBCAL.TXT on the card records the chart of
camera/film_gboy.c (film_gboy_paint_chart: 18 x 12 flat patches, Y in 6
levels and signed U, V in 6 each). This reads the patches from that JPEG,
fits a quadratic model from written to recorded Y, U, V (JFIF full range,
signed chroma), and inverts it for every palette colour.

  gboy_cal.py CHART.JPG             fit, report, print the C table
  gboy_cal.py CHART.JPG --check JPG...   compare shots with the model's
                                    prediction for their dominant colours
"""
import argparse, sys
from pathlib import Path
import numpy as np
from PIL import Image

COLS, ROWS = 18, 12
YL = [16, 58, 100, 142, 184, 226]
CL = [-100, -60, -20, 20, 60, 100]
ROOT = Path(__file__).resolve().parent.parent


def chart_levels():
    return np.array([(YL[i % 6], CL[(i // 6) % 6], CL[i // 36]) for i in range(COLS * ROWS)], float)


def ycbcr(path):
    """The JPEG's own Y, Cb, Cr, decoded without a trip through RGB (which
    would clip the chart's many colours outside RGB)."""
    im = Image.open(path)
    im.draft('YCbCr', im.size)
    assert im.mode == 'YCbCr', im.mode
    return np.asarray(im).astype(float)


def read_chart(path):
    a = ycbcr(path)
    h, w = a.shape[:2]
    out = []
    for i in range(COLS * ROWS):
        c, r = i % COLS, i // COLS
        x0, x1 = c * w // COLS, (c + 1) * w // COLS
        y0, y1 = r * h // ROWS, (r + 1) * h // ROWS
        mx, my = (x1 - x0) // 4, (y1 - y0) // 4
        p = a[y0 + my:y1 - my, x0 + mx:x1 - mx].reshape(-1, 3)
        out.append(np.median(p, axis=0))
    o = np.array(out)
    o[:, 1:] -= 128
    return o


def terms(x):
    x = np.atleast_2d(x) / 128.0
    y, u, v = x[:, 0], x[:, 1], x[:, 2]
    return np.stack([np.ones_like(y), y, u, v, y * y, u * u, v * v, y * u, y * v, u * v], 1)


def fit(written, recorded):
    coef, *_ = np.linalg.lstsq(terms(written), recorded, rcond=None)
    return coef


def predict(coef, x):
    return terms(x) @ coef


def invert(coef, target):
    """Written Y, U, V (in range) whose recorded colour is nearest target."""
    x = np.array(target, float)
    lo, hi = np.array([0, -128, -128]), np.array([255, 127, 127])
    for _ in range(60):
        f = predict(coef, x)[0] - target
        J = np.zeros((3, 3))
        for k in range(3):
            d = np.zeros(3); d[k] = 0.5
            J[:, k] = (predict(coef, x + d)[0] - predict(coef, x - d)[0]) / 1.0
        step = np.linalg.lstsq(J, -f, rcond=None)[0]
        x = np.clip(x + step, lo, hi)
        if np.abs(step).max() < 1e-3:
            break
    return x


def yuv(c):
    r, g, b = c >> 16, (c >> 8) & 255, c & 255
    return np.array([.299 * r + .587 * g + .114 * b, -.169 * r - .331 * g + .5 * b, .5 * r - .419 * g - .081 * b])


def palettes():
    src = (ROOT / 'camera/film_gboy.c').read_text()
    start = src.index('static const unsigned palettes[')
    body = src[start:src.index('};', start)]
    import re
    vals = [int(h, 16) for h in re.findall(r'0x([0-9A-Fa-f]{6})', body)]
    return [vals[i:i + 4] for i in range(0, len(vals), 4)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('chart')
    ap.add_argument('--check', nargs='*', default=[])
    a = ap.parse_args()
    written, recorded = chart_levels(), read_chart(a.chart)
    # Patches that clipped in the file say nothing about the conversion.
    ok = (recorded[:, 0] > 2) & (recorded[:, 0] < 253) & (np.abs(recorded[:, 1:]) < 126).all(1)
    print(f'{ok.sum()} of {len(ok)} patches unclipped')
    written, recorded = written[ok], recorded[ok]
    coef = fit(written, recorded)
    res = predict(coef, written) - recorded
    print(f'fit: rms {np.sqrt((res ** 2).mean(0)).round(2)} max {np.abs(res).max(0).round(1)} (Y, U, V)')
    lin = np.linalg.lstsq(np.hstack([written, np.ones((len(written), 1))]), recorded, rcond=None)[0]
    print('linear part (rows: from Y, U, V, 1):\n', lin.round(3))
    rows = []
    worst = 0
    for p in palettes():
        line = []
        for c in p:
            t = yuv(c)
            w = invert(coef, t)
            err = np.abs(predict(coef, w)[0] - t).max()
            worst = max(worst, err)
            wi = np.rint(w).astype(int)
            line.append('{%d, %d, %d}' % (wi[0], wi[1], wi[2]))
        rows.append('    {' + ', '.join(line) + '},')
    print(f'worst predicted error after compensation: {worst:.1f}')
    print('static const signed short written[FILM_GBOY_PALETTES][4][3] = {')
    print('\n'.join(rows))
    print('};')
    for path in a.check:
        y = ycbcr(path)[::3, ::3].reshape(-1, 3).astype(int)
        vals, counts = np.unique(y, axis=0, return_counts=True)
        top = vals[np.argsort(-counts)[:4]].astype(float)
        top[:, 1:] -= 128
        print(path, 'dominant (Y, U, V):', top.round().astype(int).tolist())


if __name__ == '__main__':
    main()
