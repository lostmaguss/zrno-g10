# SPDX-License-Identifier: GPL-2.0-or-later
"""ARM946E-S emulator harness for exploring Zrno kernel speedups.

Compiles a kernel source with the pinned GCC 10.3.1, runs it in Unicorn on a
full-width UYVY band cut from a real G12 JPEG, and reports:
  - executed ARM instructions
  - output bytes (for exactness checks against the reference build)
  - optional: a simple D-cache model (4-way LRU, 32-byte lines, read-allocate)
    for cacheable accesses, plus a count of uncached image-buffer accesses
  - optional: per-source-line instruction profile via addr2line
Run from the repository root with work/test-venv/bin/python.
"""
from pathlib import Path
import struct, subprocess, collections
import numpy as np
from PIL import Image
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_BLOCK, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

import os
here = Path(__file__).resolve().parent
lab = here.parents[1]


def _workspace():
    # The pinned compiler, ROM image and fixtures live in the private workspace,
    # not in git. Set G12_WORKSPACE, or run from a checkout inside it.
    if os.environ.get('G12_WORKSPACE'): return Path(os.environ['G12_WORKSPACE'])
    for p in lab.parents:
        if (p / 'work/xpack-arm-none-eabi-gcc-10.3.1-2.3').is_dir(): return p
    raise SystemExit('Set G12_WORKSPACE to the folder containing work/ and outputs/')


root = _workspace()
# Test frames: full-size G12 shots of your own, not in git (see README.md).
FRAMES = Path(os.environ.get('G12_FRAMES', root / 'work/frames'))
build = root / 'work/explore-speed/build'; build.mkdir(parents=True, exist_ok=True)
prefix = str(root / 'work/xpack-arm-none-eabi-gcc-10.3.1-2.3/bin/arm-none-eabi-')
rom = (root / 'research/digic/rom/PRIMARY-FULL.BIN').read_bytes()
support = build / 'mem.c'
support.write_text(
    "void *memset(void *p,int v,unsigned n){unsigned char *q=p;while(n--)*q++=v;return p;}\n"
    "int memcmp(const void *a,const void *b,unsigned n){const unsigned char *p=a,*q=b;while(n--){if(*p!=*q)return *p-*q;p++;q++;}return 0;}\n"
    # Canon's aligned word copy from the verified ROM, as used on camera.
    "void *memcpy(void *d,const void *s,unsigned n){return ((void *(*)(void *,const void *,unsigned))0xff840674)(d,s,n);}\n")

IMAGE, CODE, LUT, SCRATCH, WORK, STACK, RET = 0x100000, 0x200000, 0x380000, 0x3c0000, 0x400000, 0x5f0000, 0x680000
SCRATCH_BYTES = 16384


def extra_sources(source):
    # film_colour.c calls the retro effects when a sibling film_retro.c exists.
    if Path(source).name != 'film_colour.c': return []
    return [str(p) for p in (Path(source).with_name('film_retro.c'), Path(source).with_name('film_toy.c'), Path(source).with_name('film_print.c')) if p.exists()]


def compile_arm(source, entry, tag, defines=()):
    elf = build / f'{tag}.elf'; binary = build / f'{tag}.bin'
    subprocess.run([prefix + 'gcc', '-O2', '-g', '-finline', '-fno-strict-aliasing', '-fno-schedule-insns2', '-marm',
                    '-mcpu=arm946e-s', '-fno-builtin', '-nostdlib', '-Wl,-Ttext=0x200000', '-Wl,--entry=' + entry,
                    *[f'-D{d}' for d in defines], str(source), *extra_sources(source), str(support), '-lgcc', '-o', str(elf)], check=True)
    subprocess.run([prefix + 'objcopy', '-O', 'binary', str(elf), str(binary)], check=True)
    nm = subprocess.check_output([prefix + 'nm', '-n', str(elf)], text=True)
    symbols = {p[2]: int(p[0], 16) for line in nm.splitlines() if len(p := line.split()) == 3}
    size = subprocess.check_output([prefix + 'size', str(elf)], text=True).splitlines()[1].split()
    return dict(binary=binary.read_bytes(), entry=symbols[entry], elf=elf, text=int(size[0]))


class Cache:
    def __init__(self, size, ways=4, line=32):
        self.sets = size // (ways * line); self.ways = ways; self.line = line
        self.tags = [collections.OrderedDict() for _ in range(self.sets)]
        self.misses = collections.Counter(); self.accesses = collections.Counter()
        self.linefills = 0; self.writebacks = 0; self.write_misses = 0

    def access(self, address, write, region):
        self.accesses[region] += 1
        lineno = address // self.line; s = self.tags[lineno % self.sets]
        if lineno in s:
            s.move_to_end(lineno)
            if write: s[lineno] = True  # write-back: mark dirty
            return
        if write:  # ARM946E-S: write misses do not allocate; they go to the write buffer.
            self.write_misses += 1; return
        self.misses[region] += 1; self.linefills += 1
        s[lineno] = False
        if len(s) > self.ways:
            _, dirty = s.popitem(last=False)
            self.writebacks += dirty


def run(kernel, frame, stack_args, lut=b'', regs=None, cache_sizes=(), profile=False):
    w, h = regs[2], regs[3]
    u = Uc(UC_ARCH_ARM, UC_MODE_ARM); u.mem_map(0x100000, 0x600000)
    u.mem_map(0xff840000, 4096); u.mem_write(0xff840000, rom[0x30000:0x31000])
    u.mem_write(IMAGE, frame); u.mem_write(CODE, kernel['binary'])
    if lut: u.mem_write(LUT, lut)
    u.reg_write(UC_ARM_REG_SP, STACK); u.reg_write(UC_ARM_REG_LR, RET)
    for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), regs):
        u.reg_write(reg, value)
    u.mem_write(STACK, struct.pack('<' + 'I' * len(stack_args), *stack_args))
    counts = [0]; blocks = collections.Counter(); cycles = [0]; last = [None]

    def block(m, address, size, ctx):
        counts[0] += size // 4
        if profile: blocks[address, size] += 1
        prev = last[0]
        if prev is not None:
            pa, ps, pc_ = prev
            cycles[0] += pc_
            if address != pa + ps and block_ends_in_branch(kernel, m, pa, ps):
                cycles[0] += 2  # taken branch / PC write refill (ARM9E-S has no prediction)
        last[0] = (address, size, block_cycles(kernel, m, address, size))
    u.hook_add(UC_HOOK_BLOCK, block)
    caches = [Cache(s) for s in cache_sizes]
    uncached = [0]
    if caches:
        def region(a):
            if IMAGE <= a < IMAGE + len(frame): return 'image'
            if CODE <= a < LUT: return 'code_data'
            if LUT <= a < SCRATCH: return 'lut'
            if SCRATCH <= a < SCRATCH + SCRATCH_BYTES: return 'scratch'
            if WORK <= a < WORK + 0x80000: return 'work'
            if a >= 0x500000: return 'stack'
            return 'other'

        def mem(m, kind, address, size, value, ctx):
            r = region(address)
            if r == 'image': uncached[0] += 1; return
            for c in caches: c.access(address, kind == 17, r)  # 17 == UC_MEM_WRITE
        u.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, mem)
    u.emu_start(kernel['entry'], RET, count=2_000_000_000)
    assert u.reg_read(UC_ARM_REG_PC) == RET, hex(u.reg_read(UC_ARM_REG_PC))
    rc = u.reg_read(UC_ARM_REG_R0)
    assert rc == 0, rc
    cycles[0] += last[0][2] + 2
    out = dict(image=bytes(u.mem_read(IMAGE, len(frame))), instructions=counts[0], uncached=uncached[0],
               cycles=cycles[0],
               caches={s: dict(misses=sum(c.misses.values()), by_region=dict(c.misses),
                               accesses=sum(c.accesses.values()), linefills=c.linefills,
                               writebacks=c.writebacks, write_misses=c.write_misses)
                       for s, c in zip(cache_sizes, caches)})
    if profile:
        out['profile'] = line_profile(kernel, blocks)
    return out


import capstone as _cs
_md = _cs.Cs(_cs.CS_ARCH_ARM, _cs.CS_MODE_ARM); _md.detail = True
_block_cache = {}
_MUL1 = {'smulbb', 'smulbt', 'smultb', 'smultt', 'smlabb', 'smlabt', 'smlatb', 'smlatt', 'smulwb', 'smulwt',
         'smlawb', 'smlawt', 'qadd', 'qsub', 'qdadd', 'qdsub'}
_MUL2 = {'mul', 'mla'}
_MUL3 = {'umull', 'smull', 'umlal', 'smlal', 'smlalbb', 'smlalbt', 'smlaltb', 'smlaltt'}


def _decode(kernel, m, address, size):
    key = (id(kernel), address, size)
    if key not in _block_cache:
        insns = list(_md.disasm(bytes(m.mem_read(address, size)), address))
        _block_cache[key] = insns, _static_cycles(insns)
    return _block_cache[key]


def _static_cycles(insns):
    """Approximate ARM9E-S issue cycles: see ARM946E-S TRM, chapter on instruction timing.
    Assumes cache hits; memory stalls are modelled separately by the cache model."""
    total = 0
    for i, ins in enumerate(insns):
        mn = ins.mnemonic.split('.')[0]
        base = mn[:-2] if len(mn) > 3 and mn[-2:] in _CONDS else mn
        reads, writes = ins.regs_access()
        c = 1; latency = 0
        if base.startswith('ldm') or base.startswith('stm') or base in ('push', 'pop'):
            n = max(1, len(ins.operands) - (1 if base[:3] in ('ldm', 'stm') else 0))
            c = max(2, n)
        elif base in ('ldrd', 'strd'):
            c = 2; latency = 1
        elif base.startswith('ldr'):
            latency = 2 if base in ('ldrb', 'ldrh', 'ldrsb', 'ldrsh') else 1
        elif base in _MUL1:
            latency = 1
        elif base.rstrip('s') in _MUL2:
            c = 4 if base.endswith('s') and base != 'mla' else 2; latency = 1
        elif base.rstrip('s') in _MUL3:
            c = 3; latency = 1
        else:
            if any(op.type == _cs.arm.ARM_OP_REG and op.shift.type >= _cs.arm.ARM_SFT_ASR_REG for op in ins.operands):
                c = 2
        total += c
        if latency and i + 1 < len(insns):
            nxt_reads = insns[i + 1].regs_access()[0]
            if set(writes) & set(nxt_reads): total += latency
            elif latency == 2 and i + 2 < len(insns) and set(writes) & set(insns[i + 2].regs_access()[0]):
                total += 1
    return total


_CONDS = {'eq', 'ne', 'cs', 'hs', 'cc', 'lo', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le'}


def block_cycles(kernel, m, address, size):
    return _decode(kernel, m, address, size)[1]


def block_ends_in_branch(kernel, m, address, size):
    return True


def line_profile(kernel, blocks):
    per_addr = collections.Counter()
    for (address, size), n in blocks.items():
        for a in range(address, address + size, 4): per_addr[a] += n
    addrs = sorted(per_addr)
    lines = subprocess.check_output([prefix + 'addr2line', '-e', str(kernel['elf']), *[hex(a) for a in addrs]],
                                    text=True).splitlines()
    total = collections.Counter()
    for a, line in zip(addrs, lines):
        total[line.rsplit('/', 1)[-1].split(' ')[0]] += per_addr[a]
    return total


def uyvy_from_image(im):
    a = np.asarray(im.convert('YCbCr'), dtype=np.int32); h, w = a.shape[:2]
    packed = np.empty((h, w * 2), np.uint8); packed[:, 1::2] = a[:, :, 0]
    packed[:, 0::4] = ((a[:, 0::2, 1] + a[:, 1::2, 1] + 1) // 2 - 128) % 256
    packed[:, 2::4] = ((a[:, 0::2, 2] + a[:, 1::2, 2] + 1) // 2 - 128) % 256
    return packed.tobytes()


CAMERA_SAMPLES = sorted((root / 'research/digic/full-lut-test-1/samples')
                        .glob('*phase1*.BIN'))


def band(rows=16, top=None, path=FRAMES / 'smooth.jpg', fixture='jpeg'):
    """fixture='jpeg': centre band of a decoded G12 JPEG with smooth tones
    (many repeated pairs), FRAMES/smooth.jpg.
    fixture='camera': real pre-encoder UYVY captured on the G12, 4 KiB samples tiled
    with a per-row offset so rows differ (sensor noise retained)."""
    if fixture == 'camera':
        width = 3648; data = b''.join(p.read_bytes() for p in CAMERA_SAMPLES)
        out = bytearray()
        for r in range(rows):
            off = (r * 1828) % len(data) & ~3
            row = (data[off:] + data) * 2
            out += row[:width * 2]
        return bytes(out), width, rows
    with Image.open(path) as im:
        width, height = im.size
        top = height // 2 - rows // 2 if top is None else top
        return uyvy_from_image(im.crop((0, top, width, top + rows))), width, rows


def lut_bytes(name):
    return (lab / 'card/CHDK/FILM/LUTS' / name).read_bytes()


def colour_args(entry, w, h, halo_level=3, scratch=True):
    """Stack arguments beyond r0-r3 for the two colour entry points."""
    pitch = w * 2
    s = [SCRATCH, SCRATCH_BYTES, 0] if scratch else [0, 0, 0]
    if entry == 'film_uyvy_map_cached':
        return [pitch, 0, 0, w, h, LUT] + s
    return [pitch, LUT, halo_level, WORK, 200000] + s
