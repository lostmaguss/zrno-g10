#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the G10 develop-task copy (tests/g10_reloc_host.c output) against the
ROM, instruction by instruction: every branch reaches the same place (inside
the copy, or through a veneer to the same ROM address), every PC-relative
load gives the same value, everything else is copied unchanged, and no other
PC-relative instruction is in the copied range.

Usage: tools/g10_reloc_check.py ROM COPY.bin MAP.bin
"""
import struct, sys
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

ROM_BASE, OUT = 0xff810000, 0x00100000
TASK = (0xff8b1c8c, 0xff8b1f14)
STEPS = (0xff8b153c, 0xff8b164c)
HOOKS = {0xff8b1cc0: 0, 0xff8b1edc: 1, 0xff8b1ee0: 2, 0xff8b1620: 3}
HOOK_WORDS = 11
TRACE_FN, OBSERVE_FN = 0x00200001, 0x00300001

rom = open(sys.argv[1], 'rb').read()
copy = open(sys.argv[2], 'rb').read()
cmap = struct.unpack(f'<{len(open(sys.argv[3], "rb").read()) // 2}H', open(sys.argv[3], 'rb').read())
R = lambda a: struct.unpack_from('<I', rom, a - ROM_BASE)[0]
C = lambda i: struct.unpack_from('<I', copy, 4 * i)[0]
addrs = list(range(*TASK, 4)) + list(range(*STEPS, 4))
index = {a: i for i, a in enumerate(addrs)}
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
errors = []


def ins_pos(i):
    return cmap[i] + (HOOK_WORDS if addrs[i] in HOOKS else 0)


def branch_target(w, at):
    off = w & 0xffffff
    if off & 0x800000: off -= 1 << 24
    return (at + 8 + 4 * off) & 0xffffffff


def literal(w, at):
    off = w & 0xfff
    return at + 8 + (off if w & (1 << 23) else -off)


for i, a in enumerate(addrs):
    w, p = R(a), ins_pos(i)
    cw, cat = C(p), OUT + 4 * p
    if a in HOOKS:
        h = cmap[i]
        k = HOOKS[a]
        load = C(h + 4)
        want = 0xe59d0058 if k == 0 else 0xe3a00000 | k
        fn = R  # placeholder
        if load != want: errors.append(f'{a:08x}: hook load {load:08x} != {want:08x}')
        lv = struct.unpack_from('<I', copy, literal(C(h + 5), OUT + 4 * (h + 5)) - OUT)[0]
        if lv != (TRACE_FN if k == 0 else OBSERVE_FN): errors.append(f'{a:08x}: hook calls {lv:08x}')
    if (w & 0x0e000000) == 0x0a000000:
        t = branch_target(w, a)
        ct = branch_target(cw, cat)
        if (cw & 0xff000000) != (w & 0xff000000): errors.append(f'{a:08x}: branch kind changed')
        if t in index:
            if ct != OUT + 4 * cmap[index[t]]: errors.append(f'{a:08x}: internal branch to {ct:08x}, want copy of {t:08x}')
        else:
            v0, v1 = struct.unpack_from('<II', copy, ct - OUT)
            if v0 != 0xe51ff004 or v1 != t: errors.append(f'{a:08x}: veneer {v0:08x} {v1:08x} != ldr pc / {t:08x}')
    elif (w & 0x0f7f0000) == 0x051f0000:
        v = R(literal(w, a))
        if (cw & 0x0f7f0000) != 0x051f0000 or (cw & 0xf000f000) != (w & 0xf000f000):
            errors.append(f'{a:08x}: literal load changed form')
        cv = struct.unpack_from('<I', copy, literal(cw, cat) - OUT)[0]
        if cv != v: errors.append(f'{a:08x}: literal {cv:08x} != {v:08x}')
    elif (w & 0x0e0f0000) == 0x020f0000 and ((w >> 21) & 15) in (2, 4):
        ins = next(md.disasm(struct.pack('<I', w), a))
        imm = int(ins.op_str.split('#')[-1], 16)
        t = a + 8 + imm if ((w >> 21) & 15) == 4 else a + 8 - imm
        cv = struct.unpack_from('<I', copy, literal(cw, cat) - OUT)[0]
        if cv != t: errors.append(f'{a:08x}: adr {cv:08x} != {t:08x}')
    else:
        if cw != w: errors.append(f'{a:08x}: changed {w:08x} -> {cw:08x}')
        ins = next(md.disasm(struct.pack('<I', w), a), None)
        if ins is None: errors.append(f'{a:08x}: not an instruction ({w:08x})')
        elif 'pc' in ins.op_str and not (ins.mnemonic.startswith(('pop', 'ldm')) and ins.op_str.endswith('pc}')) \
                and not (ins.mnemonic.startswith('add') and ins.op_str.startswith('pc, pc, r')):
            errors.append(f'{a:08x}: other PC use: {ins.mnemonic} {ins.op_str}')

# Table jumps must be followed by their table with nothing in between.
for tj in (0xff8b1d18, 0xff8b1560):
    n = 0
    while (R(tj + 4 + 4 * n) & 0x0f000000) == 0x0a000000: n += 1
    for k in range(n + 1):
        if ins_pos(index[tj]) + 1 + k != ins_pos(index[tj + 4 + 4 * k]):
            errors.append(f'table jump {tj:08x}: entry {k} moved')
    print(f'table jump {tj:08x}: {n} entries in place')

print(f'{len(addrs)} instructions checked, {len(errors)} problem(s)')
for e in errors: print('  ' + e)
sys.exit(1 if errors else 0)
