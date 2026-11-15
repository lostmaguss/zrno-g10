# SPDX-License-Identifier: GPL-2.0-or-later
"""Byte-exactness of the colour/halation kernels against a reference camera/ folder.

Builds camera/ and a reference copy (default: work/explore-speed/build/ref_head,
filled with `git show HEAD:camera/...`) as host libraries and runs both on
full 3648 x 2736 frames: the DIGIC-engine graded path (halation levels 1-3,
colour and mono tints, retro effects, toy lens, print effects, no scratch),
the CPU LUT path with halation, and the plain colour pass. When the new build
exports film_colour_rows_in_place, every case is also run with rows processed
in place, which must give the same bytes.

The test frames are your own full-size shots, in G12_FRAMES (default
work/frames): shot-1.jpg and shot-2.jpg, two camera JPEGs, and highlights.png,
a developed frame with a large bright area.

Run from the repository root:
  G12_WORKSPACE=$PWD work/test-venv/bin/python tools/speed_model/exact_halation.py
"""
import ctypes as C, hashlib, os, subprocess, sys, time
from pathlib import Path
import numpy as np
from PIL import Image

lab = Path(__file__).resolve().parents[2]
ws = Path(os.environ.get('G12_WORKSPACE', lab))
build = ws / 'work/explore-speed/build'; build.mkdir(parents=True, exist_ok=True)
FRAMES = Path(os.environ.get('G12_FRAMES', ws / 'work/frames'))
REF = Path(os.environ.get('G12_REFERENCE', build / 'ref_head'))
W, H = 3648, 2736
sys.path.insert(0, str(Path(__file__).resolve().parent))


def lib(src, tag):
    out = build / f'exact-{tag}.dylib'
    files = [src / n for n in ('film_colour.c', 'film_retro.c', 'film_toy.c', 'film_print.c')]
    subprocess.run(['cc', '-O2', '-std=gnu99', '-shared', '-fPIC', '-I', str(src), *map(str, files), '-o', str(out)], check=True)
    return C.CDLL(str(out))


class Tint(C.Structure):
    _fields_ = [('red', C.c_ushort), ('green', C.c_ushort), ('blue', C.c_ushort), ('highlight', C.c_void_p)]


class Print(C.Structure):
    _fields_ = [('dust', C.c_uint), ('dust_dark', C.c_uint), ('stamp', C.c_uint), ('text', C.c_char * 25)]


def uyvy(im):
    a = np.asarray(im.convert('YCbCr').resize((W, H)), dtype=np.int32)
    p = np.empty((H, W * 2), np.uint8); p[:, 1::2] = a[:, :, 0]
    p[:, 0::4] = ((a[:, 0::2, 1] + a[:, 1::2, 1] + 1) // 2 - 128) % 256
    p[:, 2::4] = ((a[:, 0::2, 2] + a[:, 1::2, 2] + 1) // 2 - 128) % 256
    return p.tobytes()


def frames():
    out = [(n, uyvy(Image.open(FRAMES / f))) for n, f in (('shot1', 'shot-1.jpg'), ('shot2', 'shot-2.jpg'), ('highlights', 'highlights.png'))]
    rng = np.random.default_rng(7)
    out.append(('noise', rng.integers(0, 256, W * H * 2, dtype=np.uint8).tobytes()))
    return out


def tint(name):
    t = (lab / 'card/CHDK/FILM/LUTS' / f'{name}.HWT').read_bytes()[4000:]
    assert t[:8] == b'HWTINT1\0'
    hl = C.create_string_buffer(t[16:272], 256)
    r, g, b = (int.from_bytes(t[8 + 2 * i:10 + 2 * i], 'little') for i in range(3))
    return Tint(r, g, b, C.cast(hl, C.c_void_p)), hl


def setup(L):
    vp, sz, u = C.c_void_p, C.c_size_t, C.c_uint
    L.film_uyvy_graded_retro.argtypes = [vp, sz, u, u, u, C.c_int, u, vp, vp, sz, vp, sz, vp, u, u, u, u, vp, sz, vp]
    L.film_uyvy_map_halo_cached_retro.argtypes = [vp, sz, u, u, u, vp, u, vp, sz, vp, sz, vp, u, u, u, u, vp, sz, vp]
    L.film_uyvy_map_cached_retro.argtypes = [vp, sz, u, u, u, vp, vp, sz, vp, u, u, u, u, vp, sz, vp]
    L.film_halo_work_size.argtypes = [u]; L.film_halo_work_size.restype = sz
    L.film_toy_work_size.argtypes = [u]; L.film_toy_work_size.restype = sz
    L.film_lut_prepare.argtypes = [vp, sz]


def call(L, kind, frame, p):
    img = C.create_string_buffer(frame, len(frame))
    work = C.create_string_buffer(L.film_halo_work_size(W)); scratch = C.create_string_buffer(16384)
    toyw = C.create_string_buffer(L.film_toy_work_size(W)) if p.get('toy') else None
    sc, cap = (scratch, 16384) if p.get('scratch', True) else (None, 0)
    common = (p.get('vig', 0), p.get('leaks', 0), 0x1234, p.get('toy', 0), toyw, L.film_toy_work_size(W) if toyw else 0,
              C.byref(p['print']) if p.get('print') else None)
    if kind == 'graded':
        rc = L.film_uyvy_graded_retro(img, len(frame), W, H, W * 2, p.get('mono', 0), p['level'], C.byref(p['tint']),
                                      work if p['level'] else None, len(work) if p['level'] else 0, sc, cap, None, *common)
    elif kind == 'cpu_halo':
        rc = L.film_uyvy_map_halo_cached_retro(img, len(frame), W, H, W * 2, p['lut'], p['level'], work, len(work), sc, cap, None, *common)
    else:
        rc = L.film_uyvy_map_cached_retro(img, len(frame), W, H, W * 2, p['lut'], sc, cap, None, *common)
    assert rc == 0, (kind, rc)
    return hashlib.sha256(img.raw).hexdigest()


def main():
    ref, new = lib(REF, 'ref'), lib(lab / 'camera', 'new')
    for L in (ref, new): setup(L)
    inplace = getattr(new, 'film_colour_rows_in_place', None) if hasattr(new, 'film_colour_rows_in_place') else None
    if inplace: inplace.argtypes = [C.c_int]
    f400, keep1 = tint('F400H'); hp5, keep2 = tint('HP5')
    luts = {}
    for name in ('PORTRA', 'HP5'):
        raw = (lab / 'card/CHDK/FILM/LUTS' / f'{name}.LUT').read_bytes()
        bufs = []
        for L in (ref, new):
            b = C.create_string_buffer(raw, len(raw)); assert L.film_lut_prepare(b, len(raw)) == 0; bufs.append(b)
        luts[name] = bufs
    stamp = Print(3, 0, 1, b"'26 11 10")
    cases = []
    for lv in (1, 2, 3):
        cases.append(('graded', dict(level=lv, tint=f400, leaks=1, vig=2)))
        cases.append(('cpu_halo', dict(level=lv, lut='PORTRA', leaks=2)))
    cases += [('graded', dict(level=3, tint=hp5, mono=1, vig=1)),
              ('graded', dict(level=2, tint=f400, toy=2)),
              ('graded', dict(level=3, tint=f400, print=stamp, leaks=3)),
              ('graded', dict(level=3, tint=f400, scratch=False)),
              ('graded', dict(level=0, tint=f400, leaks=2, vig=3)),
              ('cpu_halo', dict(level=3, lut='HP5', vig=2)),
              ('plain', dict(lut='PORTRA', leaks=1, vig=2)),
              ('plain', dict(lut='PORTRA', toy=3))]
    bad = 0; t0 = time.time()
    for fname, frame in frames():
        for kind, p in cases:
            def args(i):
                q = dict(p)
                if 'lut' in q: q['lut'] = luts[q['lut']][i]
                return q
            a = call(ref, kind, frame, args(0)); b = call(new, kind, frame, args(1)); c = None
            if inplace:
                inplace(1); c = call(new, kind, frame, args(1)); inplace(0)
            ok = a == b and (c is None or c == a)
            bad += not ok
            label = ' '.join(f'{k}={v}' for k, v in p.items() if k not in ('tint', 'lut', 'print'))
            extra = ('tint=HP5 ' if p.get('tint') is hp5 else '') + (f"lut={p['lut']} " if 'lut' in p else '') + ('print ' if 'print' in p else '')
            print(f"{'ok ' if ok else 'BAD'} {fname:6s} {kind:8s} {extra}{label}" + ('' if ok else f'  ref={a[:12]} new={b[:12]} inplace={(c or "-")[:12]}'), flush=True)
    # Narrow frames around each window (grid widths below, at and just above
    # 2r+1 for r = 18, 27, 36) and odd heights, graded and CPU paths.
    rng = np.random.default_rng(11); narrow = 0
    for w in (2, 8, 36, 38, 54, 56, 72, 74, 76, 110, 112, 146, 148, 150, 300):
        for h in (1, 3, 40, 97):
            fr = rng.integers(0, 256, w * h * 2, dtype=np.uint8).tobytes()
            for lv in (1, 2, 3):
                outs = []
                for i, L in enumerate((ref, new, new)):
                    if i == 2 and not inplace: continue
                    if i == 2: inplace(1)
                    img = C.create_string_buffer(fr, len(fr)); work = C.create_string_buffer(max(1, L.film_halo_work_size(w)))
                    sc = C.create_string_buffer(16384)
                    rc = L.film_uyvy_graded_retro(img, len(fr), w, h, w * 2, 0, lv, C.byref(f400), work, len(work), sc, 16384, None,
                                                  0, 0, 1, 0, None, 0, None)
                    rc2 = L.film_uyvy_map_halo_cached_retro(img, len(fr), w, h, w * 2, luts['PORTRA'][min(i, 1)], lv, work, len(work), sc, 16384,
                                                            None, 0, 0, 1, 0, None, 0, None)
                    if i == 2: inplace(0)
                    outs.append((rc, rc2, img.raw))
                narrow += 1; ran = getattr(main, "ran", 0); main.ran = ran + (outs[0][0] == 0 and outs[0][1] == 0)
                if any(o != outs[0] for o in outs[1:]):
                    bad += 1; print(f'BAD narrow w={w} h={h} level={lv} rc={[o[:2] for o in outs]}', flush=True)
    print(f'narrow frames: {narrow} cases checked, {getattr(main, "ran", 0)} ran both passes')
    print(f'{"all exact" if not bad else f"{bad} cases differ"} ({time.time() - t0:.0f} s, in-place {"tested" if inplace else "not available"})')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
