# SPDX-License-Identifier: GPL-2.0-or-later
"""Profile Bloom (film_uyvy_bloom) by function, like profile_halation.py.

Compiles camera/film_bloom.c as the core does (-O2 ARM), runs it in Unicorn
on two full-width bands of a graded frame and attributes modelled cycles,
instructions and D-cache line fills to functions. The frame is modelled as
cacheable (FRAMECACHE). Steady state is the difference between the two
bands, per pixel. Treat the result as proportional.

Run from the repository root:
  work/test-venv/bin/python tools/speed_model/profile_bloom.py [--level 3] [--src DIR]
"""
import sys, argparse, bisect, collections, struct, subprocess
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import harness
from harness import IMAGE, WORK, STACK, RET, Cache, _static_cycles, prefix, lab
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_BLOCK, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
import capstone as cs
from profile_halation import bands, FILL_CYCLES

arm = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_ARM)
thumb = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_THUMB); arm.detail = thumb.detail = True


def build(src_dir=None, tag='bloom-prof'):
    src = Path(src_dir or lab / 'camera') / 'film_bloom.c'
    elf = harness.build / f'{tag}.elf'; binary = harness.build / f'{tag}.bin'
    subprocess.run([prefix + 'gcc', '-O2', '-g', '-finline', '-marm', '-mcpu=arm946e-s', '-fno-builtin', '-nostdlib',
                    '-Wl,-Ttext=0x200000', '-Wl,--entry=film_uyvy_bloom', str(src), str(harness.support), '-lgcc', '-o', str(elf)],
                   check=True)
    subprocess.run([prefix + 'objcopy', '-O', 'binary', str(elf), str(binary)], check=True)
    syms = []; entry = None; fade = None; fast = None
    for line in subprocess.check_output([prefix + 'readelf', '-sW', str(elf)], text=True).splitlines():
        p = line.split()
        if len(p) >= 8 and p[3] == 'FUNC':
            v = int(p[1], 16); syms.append((v & ~1, p[7]))
            if p[7] == 'film_uyvy_bloom': entry = v
            if p[7] == 'film_bloom_fade_table': fade = v
            if p[7] == 'film_bloom_fast': fast = v
    syms.sort()
    return dict(binary=binary.read_bytes(), syms=syms, entry=entry, fade=fade, fast=fast)


def work_size(w):
    grid = (w + 3) // 4
    return ((grid * 50 + 3) & ~3) + grid * 17


FADE_TABLE = 0x480000   # 64 KB, cacheable (EXMEM on the camera)


def run(k, frame, w, h, level, fade=False, fast=False):
    u = Uc(UC_ARCH_ARM, UC_MODE_ARM); u.mem_map(0x100000, 0x600000)
    u.mem_map(0xff840000, 4096); u.mem_write(0xff840000, harness.rom[0x30000:0x31000])   # as profile_halation
    u.mem_write(IMAGE, frame); u.mem_write(0x200000, k['binary'])
    if fade:   # build and install the table before profiling starts
        assert k['fade'], 'no film_bloom_fade_table in this build'
        u.reg_write(UC_ARM_REG_SP, STACK); u.reg_write(UC_ARM_REG_LR, RET); u.reg_write(UC_ARM_REG_R0, FADE_TABLE)
        u.emu_start(k['fade'], RET)
    if fast:   # fast Bloom (film_bloom_fast(1))
        assert k['fast'], 'no film_bloom_fast in this build'
        u.reg_write(UC_ARM_REG_SP, STACK); u.reg_write(UC_ARM_REG_LR, RET); u.reg_write(UC_ARM_REG_R0, 1)
        u.emu_start(k['fast'], RET)
    u.reg_write(UC_ARM_REG_SP, STACK); u.reg_write(UC_ARM_REG_LR, RET)
    for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), (IMAGE, len(frame), w, h)): u.reg_write(reg, v)
    u.mem_write(STACK, struct.pack('<5I', w * 2, level, WORK, work_size(w), 0))
    addrs = [a for a, _ in k['syms']]; names = [n for _, n in k['syms']]
    fn = lambda a: names[bisect.bisect_right(addrs, a) - 1]
    cyc = collections.Counter(); ins = collections.Counter(); fills = collections.Counter(); decoded = {}; cache = Cache(8192)

    def block(m, address, size, ctx):
        t = m.reg_read(UC_ARM_REG_CPSR) & 0x20; key = (address, size, t)
        if key not in decoded:
            insns = list((thumb if t else arm).disasm(bytes(m.mem_read(address, size)), address))
            decoded[key] = (len(insns), _static_cycles(insns), fn(address))
        n, c, f = decoded[key]; ins[f] += n; cyc[f] += c + 2

    def mem(m, kind, address, size, value, ctx):
        if 0x200000 <= address < 0x380000: return
        before = cache.linefills + cache.writebacks
        cache.access(address, kind == 17, 'x')
        if cache.linefills + cache.writebacks != before: fills[fn(m.reg_read(UC_ARM_REG_PC))] += 1
    u.hook_add(UC_HOOK_BLOCK, block); u.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, mem)
    u.emu_start(k['entry'], RET, count=3_000_000_000)
    assert u.reg_read(UC_ARM_REG_PC) == RET and u.reg_read(UC_ARM_REG_R0) == 0
    return cyc, ins, fills, bytes(u.mem_read(IMAGE, len(frame)))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--level', type=int, default=3); ap.add_argument('--rows', default='64,128')
    ap.add_argument('--fixtures', default='shot1,highlights'); ap.add_argument('--src'); ap.add_argument('--compare')
    ap.add_argument('--fade', action='store_true', help='install the chroma fade table (film_bloom_fade_table)')
    ap.add_argument('--fast', action='store_true', help='fast Bloom (film_bloom_fast): approximate, no --compare')
    a = ap.parse_args(); rows = [int(r) for r in a.rows.split(',')]
    k = build(a.src)
    kr = build(a.compare, 'bloom-ref') if a.compare else None
    for fx in a.fixtures.split(','):
        frames, w = bands(fx, rows)
        res = [run(k, f, w, r, a.level, a.fade, a.fast) for f, r in zip(frames, rows)]
        if kr:
            for f, r, mine in zip(frames, rows, res):
                same = run(kr, f, w, r, a.level, a.fade and kr['fade'] is not None)[3] == mine[3]
                print(f'{fx} {r} rows: ARM output {"matches" if same else "DIFFERS from"} {a.compare}'); assert same
        px = w * (rows[1] - rows[0])
        d = lambda i, f: (res[1][i][f] - res[0][i].get(f, 0)) / px
        funcs = set(res[1][0]) | set(res[1][2])
        tot = {f: d(0, f) + FILL_CYCLES * d(2, f) for f in funcs}; total = sum(tot.values())
        print(f'\n{fx}: Bloom level {a.level}{" with the fade table" if a.fade else ""}{" FAST" if a.fast else ""}, {total:.1f} modelled cycles/px (~{total * 9e-9 * 3648 * 2736:.1f} s/frame at 9 ns/cycle)')
        print(f'  {"function":24s} {"cyc/px":>7s} {"share":>6s} {"instr/px":>8s} {"fills/kpx":>9s}')
        for f in sorted(funcs, key=lambda f: -tot[f])[:10]:
            print(f'  {f:24s} {tot[f]:7.2f} {tot[f] / total:6.1%} {d(1, f):8.2f} {1000 * d(2, f):9.2f}')


if __name__ == '__main__':
    main()
