#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""camera/film_reloc.c on the host: a synthetic ROM (no Canon code) with
every kind of instruction the relocator rewrites, broken copies the checker
must refuse, and, when a G12 1.00G ROM dump is at ZRNO_ROM or rom/PRIMARY.BIN,
the real develop task and its hash."""
from pathlib import Path
import os, random, re, struct, sys, tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import check_reloc as cr

LAB = cr.LAB
OUT, TRACE, OBSERVE = 0x07a00000, 0x07a10001, 0x07a20000


def synthetic(seed, extra=None):
    rnd = random.Random(seed)
    rom = bytearray(0xff8e8000 - cr.ROM_BASE)
    put = lambda a, w: struct.pack_into('<I', rom, a - cr.ROM_BASE, w & 0xffffffff)
    for a in range(0xff8e0000, 0xff8e8000, 4):   # literal pools and branch targets
        put(a, rnd.getrandbits(32))
    plain = [0xe1a01002, 0xe3530005, 0xe5904004, 0xe92d4010, 0xe8bd8010, 0x13a00001, 0xe0811002]
    for a in cr.ADDRS:
        k = rnd.randrange(6); k = 4 if k == 3 and rnd.random() < 0.85 else k; cond = rnd.choice([0xe, 0x0, 0x1, 0xa, 0xc])
        if k == 0:     # branch inside the copied code (either range)
            t = rnd.choice(cr.ADDRS)
            put(a, cond << 28 | (rnd.choice([0xa, 0xb]) << 24) | ((t - a - 8) >> 2) & 0xffffff)
        elif k == 1:   # branch out to the ROM
            t = rnd.choice([0xff810100, 0xff8e0400, 0xffa7e608, 0xff8e7400]) + 4 * rnd.randrange(8)
            put(a, cond << 28 | (rnd.choice([0xa, 0xb]) << 24) | ((t - a - 8) >> 2) & 0xffffff)
        elif k == 2:   # LDR rd,[pc,#+/-imm]
            # from a pool just past the range, as a compiler places it
            t = (0xff8e73d0 if a >= 0xff8e6f3c else 0xff8e60d8) + 4 * rnd.randrange(12)
            d = t - a - 8; rd = rnd.randrange(13)
            put(a, cond << 28 | 0x051f0000 | (1 << 23 if d >= 0 else 0) | rd << 12 | abs(d))
        elif k == 3:   # ADR (add/sub rd,pc,#imm) to an address outside
            rd = rnd.randrange(13); imm = 0x200 + 4 * rnd.randrange(30)
            imm8, rot = imm >> 2, 15   # imm8 ror 30 == imm8 << 2
            op = 4 if a > 0xff8e7000 or a < 0xff8e5f00 else 2
            if op == 4 and a + 8 + imm < 0xff8e73cc and a >= 0xff8e6f3c: op = 2
            if op == 2 and a - imm < 0xff8e60d4 and a >= 0xff8e5ec4 and a + 8 - imm >= 0xff8e5ec4: op = 4
            t = a + 8 + imm if op == 4 else a + 8 - imm
            if any(s <= t < e for s, e in cr.RANGES): put(a, 0xe1a00000); continue
            put(a, cond << 28 | 0x020f0000 | op << 21 | rd << 12 | rot << 8 | imm8)
        else:
            put(a, rnd.choice(plain))
    put(cr.TABLE_JUMP, 0xe1a00000)   # no table jump here
    for a, w in (extra or {}).items():
        put(a, w)
    return bytes(rom)


def build(rom, tmp, name):
    p = Path(tmp) / name
    p.write_bytes(rom)
    return cr.host_build(p, OUT, TRACE, OBSERVE, tmp)


def expect_refused(rom, words, label, why):
    try:
        cr.verify(rom, words, OUT, TRACE, OBSERVE, label)
    except AssertionError:
        return
    raise SystemExit('checker passed a broken copy: ' + why)


with tempfile.TemporaryDirectory() as tmp:
    for seed in range(6):
        rom = synthetic(seed)
        n, h, label, words = build(rom, tmp, 'syn%d.bin' % seed)
        assert n > 0, (seed, n)
        v, l = cr.verify(rom, words, OUT, TRACE, OBSERVE, label)
        assert h != 0xd12f82de
    # Broken copies: one word changed in a branch, a literal, a hook, a veneer.
    rom = synthetic(1)
    n, h, label, words = build(rom, tmp, 'syn.bin')
    rnd = random.Random(7)
    for k in range(40):
        bad = list(words); i = rnd.randrange(n)
        bad[i] ^= 1 << rnd.choice([0, 2, 5, 12, 20, 24, 28])
        expect_refused(rom, bad, label, 'word %d flipped' % i)
    # Refusals: a Thumb-switching BLX, and an ADR into the copied code.
    a = 0xff8e7000
    n, *_ = build(synthetic(2, {a: 0xfa000010}), tmp, 'blx.bin')
    assert n == -1, n
    n, *_ = build(synthetic(2, {a: 0xe28f0010}), tmp, 'adr.bin')   # add r0,pc,#16
    assert n == -2, n

    rom_path = LAB / os.environ.get('ZRNO_ROM', 'rom/PRIMARY.BIN')
    if rom_path.is_file():
        rom = rom_path.read_bytes()
        n, h, label, words = build(rom, tmp, 'g12.bin')
        want = int(re.search(r'FILM_RELOC_HASH (0x[0-9a-f]+)u', (LAB / 'camera/film_reloc.h').read_text()).group(1), 16)
        assert n > 0 and h == want, (n, hex(h))
        v, l = cr.verify(rom, words, OUT, TRACE, OBSERVE, label)
        print('G12 1.00G ROM: %d words, %d veneers, %d literals' % (n, len(v), len(l)))
    else:
        print('No ROM dump at', rom_path, '- checked the synthetic ROMs only')
print('film_reloc: all checks passed')
