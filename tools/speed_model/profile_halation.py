# SPDX-License-Identifier: GPL-2.0-or-later
"""Profile the DIGIC-engine halation pass (film_uyvy_graded_retro) by function.

What the camera runs since 0.10: Canon grades the look in hardware, then
Zrno adds halation (and the retro effects) to the graded frame. This
compiles camera/film_colour.c (+ retro, toy, print) the way the core does
(-O2 ARM, FILM_COMPACT functions as Thumb), runs it in Unicorn on two
full-width bands, and attributes modelled cycles, instructions and D-cache
line fills to functions. The frame is modelled as cacheable (FRAMECACHE).

Steady state: the difference between a short and a long band, per pixel, so
setup (mask pre-fill, tables) cancels. Line fills are costed at FILL_CYCLES,
an estimate from the dg_mem_probe camera run (cached reads: ~690 ns per
32-byte line including the loop). Treat the result as proportional.

The bands come from your own full-size shots in G12_FRAMES (default
work/frames): shot-1.jpg, a camera JPEG graded F400H in the camera, and
highlights.png, a developed frame with a large bright area.

Run from the repository root:
  work/test-venv/bin/python tools/speed_model/profile_halation.py [--look F400H]
"""
import sys, argparse, bisect, collections, struct, subprocess
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import numpy as np
from PIL import Image
import harness
from harness import (IMAGE, LUT, SCRATCH, SCRATCH_BYTES, WORK, STACK, RET, Cache, _static_cycles, prefix, lab, root, FRAMES,
                     uyvy_from_image)
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_BLOCK, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
import capstone as cs

FILL_CYCLES = 70
TINT, HIGHLIGHT = LUT, LUT + 0x100
arm = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_ARM); arm.detail = True
thumb = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_THUMB); thumb.detail = True


def build(src_dir=None, tag='halation-graded'):
    src = Path(src_dir or lab / 'camera') / 'film_colour.c'
    stub = harness.build / 'halation_stub.c'   # film_print allocates its workspace; not called with print == 0
    stub.write_text('void *malloc(unsigned n){static unsigned char *p=(unsigned char *)0x480000;void *r=p;p+=(n+7)&~7u;return r;}\n'
                    'void free(void *p){(void)p;}\n')
    elf = harness.build / f'{tag}.elf'; binary = harness.build / f'{tag}.bin'
    subprocess.run([prefix + 'gcc', '-O2', '-g', '-finline', '-marm', '-mcpu=arm946e-s', '-fno-builtin', '-nostdlib',
                    '-DFILM_LAB_CAMERA', '-Wl,-Ttext=0x200000', '-Wl,--entry=film_uyvy_graded_retro',
                    str(src), *harness.extra_sources(src), str(harness.support), str(stub), '-lgcc', '-o', str(elf)], check=True)
    subprocess.run([prefix + 'objcopy', '-O', 'binary', str(elf), str(binary)], check=True)
    syms = []; entry = None; flag = None   # readelf keeps bit 0 (Thumb) in FUNC values; nm does not
    for line in subprocess.check_output([prefix + 'readelf', '-sW', str(elf)], text=True).splitlines():
        p = line.split()
        if len(p) >= 8 and p[3] == 'FUNC':
            v = int(p[1], 16); syms.append((v & ~1, p[7]))
            if p[7] == 'film_uyvy_graded_retro': entry = v
        if len(p) >= 8 and p[3] == 'OBJECT' and p[7] == 'rows_in_place': flag = int(p[1], 16)
    syms.sort()
    return dict(binary=binary.read_bytes(), elf=elf, syms=syms, entry=entry, flag=flag)


def look_tint(name):
    hwt = (lab / 'card/CHDK/FILM/LUTS' / f'{name}.HWT').read_bytes()
    t = hwt[4000:]; assert t[:8] == b'HWTINT1\0'
    red, green, blue = struct.unpack('<3H', t[8:14])
    return red, green, blue, t[16:16 + 256]


def run(k, frame, w, h, tint, leaks, inplace=False, cache_size=8192):
    red, green, blue, highlight = tint
    u = Uc(UC_ARCH_ARM, UC_MODE_ARM); u.mem_map(0x100000, 0x600000)
    u.mem_map(0xff840000, 4096); u.mem_write(0xff840000, harness.rom[0x30000:0x31000])
    u.mem_write(IMAGE, frame); u.mem_write(0x200000, k['binary'])
    u.mem_write(TINT, struct.pack('<3HHI', red, green, blue, 0, HIGHLIGHT)); u.mem_write(HIGHLIGHT, highlight)
    if inplace: assert k['flag'], 'no rows_in_place in this build'; u.mem_write(k['flag'], struct.pack('<I', 1))
    u.reg_write(UC_ARM_REG_SP, STACK); u.reg_write(UC_ARM_REG_LR, RET)
    for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), (IMAGE, len(frame), w, h)): u.reg_write(reg, v)
    # stride, mono, level, tint, work, work_size, scratch, capacity, progress, vignette, leaks, seed, toy, toy_work, toy_size, print
    args = [w * 2, 0, 3, TINT, WORK, 0x40000, SCRATCH, SCRATCH_BYTES, 0, 0, leaks, 0x1234, 0, 0, 0, 0]
    u.mem_write(STACK, struct.pack('<16I', *args))
    addrs = [a for a, _ in k['syms']]; names = [n for _, n in k['syms']]
    fn = lambda a: names[bisect.bisect_right(addrs, a) - 1]
    cyc = collections.Counter(); ins = collections.Counter(); fills = collections.Counter()
    decoded = {}; last = [None]; cache = Cache(cache_size)

    def block(m, address, size, ctx):
        t = m.reg_read(UC_ARM_REG_CPSR) & 0x20
        key = (address, size, t)
        if key not in decoded:
            insns = list((thumb if t else arm).disasm(bytes(m.mem_read(address, size)), address))
            decoded[key] = (len(insns), _static_cycles(insns), fn(address))
        n, c, f = decoded[key]
        ins[f] += n; cyc[f] += c + 2  # +2: taken branch / refill at block end (ARM9E-S, no prediction)

    def mem(m, kind, address, size, value, ctx):
        if address >= 0x200000 and address < 0x380000: return  # code literals: I-side/literal pool, ignore
        before = cache.linefills + cache.writebacks
        cache.access(address, kind == 17, 'x')
        if cache.linefills + cache.writebacks != before: fills[fn(m.reg_read(UC_ARM_REG_PC))] += 1
    u.hook_add(UC_HOOK_BLOCK, block); u.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, mem)
    u.emu_start(k['entry'] | 0, RET, count=3_000_000_000)
    assert u.reg_read(UC_ARM_REG_PC) == RET and u.reg_read(UC_ARM_REG_R0) == 0, (hex(u.reg_read(UC_ARM_REG_PC)), u.reg_read(UC_ARM_REG_R0))
    return cyc, ins, fills, bytes(u.mem_read(IMAGE, len(frame)))


def bands(name, rows):
    if name == 'shot1':  # a camera JPEG graded F400H: a fixed band
        im = Image.open(FRAMES / 'shot-1.jpg')
        top = 1200
    else:                # 'highlights': the frame's brightest band
        im = Image.open(FRAMES / 'highlights.png')
        g = np.asarray(im.convert('L').resize((im.width // 16, im.height // 16)), float).mean(1)
        top = int(np.argmax(np.convolve(g, np.ones(12), 'valid'))) * 16
    im = im.convert('RGB')
    return [uyvy_from_image(im.crop((0, top, im.width, top + r))) for r in rows], im.width


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--look', default='F400H'); ap.add_argument('--leaks', type=int, default=1)
    ap.add_argument('--rows', default='64,128'); ap.add_argument('--fixtures', default='shot1,highlights'); ap.add_argument('--inplace', action='store_true')
    ap.add_argument('--compare', help='reference camera/ folder: ARM outputs must match it byte for byte')
    a = ap.parse_args(); rows = [int(r) for r in a.rows.split(',')]
    k = build(); tint = look_tint(a.look)
    for fx in a.fixtures.split(','):
        frames, w = bands(fx, rows)
        res = [run(k, f, w, r, tint, a.leaks, a.inplace) for f, r in zip(frames, rows)]
        if a.compare:
            kr = build(a.compare, 'halation-ref')
            for f, r, mine in zip(frames, rows, res):
                theirs = run(kr, f, w, r, tint, a.leaks)[3]
                print(f'{fx} {r} rows: ARM output {"matches" if theirs == mine[3] else "DIFFERS from"} {a.compare}')
                assert theirs == mine[3]
        px = w * (rows[1] - rows[0])
        d = lambda i, f: (res[1][i][f] - res[0][i].get(f, 0)) / px
        funcs = set(res[1][0]) | set(res[1][2])
        tot = {f: d(0, f) + FILL_CYCLES * d(2, f) for f in funcs}
        total = sum(tot.values())
        print(f'\n{fx}: {a.look} halation High leaks={a.leaks}{" in place" if a.inplace else ""}, {total:.1f} modelled cycles/px '
              f'(~{total * 9e-9 * 3648 * 2736:.1f} s/frame at 9 ns/cycle)')
        print(f'  {"function":28s} {"cyc/px":>7s} {"share":>6s} {"instr/px":>8s} {"fills/kpx":>9s}')
        for f in sorted(funcs, key=lambda f: -tot[f])[:14]:
            print(f'  {f:28s} {tot[f]:7.2f} {tot[f] / total:6.1%} {d(1, f):8.2f} {1000 * d(2, f):9.2f}')


if __name__ == '__main__':
    main()
