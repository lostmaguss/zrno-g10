#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Print host render: run camera/film_riso.c on a photo as the camera does.

The photo (a JPEG/PNG, or a CR2 developed as tools/acceptance.py does) is
converted to the signed UYVY422 frame Canon hands the stage at L (3648 x
2736) and printed in place with card/CHDK/FILM/PRINT.BIN, exactly as
film_rsui.c calls the kernel. --live renders the live view instead (the
frame shrunk to the 720 x 480 UYVYYY viewport, 320 x 240 masks painted into
the 720 x 240 overlay, shown 4:3).

  riso_render.py PHOTO --out DIR [--inks 0-6] [--separation 0-1] [--screen 0-1]
                 [--noise 0-4] [--register 0-3] [--flaws 0-3] [--paper 0-2]
                 [--style 0-6] [--variant N] [--seed N] [--live]
  riso_render.py PHOTO --out DIR --styles    every style's every palette
  riso_render.py PHOTO --out DIR --sheet     every ink set, and each row's values
"""
import argparse, ctypes as C, subprocess, sys, time
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import acceptance

W, H = 3648, 2736
ROWS = ('inks', 'separation', 'screen', 'noise', 'reg', 'flaws', 'paper', 'style', 'variant')
STYLE_ROWS = [(2, 2, 3), (0, 2, 0), (0, 0, 0), (1, 2, 1), (3, 0, 2), (0, 1, 0), (0, 0, 0)]   # film_rsui.c's defaults
DEFAULTS = dict(inks=0, separation=0, screen=0, noise=2, reg=2, flaws=3, paper=0, style=0, variant=0)


class Settings(C.Structure):
    _fields_ = [(n, C.c_ubyte) for n in ROWS]


def library():
    out = ROOT / 'build/riso.dylib'
    out.parent.mkdir(exist_ok=True)
    subprocess.run(['cc', '-O2', '-std=c99', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-I' + str(ROOT / 'camera'),
                    str(ROOT / 'camera/film_riso.c'), '-o', str(out)], check=True)
    lib = C.CDLL(str(out))
    for n in ('inks', 'separation', 'screen', 'noise', 'register', 'flaws', 'paper', 'style'):
        getattr(lib, f'film_riso_{n}_name').restype = C.c_char_p
    lib.film_riso_variant_name.restype = C.c_char_p
    lib.film_riso_work_size.restype = C.c_size_t
    lib.film_riso_work_size.argtypes = [C.c_uint]
    return lib


def data():
    d = (ROOT / 'card/CHDK/FILM/PRINT.BIN').read_bytes()
    return (C.c_ubyte * len(d)).from_buffer_copy(d), len(d)


def load(path):
    path = Path(path)
    if path.suffix.upper() == '.CR2':
        return acceptance.develop(path, path.with_suffix('.developed.png'))
    im = Image.open(path).convert('RGB')
    return np.asarray(im.resize((W, H), Image.LANCZOS) if im.size != (W, H) else im)


def render(lib, dat, frame_uyvy, s, seed=1234, w=W, h=H):
    buf = np.ascontiguousarray(frame_uyvy.copy())
    work = np.zeros(lib.film_riso_work_size(w) + 16, np.uint8)
    rc = lib.film_riso_render(buf.ctypes.data_as(C.c_void_p), w, h, w * 2, dat[0], C.byref(s), C.c_uint(seed),
                              work.ctypes.data_as(C.c_void_p), C.c_size_t(work.size), None)
    assert rc == 0, rc
    return acceptance.to_rgb(buf)


def viewport(rgb):
    small = np.asarray(Image.fromarray(rgb).resize((720, 480), Image.BILINEAR))
    a = np.asarray(Image.fromarray(small).convert('YCbCr'), dtype=np.int32)
    out = np.zeros((480, 1080), np.uint8)
    y = a[:, :, 0]
    out[:, 1::6], out[:, 3::6], out[:, 4::6], out[:, 5::6] = y[:, 0::4], y[:, 1::4], y[:, 2::4], y[:, 3::4]
    out[:, 0::6] = ((a[:, 0::4, 1] + a[:, 1::4, 1] + a[:, 2::4, 1] + a[:, 3::4, 1] + 2) // 4 - 128) % 256
    out[:, 2::6] = ((a[:, 0::4, 2] + a[:, 1::4, 2] + a[:, 2::4, 2] + a[:, 3::4, 2] + 2) // 4 - 128) % 256
    return out


def live(lib, dat, rgb, s):
    vp = np.ascontiguousarray(viewport(rgb))
    cw, ch = 320, 240
    yuv = np.zeros(3 * cw * ch, np.uint8); sep = np.zeros(3 * 8192, np.uint8); canvas = np.zeros(cw * ch, np.uint8)
    P = lambda a: a.ctypes.data_as(C.c_void_p)
    lib.film_riso_live_sample(P(vp), 720, 480, 1080, cw, ch, 0, P(yuv))
    lo, hi = C.c_int(), C.c_int()
    lib.film_riso_live_levels(P(yuv), cw * ch, 1, C.byref(lo), C.byref(hi))
    lib.film_riso_live_render(P(yuv), cw, ch, dat[0], C.byref(s), lo, hi, P(sep), P(canvas))
    overlay = np.zeros((240, 720), np.uint8)
    idx = (C.c_ubyte * 8)(*range(8))
    lib.film_riso_live_paint(P(canvas), cw, ch, idx, P(overlay), 720, 720, 240)
    pal = []
    for m in range(8):
        y, u, v = C.c_int(), C.c_int(), C.c_int()
        lib.film_riso_colour(dat[0], C.byref(s), m, C.byref(y), C.byref(u), C.byref(v))
        pal.append(np.clip([y.value + 1.402 * v.value, y.value - 0.344136 * u.value - 0.714136 * v.value, y.value + 1.772 * u.value], 0, 255))
    return Image.fromarray(np.array(pal, np.uint8)[overlay]).resize((640, 480), Image.NEAREST)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('photo')
    ap.add_argument('--out', type=Path, required=True)
    for k, v in DEFAULTS.items():
        ap.add_argument('--' + ('register' if k == 'reg' else k), dest=k, type=int, default=v)
    ap.add_argument('--seed', type=int, default=1234)
    ap.add_argument('--live', action='store_true')
    ap.add_argument('--sheet', action='store_true')
    ap.add_argument('--styles', action='store_true')
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    lib, dat = library(), data()
    assert lib.film_riso_data_check(dat[0], C.c_size_t(dat[1])) == 0, 'PRINT.BIN does not match film_riso.c'
    rgb = load(a.photo)
    stem = Path(a.photo).stem
    base = acceptance.to_uyvy(rgb)
    s = Settings(*(getattr(a, k) for k in ROWS))
    if a.styles:
        for st in range(1, 7):
            for v in range(lib.film_riso_variants(st)):
                s.style, s.variant = st, v
                s.noise, s.reg, s.flaws = STYLE_ROWS[st]
                im = render(lib, dat, base, s, a.seed)
                nm = f'{stem}-zp-{lib.film_riso_style_name(st).decode()}-{lib.film_riso_variant_name(st, v).decode()}'.replace(' ', '').replace('+', '')
                im.save(a.out / f'{nm}.jpg', quality=92, subsampling=1)
                live(lib, dat, rgb, s).save(a.out / f'{nm}-live.png')
                print(a.out / f'{nm}.jpg')
        return
    if a.sheet:
        tiles = []
        for i in range(7):
            s.inks = i
            tiles.append((lib.film_riso_inks_name(i).decode(), render(lib, dat, base, s, a.seed)))
        s.inks = a.inks
        for row, n in (('separation', 2), ('screen', 2), ('noise', 5), ('flaws', 4), ('paper', 3)):
            for v in range(n):
                if v == getattr(a, row): continue
                setattr(s, row, v)
                name = getattr(lib, f'film_riso_{row}_name')(v).decode()
                tiles.append((f'{row}: {name}', render(lib, dat, base, s, a.seed)))
            setattr(s, row, getattr(a, row))
        cols = 4
        sheet = Image.new('RGB', (cols * 616, (len(tiles) + cols - 1) // cols * 480), (30, 30, 30))
        d = ImageDraw.Draw(sheet)
        for i, (name, im) in enumerate(tiles):
            x, y = i % cols * 616 + 4, i // cols * 480 + 20
            sheet.paste(im.resize((608, 456), Image.LANCZOS), (x, y)); d.text((x, y - 16), name, fill=(240, 240, 240))
        sheet.save(a.out / f'{stem}-print-sheet.jpg', quality=90)
        print(a.out / f'{stem}-print-sheet.jpg')
        return
    t = time.time()
    im = render(lib, dat, base, s, a.seed)
    name = f'{stem}-print-i{a.inks}-s{a.separation}{a.screen}-n{a.noise}-r{a.reg}-f{a.flaws}-p{a.paper}-st{a.style}v{a.variant}'
    im.save(a.out / f'{name}.jpg', quality=95, subsampling=1)
    print(a.out / f'{name}.jpg', f'{time.time() - t:.1f} s on the Mac')
    if a.live:
        live(lib, dat, rgb, s).save(a.out / f'{name}-live.png')
        print(a.out / f'{name}-live.png')


if __name__ == '__main__':
    main()
