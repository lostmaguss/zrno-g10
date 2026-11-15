#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Render photos through Grade grades, as the camera would make them.

The camera's own grade code (camera/film_grade.c, compiled fresh) builds the
DIGIC colour table for each grade, and the table is applied with the
hardware's tetrahedral interpolation (tools/hw_table.py) to the image's RGB,
which stands for Canon's develop before the table (a CR2's .developed.png from
the acceptance set, or any sRGB image: a JPEG taken with My Colors Off has
been through Canon's identity table already, so the result is close).

  grade_render.py IMAGE [--out DIR] [--width 1216] [--grade NAME=field:v,...]

Without --grade, a sheet of sample grades. Field names are film_grade.h's
without the prefix, lower case (contrast, fade, shadow_hue, hsl_sat+3, ...);
hues 1..8 are Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta;
mono 0..6 is Off, No filter, Yellow, Orange, Red, Green, Blue filter.
"""
import argparse, ctypes as C, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import hw_table as W

FIELDS = ['contrast', 'midtones', 'shadows', 'highlights', 'fade', 'temperature', 'tint', 'saturation', 'vibrance',
          'shadow_hue', 'shadow_amount', 'highlight_hue', 'highlight_amount']
LATER = ['blacks', 'whites', 'mono']                   # after the 24 HSL fields
NFIELDS = len(FIELDS) + 24 + len(LATER)
HUES = ['red', 'orange', 'yellow', 'green', 'aqua', 'blue', 'purple', 'magenta']
SAMPLES = {
    'Neutral': '',
    'Teal & orange': 'contrast:2,shadow_hue:5,shadow_amount:4,highlight_hue:2,highlight_amount:3,vibrance:2,hsl_hue+orange:-1,hsl_sat+aqua:2',
    'Matte film': 'fade:3,contrast:-1,saturation:-1,temperature:2,highlights:-2',
    'Crisp B&W': 'saturation:-5,contrast:4,shadows:-2',
    'Red only': 'saturation:0,hsl_sat+orange:-5,hsl_sat+yellow:-5,hsl_sat+green:-5,hsl_sat+aqua:-5,hsl_sat+blue:-5,hsl_sat+purple:-5,hsl_sat+magenta:-3,hsl_sat+red:3',
    'Cool & moody': 'temperature:-3,tint:-1,midtones:-2,highlights:-3,saturation:-2,hsl_light+blue:-4',
    'Slide blacks': 'blacks:-4,contrast:1,saturation:1,whites:1',
    'Matte print': 'blacks:3,whites:-3,contrast:-1',
    'Red filter B&W': 'mono:4,contrast:2,blacks:-2',
    'Cyanotype': 'mono:3,contrast:1,whites:-1,shadow_hue:6,shadow_amount:5,highlight_hue:5,highlight_amount:2',
}


def library(tmp):
    out = Path(tmp) / 'grade.so'
    subprocess.run(['cc', '-O2', '-shared', '-fPIC', '-I', str(ROOT / 'camera'), '-o', str(out), str(ROOT / 'camera/film_grade.c')], check=True)
    lib = C.CDLL(str(out))
    lib.film_grade_table.argtypes = [C.c_void_p, C.c_void_p]
    lib.film_grade_neutral.argtypes = [C.c_void_p]
    lib.film_grade_valid.argtypes = [C.c_void_p]
    return lib


def settings(lib, text):
    s = (C.c_byte * NFIELDS)()
    lib.film_grade_neutral(s)
    for part in filter(None, text.split(',')):
        key, value = part.split(':')
        if key.startswith('hsl_'):
            what, hue = key[4:].split('+')
            idx = 13 + 8 * ['hue', 'sat', 'light'].index(what) + (HUES.index(hue) if hue in HUES else int(hue))
        elif key in LATER:
            idx = len(FIELDS) + 24 + LATER.index(key)
        else:
            idx = FIELDS.index(key)
        s[idx] = int(value)
    assert lib.film_grade_valid(s), f'out of range: {text}'
    return s


def grade(lib, text, rgb):
    s = settings(lib, text)
    words = np.zeros(1000, np.uint32)
    lib.film_grade_table(s, words.ctypes.data)
    flat = rgb.reshape(-1, 3).astype(np.int64)
    out = np.empty_like(flat)
    for i in range(0, len(flat), 1 << 18):                 # the interpolation is memory-hungry: in slices
        out[i:i + (1 << 18)] = W.apply(words, flat[i:i + (1 << 18)])
    return out.reshape(rgb.shape).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('image')
    ap.add_argument('--out', default=str(ROOT / 'validation' / 'grade'))
    ap.add_argument('--width', type=int, default=1216)
    ap.add_argument('--grade', action='append', default=[], help='NAME=field:v,...')
    a = ap.parse_args()
    im = Image.open(a.image).convert('RGB')
    im = im.resize((a.width, round(im.height * a.width / im.width)), Image.LANCZOS)
    rgb = np.asarray(im)
    grades = dict(g.split('=', 1) for g in a.grade) if a.grade else SAMPLES
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    stem = Path(a.image).stem.split('.')[0]
    with tempfile.TemporaryDirectory() as tmp:
        lib = library(tmp)
        tiles = [(name, Image.fromarray(grade(lib, text, rgb))) for name, text in grades.items()]
    cols = 2 if len(tiles) > 1 else 1
    w, h = tiles[0][1].size
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * w + (cols - 1) * 8, rows * (h + 34)), 'white')
    d = ImageDraw.Draw(sheet)
    for i, (name, t) in enumerate(tiles):
        x, y = (i % cols) * (w + 8), (i // cols) * (h + 34)
        sheet.paste(t, (x, y + 34))
        d.text((x + 6, y + 8), f'{name}: {grades[name] or "all 0"}', fill='black')
        t.save(out / f'{stem}-{name.lower().replace(" & ", "-").replace(" ", "-")}.jpg', quality=92)
    sheet.save(out / f'{stem}-grades.jpg', quality=90)
    print(out / f'{stem}-grades.jpg')


if __name__ == '__main__':
    main()
