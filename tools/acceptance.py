#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Acceptance render: run the camera's effect kernels on a real G12 CR2.

Every effect change must pass this on the reference shot before firmware goes
to the camera (see docs/ARCHITECTURE.md, "Changing and testing"). The CR2 is developed to an
sRGB frame of the size the stage hook grades (3648 x 2736), converted to the
signed UYVY422 the camera hands us, and run through the same calls in the
same order as camera/film_stage.c: the look through the emulated DIGIC
colour table (the look's .HWT), the effects on the graded frame (halation,
toy lens, vignette and light leaks), then bloom, then grain. --engine cpu
renders the exact CPU LUT instead, the host reference the tables are fitted
to. Sources are compiled fresh from camera/ on every run, so the render is
always of the current code.

Outputs in --out:
  <stem>-overview.jpg  look only | [baseline |] look + effects [| camera JPEG]
  <stem>-edges.png     100% crops of the four corners and four edge centres:
                       [baseline,] this build[, camera JPEG]

--baseline REF renders the same shot with the kernels of a git ref (for
example main or HEAD) for a before/after review; --src renders another source
directory instead of camera/. No automatic score stands in for looking: on a
real photo the scene's own edges swamp any line measure. The known failure
modes have synthetic tests in tests/ instead (for example the Toy lens
straight-line checks in tests/test_toy.c).

Needs numpy, pillow and rawpy (pip install rawpy) and a host C compiler.
"""
import argparse, ctypes as C, os, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
W, H = 3648, 2736
SOURCES = ['film_colour.c', 'film_retro.c', 'film_toy.c', 'film_print.c', 'film_bloom.c', 'film_grain.c']
# Looks whose dust shows dark (slides and instant film); prints from negatives show it light.
DARK_DUST = {'VELVIA.LUT', 'KCHROME.LUT', 'PROVIA.LUT', 'ASTIA.LUT', 'POLA669.LUT'}
STAMPS = {'off': 0, 'bottom-right': 1, 'bottom-left': 2, 'top-right': 3, 'top-left': 4}


class PrintSettings(C.Structure):
    _fields_ = [('dust', C.c_uint), ('dust_dark', C.c_uint), ('stamp', C.c_uint),
                ('text', C.c_char * 25)]


def print_settings(fx):
    if not (fx.get('dust') or fx.get('stamp')):
        return None
    return C.byref(PrintSettings(fx['dust'], fx['dust_dark'], fx['stamp'], fx['text'].encode('ascii')[:24]))


def build_library(tmp, src, hw_mask=False):
    out = Path(tmp) / 'film_effects.so'
    cmd = [os.environ.get('CC', 'cc'), '-O2', '-shared', '-fPIC', '-I', str(ROOT / 'camera'), '-o', str(out)]
    if hw_mask:                                          # the FILM_HWMASK build (camera: FILM_HWMASK=1 integrate.py)
        cmd.append('-DFILM_HWMASK')
    cmd += [str((src if (src / s).exists() else ROOT / 'camera') / s) for s in SOURCES]
    subprocess.run(cmd, check=True)
    lib = C.CDLL(str(out))
    lib.film_lut_prepare.argtypes = [C.c_void_p, C.c_size_t]
    for name in ('film_toy_work_size', 'film_bloom_work_size', 'film_grain_work_size'):
        getattr(lib, name).restype = C.c_size_t
        getattr(lib, name).argtypes = [C.c_uint]
    lib.film_halo_level_work_size.restype = C.c_size_t
    lib.film_halo_level_work_size.argtypes = [C.c_uint, C.c_uint]
    u, p, s = C.c_uint, C.c_void_p, C.c_size_t
    lib.film_uyvy_map_cached_retro.argtypes = [p, s, u, u, u, p, p, s, p, u, u, u, u, p, s, p]
    lib.film_uyvy_map_halo_cached_retro.argtypes = [p, s, u, u, u, p, u, p, s, p, s, p, u, u, u, u, p, s, p]
    lib.film_uyvy_bloom.argtypes = [p, s, u, u, u, u, p, s, p]
    if hasattr(lib, 'film_bloom_fast'):                 # not in a --baseline from before it
        lib.film_bloom_fast.argtypes = [C.c_int]
    if hasattr(lib, 'film_halo_fast'):
        lib.film_halo_fast.argtypes = [C.c_int]
    if hasattr(lib, 'film_bloom_use_quarter'):          # not in a --baseline from before it
        lib.film_bloom_use_quarter.argtypes = [p, u, u, u]
    lib.film_uyvy_soften.argtypes = [p, s, u, u, u, u, p, s, p]
    if hasattr(lib, 'film_uyvy_graded_retro'):          # not in a --baseline from before it
        lib.film_uyvy_graded_retro.argtypes = [p, s, u, u, u, C.c_int, u, p, p, s, p, s, p, u, u, u, u, p, s, p]
    lib.film_uyvy_grain.argtypes = [p, s, u, u, u, u, u, p, s, p]
    return lib


def develop(cr2, cache):
    if cache.exists():
        return np.asarray(Image.open(cache).convert('RGB'))
    import rawpy
    with rawpy.imread(str(cr2)) as raw:
        sz = raw.sizes
        rgb = raw.postprocess(use_camera_wb=True, output_bps=8, user_flip=0)
    # Crop to the camera's JPEG area (crop margins are given on the raw frame).
    x0, y0 = sz.crop_left_margin - sz.left_margin, sz.crop_top_margin - sz.top_margin
    if (sz.crop_width, sz.crop_height) != (W, H) or x0 < 0 or y0 < 0:
        sys.exit(f'{cr2}: unexpected crop {sz}')
    rgb = np.ascontiguousarray(rgb[y0:y0 + H, x0:x0 + W])
    Image.fromarray(rgb).save(cache)
    return rgb


def to_uyvy(rgb):
    a = np.asarray(Image.fromarray(rgb).convert('YCbCr'), dtype=np.int32)
    p = np.empty((H, W * 2), np.uint8)
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


def hw_quarter(img):
    """The DIGIC resize engine's 1/4 shrink with filter arguments (4,2), as
    fitted from the camera probe: horizontal taps 1/8, 1/8, 3/16 x 4 on source
    x 4x+2..4x+7, vertical 1/3 each on rows 4y+1..4y+3. The last row and column,
    which the hardware gets wrong, are not read (Bloom uses their neighbours).
    Returns the 912 x 684 UYVY image the camera hands Bloom (luma only matters)."""
    y = img[:, 1::2].astype(np.int32)
    qw, qh = (W + 3) // 4, (H + 3) // 4
    yp = np.pad(y, ((0, 8), (0, 8)), mode='edge')
    rows = sum(yp[d:d + 4 * qh:4] for d in (1, 2, 3))
    out = sum(t * rows[:, 2 + k:2 + k + 4 * qw:4] for k, t in enumerate((2, 2, 3, 3, 3, 3)))
    q = np.full((qh, qw * 2), 128, np.uint8)
    q[:, 1::2] = np.clip((out + 24) // 48, 0, 255)
    return np.ascontiguousarray(q)


def digic_grade(img, words):
    """Canon's develop with the look's hardware table, emulated: the RGB the
    table sees (Canon's UYVY decoded as the camera decodes it), the table's
    tetrahedral YCbCr, luma per pixel and chroma averaged per pair."""
    import hw_table
    u = img[:, 0::4].astype(np.int8).astype(np.float64); v = img[:, 2::4].astype(np.int8).astype(np.float64)
    out = img.copy()
    yuv = []
    for k in (1, 3):
        y = img[:, k::4].astype(np.float64)
        rgb = np.stack([y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u], -1)
        rgb = np.clip(np.floor(rgb + 0.5), 0, 255).astype(np.int64)
        yuv.append(hw_table.apply_yuv(words, rgb.reshape(-1, 3)).reshape(rgb.shape))
    for k, t in zip((1, 3), yuv):
        out[:, k::4] = np.clip(np.rint(t[..., 0]), 0, 255).astype(np.uint8)
    out[:, 0::4] = np.clip(np.rint((yuv[0][..., 1] + yuv[1][..., 1]) / 2), -128, 127).astype(np.int8).view(np.uint8)
    out[:, 2::4] = np.clip(np.rint((yuv[0][..., 2] + yuv[1][..., 2]) / 2), -128, 127).astype(np.int8).view(np.uint8)
    return out


def render(lib, base, lut, fx):
    img = np.ascontiguousarray(base.copy())
    ptr, size, stride = img.ctypes.data, img.nbytes, W * 2
    scratch = C.create_string_buffer(16384)
    toy_n = lib.film_toy_work_size(W) if fx['toy'] else 0
    toy = C.create_string_buffer(toy_n) if toy_n else None
    seed = fx['seed'] ^ 0xa3b195f1
    if hasattr(lib, 'film_halo_fast'):                  # FASTHALO.TXT on the camera
        lib.film_halo_fast(1 if fx.get('fast_halation') else 0)
    if fx.get('engine') == 'digic':
        import hw_table
        words, tint, highlight = hw_table.read_hwt(fx['hwt'])
        if fx.get('halation_colour') == 'amber':           # the Halation colour row: the look's .HWA
            tint = hw_table.read_hwa(fx['hwt'].with_suffix('.HWA'))
        img[:] = digic_grade(img, words)
        mono = lut.raw[10] & 1
        hn = lib.film_halo_level_work_size(W, fx['halation']) if fx['halation'] else 0
        halo = C.create_string_buffer(hn) if hn else None
        class Tint(C.Structure):
            _fields_ = [('red', C.c_ushort), ('green', C.c_ushort), ('blue', C.c_ushort), ('highlight', C.c_void_p)]
        hl = (C.c_ubyte * 256)(*highlight)
        tint_s = Tint(*tint, C.cast(hl, C.c_void_p))
        tint_c = C.cast(C.pointer(tint_s), C.c_void_p)
        rc = lib.film_uyvy_graded_retro(ptr, size, W, H, stride, mono, fx['halation'], tint_c, halo, hn,
                                        scratch, 16384, None, fx['vignette'], fx['leaks'], seed,
                                        fx['toy'], toy, toy_n, print_settings(fx))
    elif fx['halation']:
        hn = lib.film_halo_level_work_size(W, fx['halation'])
        halo = C.create_string_buffer(hn)
        rc = lib.film_uyvy_map_halo_cached_retro(ptr, size, W, H, stride, lut, fx['halation'], halo, hn,
                                                  scratch, 16384, None, fx['vignette'], fx['leaks'], seed,
                                                  fx['toy'], toy, toy_n, print_settings(fx))
    else:
        rc = lib.film_uyvy_map_cached_retro(ptr, size, W, H, stride, lut, scratch, 16384, None,
                                             fx['vignette'], fx['leaks'], seed, fx['toy'], toy, toy_n, print_settings(fx))
    assert rc == 0, f'colour pass failed: {rc}'
    if fx.get('soft'):
        n = lib.film_bloom_work_size(W); work = C.create_string_buffer(n)
        assert lib.film_uyvy_soften(ptr, size, W, H, stride, fx['soft'], work, n, None) == 0
    if fx['bloom']:
        n = lib.film_bloom_work_size(W); work = C.create_string_buffer(n)
        if fx.get('hw_mask') and hasattr(lib, 'film_bloom_use_quarter'):
            q = hw_quarter(img)
            lib.film_bloom_use_quarter(q.ctypes.data, q.shape[1], q.shape[1] // 2, q.shape[0])
        fast = fx.get('fast_bloom') and hasattr(lib, 'film_bloom_fast')   # FASTBLOOM.TXT on the camera
        if fast: lib.film_bloom_fast(1)
        assert lib.film_uyvy_bloom(ptr, size, W, H, stride, fx['bloom'], work, n, None) == 0
        if fast: lib.film_bloom_fast(0)
    if fx['grain']:
        n = lib.film_grain_work_size(W); work = C.create_string_buffer(n)
        assert lib.film_uyvy_grain(ptr, size, W, H, stride, fx['grain'], fx['seed'], work, n, None) == 0
    return img


CROPS = [('top-left', 0, 0), ('top', W // 2 - 300, 0), ('top-right', W - 600, 0),
         ('left', 0, H // 2 - 300), ('right', W - 600, H // 2 - 300),
         ('bottom-left', 0, H - 600), ('bottom', W // 2 - 300, H - 600), ('bottom-right', W - 600, H - 600)]


def label(im, text):
    d = ImageDraw.Draw(im); d.rectangle((0, 0, im.width, 20), fill=0); d.text((6, 4), text, fill=(255, 255, 255))
    return im


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cr2', type=Path)
    ap.add_argument('--look', default='PORTRA.LUT', help='LUT file, or a name in card/CHDK/FILM/LUTS')
    for k in ('toy', 'vignette', 'leaks', 'halation', 'bloom', 'grain', 'soft'):
        ap.add_argument('--' + k, type=int, default=0, choices=range(4), help='level 0-3')
    ap.add_argument('--halation-colour', default='red', choices=('red', 'amber'), help='halation colour (digic engine)')
    ap.add_argument('--dust', type=int, default=0, choices=range(4), help='dust and scratches level 0-3')
    ap.add_argument('--stamp', default='off', choices=STAMPS, help='date stamp corner')
    ap.add_argument('--text', default="'25 11 05", help='stamp text (printable ASCII, up to 24)')
    ap.add_argument('--engine', default='digic', choices=('digic', 'cpu'),
                    help="digic (the camera): Canon's hardware colour table (the look's .HWT, emulated), then the effects on the "
                         "graded frame; cpu: the exact CPU LUT the tables are fitted to (a host reference, not on the camera)")
    ap.add_argument('--hw-mask', action='store_true', help='Bloom reads its mask from the emulated DIGIC 1/4 shrink, as a FILM_HWMASK build with HWMASK.TXT on the card')
    ap.add_argument('--fast-bloom', action='store_true', help='fast Bloom, as with FASTBLOOM.TXT on the card (approximate)')
    ap.add_argument('--fast-halation', action='store_true', help='fast halation, as with FASTHALO.TXT on the card (approximate)')
    ap.add_argument('--seed', type=lambda s: int(s, 0), default=0x12345678)
    ap.add_argument('--camera-jpg', type=Path, help='camera JPEG of the same shot, shown for comparison')
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'acceptance')
    ap.add_argument('--src', type=Path, default=ROOT / 'camera',
                    help='directory with replacement kernel sources (others come from camera/)')
    ap.add_argument('--baseline', metavar='REF', help='git ref whose kernels to render alongside, e.g. main')
    args = ap.parse_args()
    fx = {k: getattr(args, k) for k in ('toy', 'vignette', 'leaks', 'halation', 'bloom', 'grain', 'soft', 'seed', 'dust', 'text', 'hw_mask', 'engine', 'fast_bloom', 'fast_halation', 'halation_colour')}
    if args.halation_colour != 'red' and args.engine != 'digic':
        ap.error('--halation-colour amber needs the digic engine (the look\'s .HWA)')
    fx.update(stamp=STAMPS[args.stamp])

    args.out.mkdir(parents=True, exist_ok=True)
    stem = args.cr2.stem
    look = Path(args.look) if Path(args.look).exists() else ROOT / 'card/CHDK/FILM/LUTS' / args.look
    fx['dust_dark'] = int(look.name.upper() in DARK_DUST)
    fx['hwt'] = look.with_suffix('.HWT')
    base = to_uyvy(develop(args.cr2, args.out / f'{stem}-developed.png'))
    raw = look.read_bytes()
    effects = ' '.join(f'{k}={v}' for k, v in fx.items() if v and k not in ('seed', 'text', 'dust_dark', 'hwt') and v not in ('cpu', 'red')) or 'none'

    def run(src, fx=fx):
        with tempfile.TemporaryDirectory() as tmp:
            lib = build_library(tmp, src, fx['hw_mask'])
            lut = C.create_string_buffer(raw)
            assert lib.film_lut_prepare(lut, len(raw)) == 0, f'{look}: not a G12 LUT'
            return lib, lut, render(lib, base, lut, fx)

    lib, lut, on = run(args.src)
    off = render(lib, base, lut, dict(fx, toy=0, vignette=0, leaks=0, halation=0, bloom=0, grain=0, soft=0, dust=0, stamp=0))
    renders = [(f'{look.name} only', to_rgb(off), False)]
    if args.baseline:
        with tempfile.TemporaryDirectory() as old:
            archive = subprocess.run(['git', '-C', str(ROOT), 'archive', args.baseline, 'camera'],
                                     check=True, capture_output=True).stdout
            subprocess.run(['tar', '-x', '-C', old], input=archive, check=True)
            base_fx = dict(fx)                               # the same engine and effects
            base_effects = ' '.join(f'{k}={v}' for k, v in base_fx.items() if v and k not in ('seed', 'text', 'dust_dark', 'hwt') and v not in ('cpu', 'red')) or 'none'
            renders.append((f'{args.baseline}: {base_effects}', to_rgb(run(Path(old) / 'camera', base_fx)[2]), True))
    renders.append((f'this build: {effects}', to_rgb(on), True))
    if args.camera_jpg:
        renders.append(('camera JPEG', Image.open(args.camera_jpg).convert('RGB'), True))

    tw, th = W // 4, H // 4
    sheet = Image.new('RGB', (len(renders) * (tw + 6) - 6, th), 'white')
    for i, (name, im, _) in enumerate(renders):
        sheet.paste(label(im.resize((tw, th), Image.LANCZOS), name), (i * (tw + 6), 0))
    sheet.save(args.out / f'{stem}-overview.jpg', quality=90)
    rows = [(n, im) for n, im, crop in renders if crop]
    edges = Image.new('RGB', (4 * 606 - 6, 2 * 606 * len(rows) - 6), 'white')
    for r, (name, im) in enumerate(rows):
        for i, (where, x, y) in enumerate(CROPS):
            c = label(im.crop((x, y, x + 600, y + 600)), f'{name} | {where} 100%')
            edges.paste(c, ((i % 4) * 606, (r * 2 + i // 4) * 606))
    edges.save(args.out / f'{stem}-edges.png')
    print(f'{stem}: {look.name} {effects}')
    print('wrote', args.out / f'{stem}-overview.jpg', args.out / f'{stem}-edges.png')
    return 0


if __name__ == '__main__':
    sys.exit(main())
