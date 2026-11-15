#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""ZrnoBoy host render: run camera/film_gboy.c on a photo as the camera does.

The photo (a JPEG/PNG, or a CR2 developed as tools/acceptance.py does) is
converted to the signed UYVY422 frame Canon hands the stage at L (3648 x
2736), sampled, dithered, framed and painted back exactly as
camera/film_stage.c calls the kernels. --live renders the live view: the
frame shrunk to the G12's 720 x 480 UYVYYY viewport, painted into the 720 x
240 overlay and shown at the screen's 4:3 shape.

  gboy_render.py PHOTO --out DIR [--frame N] [--palette N] [--contrast 1-16]
                 [--brightness -3..3] [--edges 0-3] [--no-dither] [--live]
  gboy_render.py PHOTO --out DIR --sheet     every frame, and every palette
"""
import argparse, ctypes as C, subprocess, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
W, H = 3648, 2736


class Settings(C.Structure):
    _fields_ = [(n, C.c_ubyte) for n in ('palette', 'frame', 'contrast', 'brightness', 'edges', 'dither', 'resolution')]


class Geom(C.Structure):
    _fields_ = [(n, C.c_uint) for n in ('cw', 'ch', 'wx', 'wy', 'ww', 'wh')]


class Levels(C.Structure):
    _fields_ = [('lo', C.c_int), ('hi', C.c_int)]


def library():
    out = ROOT / 'build/gboy.dylib'
    out.parent.mkdir(exist_ok=True)
    subprocess.run(['cc', '-O2', '-std=c99', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-I' + str(ROOT / 'camera'),
                    str(ROOT / 'camera/film_gboy.c'), '-o', str(out)], check=True)
    lib = C.CDLL(str(out))
    for name in ('film_gboy_palette_name', 'film_gboy_frame_name'):
        getattr(lib, name).restype = C.c_char_p
    lib.film_gboy_rgb.restype = C.c_uint
    return lib


def load(path):
    path = Path(path)
    if path.suffix.upper() == '.CR2':
        sys.path.insert(0, str(ROOT / 'tools'))
        import acceptance
        return acceptance.develop(path, path.with_suffix('.developed.png'))
    im = Image.open(path).convert('RGB')
    if im.size != (W, H):
        im = im.resize((W, H), Image.LANCZOS)
    return np.asarray(im)


def to_uyvy(rgb):
    a = np.asarray(Image.fromarray(rgb).convert('YCbCr'), dtype=np.int32)
    h, w = a.shape[:2]
    p = np.empty((h, w * 2), np.uint8)
    p[:, 1::2] = a[:, :, 0]
    p[:, 0::4] = ((a[:, 0::2, 1] + a[:, 1::2, 1] + 1) // 2 - 128) % 256
    p[:, 2::4] = ((a[:, 0::2, 2] + a[:, 1::2, 2] + 1) // 2 - 128) % 256
    return p


def to_rgb(p):
    y = p[:, 1::2].astype(np.float32)
    u = p[:, 0::4].astype(np.int8).astype(np.float32).repeat(2, axis=1)
    v = p[:, 2::4].astype(np.int8).astype(np.float32).repeat(2, axis=1)
    rgb = np.stack([y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u], -1)
    return Image.fromarray(np.clip(rgb + 0.5, 0, 255).astype(np.uint8))


def settings(a):
    return Settings(a.palette, a.frame, a.contrast - 1, a.brightness + 3, a.edges, 0 if a.no_dither else 1, a.resolution)


def canvas_for(lib, s, luma_fn, seed=1234, levels=None):
    g = Geom()
    lib.film_gboy_geometry(s.frame, s.resolution, C.byref(g))
    canvas = (C.c_ubyte * (320 * 240))()
    lib.film_gboy_frame(s.frame, s.resolution, canvas, C.c_uint(seed), None)
    luma = (C.c_ubyte * (g.ww * g.wh))()
    luma_fn(g, luma)
    lv = Levels()
    lib.film_gboy_levels_measure(luma, g.ww * g.wh, C.byref(lv))
    if levels is not None:
        lv = levels
    work = (C.c_ubyte * (g.ww * g.wh * 2))()
    lib.film_gboy_picture(luma, work, C.byref(s), C.byref(g), C.byref(lv), C.c_uint(seed), canvas)
    return g, canvas


def photo(lib, frame_uyvy, s, w=W, h=H):
    """The stage's render: sample, picture, paint (in place)."""
    buf = np.ascontiguousarray(frame_uyvy.copy())
    ptr = buf.ctypes.data_as(C.c_void_p)
    step = max(1, w // 1824)
    g, canvas = canvas_for(lib, s, lambda g, luma: lib.film_gboy_sample_uyvy(ptr, w, h, w * 2, g.ww, g.wh, step, luma))
    row = (C.c_ubyte * (w * 2))()
    assert lib.film_gboy_paint_uyvy(ptr, w, h, w * 2, canvas, C.byref(g), s.palette, row, None) == 0
    return to_rgb(buf)


def viewport(rgb):
    """The live view's 720 x 480 UYVYYY buffer of the scene."""
    small = np.asarray(Image.fromarray(rgb).resize((720, 480), Image.BILINEAR))
    a = np.asarray(Image.fromarray(small).convert('YCbCr'), dtype=np.int32)
    out = np.zeros((480, 1080), np.uint8)
    y = a[:, :, 0]
    out[:, 1::6], out[:, 3::6], out[:, 4::6], out[:, 5::6] = y[:, 0::4], y[:, 1::4], y[:, 2::4], y[:, 3::4]
    return out


def live(lib, rgb, s):
    vp = np.ascontiguousarray(viewport(rgb))
    ptr = vp.ctypes.data_as(C.c_void_p)
    g, canvas = canvas_for(lib, s, lambda g, luma: lib.film_gboy_sample_live(ptr, 720, 480, 1080, g.ww, g.wh, luma))
    overlay = np.zeros((240, 720), np.uint8)
    idx = (C.c_ubyte * 4)(0, 1, 2, 3)
    lib.film_gboy_live_paint(canvas, C.byref(g), idx, overlay.ctypes.data_as(C.c_void_p), 720, 720, 240)
    pal = np.array([[(lib.film_gboy_rgb(s.palette, k) >> sh) & 255 for sh in (16, 8, 0)] for k in range(4)], np.uint8)
    # The G12's screen: 720 bytes shown as 640 pixels (4:3 with 480 lines).
    return Image.fromarray(pal[overlay]).resize((640, 480), Image.NEAREST)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('photo')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--frame', type=int, default=1)
    ap.add_argument('--palette', type=int, default=0)
    ap.add_argument('--contrast', type=int, default=8)
    ap.add_argument('--brightness', type=int, default=0)
    ap.add_argument('--edges', type=int, default=2)
    ap.add_argument('--no-dither', action='store_true')
    ap.add_argument('--resolution', type=int, default=0, help='0: 128x96 picture, 1: 256x192')
    ap.add_argument('--live', action='store_true')
    ap.add_argument('--sheet', action='store_true')
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    lib = library()
    rgb = load(a.photo)
    stem = Path(a.photo).stem
    base = to_uyvy(rgb)
    if a.sheet:
        tiles = []
        for f in range(11):
            a.frame = f
            tiles.append((lib.film_gboy_frame_name(f).decode(), photo(lib, base, settings(a)).resize((608, 456), Image.NEAREST)))
        a.frame = 1
        for p in range(10):
            a.palette = p
            tiles.append((lib.film_gboy_palette_name(p).decode(), photo(lib, base, settings(a)).resize((608, 456), Image.NEAREST)))
        cols = 4
        sheet = Image.new('RGB', (cols * 616, (len(tiles) + cols - 1) // cols * 480), (30, 30, 30))
        d = ImageDraw.Draw(sheet)
        for i, (name, im) in enumerate(tiles):
            x, y = i % cols * 616 + 4, i // cols * 480 + 20
            sheet.paste(im, (x, y))
            d.text((x, y - 16), name, fill=(240, 240, 240))
        sheet.save(a.out / f'{stem}-zrnoboy-sheet.png')
        print(a.out / f'{stem}-zrnoboy-sheet.png')
        return
    s = settings(a)
    im = photo(lib, base, s)
    name = f'{stem}-zrnoboy-f{a.frame}-p{a.palette}-c{a.contrast}-r{a.resolution}'
    im.save(a.out / f'{name}.jpg', quality=95)
    print(a.out / f'{name}.jpg')
    if a.live:
        live(lib, rgb, s).save(a.out / f'{name}-live.png')
        print(a.out / f'{name}-live.png')


if __name__ == '__main__':
    main()
