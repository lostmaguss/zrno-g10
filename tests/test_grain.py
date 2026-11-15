# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual grain vs full-field reference, spatial statistics and G12 bounds."""
from pathlib import Path
import ctypes as C
import json
import subprocess
import numpy as np

lab = Path(__file__).resolve().parents[1]
subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror', str(lab / 'camera/film_grain.c'), '-o', str(lab / 'build/grain.dylib')], check=True)
lib = C.CDLL(str(lab / 'build/grain.dylib'))
lib.film_grain_work_size.argtypes = [C.c_uint]; lib.film_grain_work_size.restype = C.c_size_t
lib.film_uyvy_grain.argtypes = [C.c_void_p, C.c_size_t] + [C.c_uint] * 5 + [C.c_void_p, C.c_size_t, C.c_void_p]
def randoms(seed):
    while True:
        seed ^= (seed << 13) & 0xffffffff; seed ^= seed >> 17; seed ^= (seed << 5) & 0xffffffff
        yield seed
def reference(data, w, h, stride, level, seed):
    if not level: return data
    fine_seed = seed or 0x6d2b79f5; coarse_seed = (fine_seed ^ 0xa511e9b3) or 0x1b873593
    coarse = randoms(coarse_seed); fine = randoms(fine_seed)
    grid = np.array([int((sum((r >> k) & 255 for k in (0, 8, 16, 24)) - 510) / 4) for r in (next(coarse) for _ in range(((h + 1) // 2 + 1) * (w // 2 + 1)))], dtype=int).reshape(-1, w // 2 + 1)
    yy = np.arange(h) // 2; xx = np.arange(w) // 2
    vertical = np.where((np.arange(h) & 1)[:, None], np.trunc((grid[yy] + grid[yy + 1]) / 2).astype(int), grid[yy])
    clumps = np.where((np.arange(w) & 1)[None, :], np.trunc((vertical[:, xx] + vertical[:, xx + 1]) / 2).astype(int), vertical[:, xx])
    noise = np.empty((h, w), dtype=int)
    for y in range(h):
        for x in range(0, w, 2):
            r = next(fine); noise[y, x] = (r & 255) - ((r >> 8) & 255); noise[y, x + 1] = ((r >> 16) & 255) - (r >> 24)
    image = np.frombuffer(data, dtype=np.uint8).reshape(h, stride).copy(); luma = image[:, 1:w * 2:2].astype(int)
    weight = (4 * luma * (255 - luma) + 127) // 255
    scaled = (clumps * 4 + noise) * weight * {1: 2, 2: 4, 3: 7}[level]
    delta = np.where(scaled >= 0, (scaled + 16320) // 32640, -((-scaled + 16320) // 32640))
    image[:, 1:w * 2:2] = np.clip(luma + delta, 0, 255)
    return image.tobytes()
def run(data, w, h, stride, level, seed):
    image = C.create_string_buffer(b'A' * 32 + data + b'Z' * 32); n = lib.film_grain_work_size(w)
    work = C.create_string_buffer(b'B' * 32 + b'X' * n + b'Y' * 32)
    assert not lib.film_uyvy_grain(C.byref(image, 32), len(data), w, h, stride, level, seed, C.byref(work, 32), n, None)
    assert image.raw[:32] == b'A' * 32 and image.raw[len(data) + 32:len(data) + 64] == b'Z' * 32
    assert work.raw[:32] == b'B' * 32 and work.raw[n + 32:n + 64] == b'Y' * 32
    if not level: assert image.raw[32:32 + len(data)] == data and work.raw[32:32 + n] == b'X' * n
    return image.raw[32:32 + len(data)]
rng = np.random.default_rng(50)
for w, h in ((2, 1), (2, 3), (8, 8), (80, 67)):
    stride = w * 2 + 6; data = rng.integers(0, 256, (h, stride), dtype=np.uint8).tobytes()
    for level in range(4):
        for seed in (0, 0x13579bdf): assert run(data, w, h, stride, level, seed) == reference(data, w, h, stride, level, seed)
report = dict(workspace_3648_bytes=lib.film_grain_work_size(3648), reference='All levels match independent full-field reference at four geometries and two seeds')
stats = {}; last = 0; w = h = 512; data = bytes([0, 128, 0, 128]) * (w * h // 2)
for level in (1, 2, 3):
    out = run(data, w, h, w * 2, level, 0x13579bdf)
    image = np.frombuffer(out, dtype=np.uint8).reshape(h, w * 2); delta = image[:, 1::2].astype(float) - 128
    rms = float(np.sqrt((delta ** 2).mean())); correlation = float(np.corrcoef(delta[:, :-1].ravel(), delta[:, 1:].ravel())[0, 1])
    assert rms > last and abs(delta.mean()) < .15 and .15 < correlation < .7
    assert not image[:, ::2].any()
    assert out != run(data, w, h, w * 2, level, 0x2468ace1)
    stats[str(level)] = dict(mean_delta=float(delta.mean()), rms_luma=rms, adjacent_correlation=correlation); last = rms
for y in (0, 255):
    data = bytes([0, y, 0, y]) * (w * h // 2)
    assert run(data, w, h, w * 2, 3, 12345) == data
size = 3648 * 2736 * 2; data = bytes([0, 128, 0, 128]) * (size // 4)
image = C.create_string_buffer(data); n = lib.film_grain_work_size(3648); work = C.create_string_buffer(n); rows = []
callback = C.CFUNCTYPE(None, C.c_uint, C.c_uint)
@callback
def progress(row, total):
    assert total == 2736; rows.append(row)
assert not lib.film_uyvy_grain(image, size, 3648, 2736, 7296, 3, 12345, work, n, C.cast(progress, C.c_void_p))
assert rows == list(range(32, 2736, 32)) + [2736]
assert not np.frombuffer(image.raw[:size], dtype=np.uint8)[::2].any()
report.update(statistics=stats, black_white='Exact black/white preserved', full_frame='High: full G12 geometry, unchanged chroma, 86 ordered callbacks')
(lab / 'validation/grain-kernel-v0.5.0.json').write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report, indent=2))
