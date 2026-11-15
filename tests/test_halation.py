# SPDX-License-Identifier: GPL-2.0-or-later
"""Spatial halo vs an independent whole-frame convolution reference."""
from pathlib import Path
import bisect
import ctypes as C
import json
import random
import subprocess
import sys
import tarfile
import numpy as np

lab = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(lab / 'tools'))
from cube_pack import pack
subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', 'camera/film_colour.c', 'camera/film_retro.c', 'camera/film_toy.c', 'camera/film_print.c', '-o', 'build/colour.dylib'], cwd=lab, check=True)
lib = C.CDLL(str(lab / 'build/colour.dylib'))
lib.film_halo_work_size.argtypes = [C.c_uint]; lib.film_halo_work_size.restype = C.c_size_t
args = [C.c_void_p, C.c_size_t, C.c_uint, C.c_uint, C.c_uint, C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p]
lib.film_uyvy_map_halo.argtypes = args
lib.film_uyvy_map_halo_level.argtypes = args[:6] + [C.c_uint] + args[6:]
lib.film_halo_level_work_size.argtypes=[C.c_uint,C.c_uint];lib.film_halo_level_work_size.restype=C.c_size_t
lib.film_uyvy_patch.argtypes = [C.c_void_p, C.c_size_t] + [C.c_uint] * 7 + [C.c_void_p]
lib.film_lut_pixel.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
lib.film_lut_prepare.argtypes = [C.c_void_p, C.c_size_t]
def prepared(data):
    buffer = C.create_string_buffer(data); assert lib.film_lut_prepare(buffer, len(data)) == 0
    return buffer
linear = [round(((x / 255 / 12.92) if x / 255 <= .04045 else ((x / 255 + .055) / 1.055) ** 2.4) * 65535) for x in range(256)]
# Highlight weight: 0 up to luma 210, then a straight ramp to 255 at 255.
mask = [0 if x <= 210 else (2 * (x - 210) * 255 + 45) // 90 for x in range(256)]
identity = pack((2, [0.] * 3, [1.] * 3, [(r, g, b) for b in (0, 1) for g in (0, 1) for r in (0, 1)]), 17)
lut = prepared(identity); prepared_identity = lut.raw[:len(identity)]
def rnd16(x): return (x + 32768) // 65536 if x >= 0 else -((-x + 32768) // 65536)
def clip(x): return min(255, max(0, x))
def exact_encoded(value):
    if value >= 65535: return 255
    high = bisect.bisect_left(linear, value)
    return high - 1 if high and value - linear[high - 1] < linear[high] - value else high
def encoded(value): return exact_encoded(min(65535, ((value + 8) // 16) * 16))
assert max(abs(encoded(x) - exact_encoded(x)) for x in range(65536)) <= 1
def box3(a, box, axis):
    """Three rounded box means of window 2*box+1 along axis, on a copy
    edge-padded by 3*box (each box sees its full window)."""
    win = 2 * box + 1
    a = np.pad(a, [(3 * box, 3 * box) if k == axis else (0, 0) for k in range(a.ndim)], mode='edge')
    for _ in range(3):
        n = a.shape[axis] - win + 1
        a = (sum(np.take(a, range(k, k + n), axis=axis) for k in range(win)) + win // 2) // win
    return a
def tail_reference(highlights, width, height, level):
    """The tail on the 16-pixel grid, blended weight per mask column per row,
    or None for a frame taller than wide."""
    tw, th = -(-width // 16), -(-height // 16)
    if th > tw: return None
    xs = np.minimum(np.arange(tw * 16), width - 1); ys = np.minimum(np.arange(th * 16), height - 1)
    samples = highlights[ys][:, xs].reshape(th, 16, tw, 16)[:, 0::4, :, 0::4]  # rows and columns 0, 4, 8, 12 of each cell
    cells = (samples.sum((1, 3)) + 8) >> 4
    box = {1: 4, 2: 6, 3: 8}[level]
    cells = box3(box3(cells, box, 1), box, 0)
    def at(p, n):                                                              # cell before p (1/16 cell) and weight after
        i = np.where(p <= 0, 0, p >> 4); f = np.where(p <= 0, 0, p & 15)
        last = i + 1 >= n
        return np.where(last, n - 1, i), np.where(last, 0, f)
    j, g = at((np.arange(height) & ~1) - 7, th)                              # once per row pair, at its even row
    rows = (cells[j] * (16 - g)[:, None] + cells[np.minimum(j + (g > 0), th - 1)] * g[:, None] + 8) >> 4
    k, f = at(2 * np.arange(width // 2) - 7, tw)
    return (rows[:, k] * (16 - f) + rows[:, np.minimum(k + (f > 0), tw - 1)] * f + 8) >> 4
def reference(data, width, height, stride, lattice, level=1):
    y = np.frombuffer(data, dtype=np.uint8).reshape(height, stride)[:, 1:width * 2:2]
    highlights = np.asarray(mask, dtype=int)[y]
    # Independent whole-mask convolution and full-resolution interpolation.
    # Three box blurs per axis, each a third of the radius, on the mask
    # edge-padded by the whole radius (so every box sees its full window).
    box, strength = {1: (6, 48), 2: (9, 72), 3: (12, 96)}[level]
    win, radius = 2 * box + 1, 3 * box
    paired = np.pad(highlights, ((0, height % 2), (0, 0)), mode='edge')
    grid = (paired[::2, ::2] + paired[::2, 1::2] + paired[1::2, ::2] + paired[1::2, 1::2] + 2) // 4
    gh, gw = grid.shape
    coarse = np.pad(grid, radius, mode='edge')
    for axis in (1, 0):
        for _ in range(3):
            n = coarse.shape[axis] - win + 1
            coarse = (sum(np.take(coarse, range(k, k + n), axis=axis) for k in range(win)) + win // 2) // win
    assert coarse.shape == (gh, gw)
    yy = np.arange(height) // 2
    vertical = np.where((np.arange(height) % 2)[:, None], (coarse[yy] + coarse[np.minimum(yy + 1, gh - 1)] + 1) // 2, coarse[yy])
    tail = tail_reference(highlights, width, height, level)
    if tail is not None:                                                       # max(core, 0.4 core + 0.6 tail)
        vertical = np.maximum(vertical, ((102 * vertical + 64) >> 8) + ((154 * tail + 64) >> 8))
    xx = np.arange(width) // 2
    blur = np.where((np.arange(width) % 2)[None, :], (vertical[:, xx] + vertical[:, np.minimum(xx + 1, gw - 1)] + 1) // 2, vertical[:, xx])
    result = bytearray(data)
    for row in range(height):
        for x in range(0, width, 2):
            off = row * stride + x * 2; u, y0, v, y1 = data[off:off + 4]
            u = u if u < 128 else u - 256; v = v if v < 128 else v - 256
            colours = []
            for col, yy in ((x, y0), (x + 1, y1)):
                rgb = [clip(yy + rnd16(91881 * v)), clip(yy - rnd16(22553 * u + 46802 * v)), clip(yy + rnd16(116130 * u))]
                d = max(0, int(blur[row, col]) - mask[yy])
                energy = (d * strength * 257 + 50) // 100
                if d < 8: energy = (energy * d + 4) >> 3                         # the toe
                if energy:
                    rgb[0] = encoded(linear[rgb[0]] + energy)
                    rgb[1] = encoded(linear[rgb[1]] + (energy + 2) // 5)
                source = (C.c_ubyte * 3)(*rgb); target = (C.c_ubyte * 3)()
                lib.film_lut_pixel(lattice, source, target); colours.append(list(target))
            a, b = colours; sums = [a[k] + b[k] for k in range(3)]
            # C truncates signed division toward zero before the fixed round.
            avg = lambda z: z // 2 if z >= 0 else -((-z) // 2)
            uu = max(-128, min(127, rnd16(avg(-11059 * sums[0] - 21709 * sums[1] + 32768 * sums[2]))))
            vv = max(-128, min(127, rnd16(avg(32768 * sums[0] - 27439 * sums[1] - 5329 * sums[2]))))
            result[off:off + 4] = bytes([uu % 256, clip(rnd16(19595 * a[0] + 38470 * a[1] + 7471 * a[2])), vv % 256, clip(rnd16(19595 * b[0] + 38470 * b[1] + 7471 * b[2]))])
    return bytes(result)
def run(data, width, height, stride, lattice, level=1):
    size = lib.film_halo_level_work_size(width,level); image = C.create_string_buffer(b'A' * 32 + data + b'Z' * 32)
    work = C.create_string_buffer(b'B' * 32 + b'X' * size + b'Y' * 32)
    ip = C.cast(C.byref(image, 32), C.c_void_p); wp = C.cast(C.byref(work, 32), C.c_void_p)
    assert not lib.film_uyvy_map_halo_level(ip, len(data), width, height, stride, lattice, level, wp, size, None)
    assert image.raw[:32] == b'A' * 32 and image.raw[32 + len(data):64 + len(data)] == b'Z' * 32
    assert work.raw[:32] == b'B' * 32 and work.raw[32 + size:64 + size] == b'Y' * 32
    return image.raw[32:32 + len(data)]
rng = random.Random(31)
for width, height in ((2, 1), (2, 3), (8, 8), (80, 64), (96, 200)):
    stride = width * 2 + 8
    data = bytearray(rng.randrange(256) for _ in range(stride * height))
    # Hard highlights at both borders and a central bright square.
    for y in range(height):
        for x in range(width):
            data[y * stride + x * 2 + 1] = 255 if (x < 3 or x > width - 4 or (width // 3 <= x < width * 2 // 3 and height // 3 <= y < height * 2 // 3)) else 30
    for level in (1,2,3): assert run(bytes(data), width, height, stride, lut, level) == reference(bytes(data), width, height, stride, lut, level)
    # Padded row bytes remain untouched.
    out = run(bytes(data), width, height, stride, lut)
    for y in range(height): assert out[y * stride + width * 2:(y + 1) * stride] == data[y * stride + width * 2:(y + 1) * stride]
# Small round lights far apart on a dark frame: rows with no highlight
# anywhere near them take the zero-row shortcuts, which must not change bytes.
width, height = 360, 300; stride = width * 2
data = bytearray(bytes([0, 30, 0, 30]) * (width * height // 2))
for cx, cy, r in ((40, 30, 4), (300, 150, 7), (90, 260, 2), (359, 299, 3)):
    for y in range(max(0, cy - r), min(height, cy + r + 1)):
        for x in range(max(0, cx - r), min(width, cx + r + 1)):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r: data[y * stride + x * 2 + 1] = 255
for level in (1, 2, 3): assert run(bytes(data), width, height, stride, lut, level) == reference(bytes(data), width, height, stride, lut, level)
width, height, stride = 80, 64, 160
for level in (30, 200, 255):
    data = bytes([0, level, 0, level]) * (width * height // 2)
    normal = C.create_string_buffer(data)
    assert not lib.film_uyvy_patch(normal, len(data), width, height, stride, 0, 0, width, height, lut)
    assert run(data, width, height, stride, lut) == normal.raw[:len(data)]
# A bright edge creates warm spill into dark pixels; the HP5 LUT stays neutral.
data = bytes([0, 30, 0, 30]) * (width * height // 2)
data = bytearray(data)
for y in range(20, 44):
    for x in range(30, 50): data[y * stride + x * 2 + 1] = 255
out = run(bytes(data), width, height, stride, lut)
assert out != data
hp5 = prepared((lab / 'card/CHDK/FILM/LUTS/HP5.LUT').read_bytes())
mono = run(bytes(data), width, height, stride, hp5)
assert all(mono[i] == 0 and mono[i + 2] == 0 for i in range(0, len(mono), 4))
gold33 = prepared((lab / 'luts/GOLD33.LUT').read_bytes())
assert run(bytes(data), width, height, stride, gold33) == reference(bytes(data), width, height, stride, gold33)
# Reject short/unaligned/overlapping work and malformed geometry before writes.
image = C.create_string_buffer(bytes(data)); needed = lib.film_halo_level_work_size(width,1); work = C.create_string_buffer(needed + 2)
for w, h, pitch, ptr, capacity in ((width, height, stride, C.addressof(work), needed - 1), (width, height, stride, C.addressof(work) + 1, needed), (width, height, stride, C.addressof(image), needed), (width, height, stride, C.addressof(lut), needed), (width - 1, height, stride, C.addressof(work), needed), (width, height, stride - 1, C.addressof(work), needed)):
    assert lib.film_uyvy_map_halo(image, len(data), w, h, pitch, lut, ptr, capacity, None) == -1
    assert image.raw[:len(data)] == bytes(data)
assert lut.raw[:len(identity)] == prepared_identity
# Kernels reject a LUT that was validated but never prepared.
unprepared = C.create_string_buffer(identity)
assert lib.film_uyvy_map_halo(image, len(data), width, height, stride, unprepared, work, needed, None) == -1
assert image.raw[:len(data)] == bytes(data)
report = dict(workspace_3648_bytes=lib.film_halo_work_size(3648), reference='All three levels match independent half-mask three-box convolution and interpolation at five geometries including borders, and sparse round lights', inactive_highlights='Uniform dark/midtone/white match LUT-only', monochrome='HP5 output chroma remains zero', bounds='Image/work canaries, row padding, pre-write geometry/alignment/overlap rejection pass')
size = 3648 * 2736 * 2
image = C.create_string_buffer(b'A' * 32 + bytes([0, 30, 0, 30]) * (size // 4) + b'Z' * 32)
needed = lib.film_halo_work_size(3648); work = C.create_string_buffer(b'B' * 32 + b'X' * needed + b'Y' * 32)
rows = []; callback = C.CFUNCTYPE(None, C.c_uint, C.c_uint)
@callback
def progress(row, total):
    assert total == 2736
    rows.append(row)
for level in (1,2,3):
    rows.clear()
    assert not lib.film_uyvy_map_halo_level(C.byref(image,32),size,3648,2736,7296,lut,level,C.byref(work,32),needed,C.cast(progress,C.c_void_p))
    assert rows == list(range(32,2736,32)) + [2736]
assert image.raw[:32] == b'A' * 32 and image.raw[size + 32:size + 64] == b'Z' * 32
assert image.raw[32:size + 32] == bytes([0, 30, 0, 30]) * (size // 4)
assert work.raw[:32] == b'B' * 32 and work.raw[needed + 32:needed + 64] == b'Y' * 32
report['profile_workspace_bytes'] = {str(level):lib.film_halo_level_work_size(3648,level) for level in (1,2,3)}
report['full_frame'] = '3648x2736 output/work canaries and all bytes pass; 86 ordered progress callbacks'
(lab / 'validation/halation-kernel-v0.4.0.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
