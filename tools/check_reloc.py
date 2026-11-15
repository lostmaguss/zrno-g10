#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check Zrno's on-camera copy of Canon's develop task (camera/film_reloc.c)
against a ROM dump, instruction by instruction, decoding both with capstone.

  python3 tools/check_reloc.py ROM.BIN
      builds the copy on the host and checks it
  python3 tools/check_reloc.py ROM.BIN --camera COPY.BIN --elf main.elf
      also checks a copy read back from the camera against the host build
      for the same addresses, word for word
  python3 tools/check_reloc.py ROM.BIN --emulate --elf main.elf
      runs the build's own film_develop_relocate in an ARM emulator (needs
      unicorn) and checks its copy the same way

For a copy to pass, each ROM instruction must be in the copy, in order, with
Zrno's four hooks before the hooked ones. Branches must reach the copy of
their target, or a veneer to the ROM target. Each LDR from a literal pool,
and each ADR, must load the value the ROM's instruction loads. Every other
instruction must be the ROM's word unchanged. Needs capstone and a C
compiler. The ROM is only read, never written or copied anywhere.
"""
from pathlib import Path
import argparse, os, struct, subprocess, sys, tempfile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

LAB = Path(__file__).resolve().parents[1]
ROM_BASE = 0xff810000
RANGES = [(0xff8e6f3c, 0xff8e73cc), (0xff8e5ec4, 0xff8e60d4)]   # the task, then its step loop
HOOKS = {0xff8e6f6c: 0, 0xff8e7324: 1, 0xff8e7328: 2, 0xff8e6050: 3}
TABLE_JUMP = 0xff8e6fb8
SAVE = [0xe92d5fff, 0xe10f0000, 0xe24dd008, 0xe58d0000]
RESTORE = [0xe59d0000, 0xe28dd008, 0xe128f000, 0xe8bd5fff]
ADDRS = [a for s, e in RANGES for a in range(s, e, 4)]
md = Cs(CS_ARCH_ARM, CS_MODE_ARM); md.detail = True


def host_build(rom_path, out_addr, trace, observe, workdir):
    exe = Path(workdir) / 'reloc_host'
    subprocess.run(['cc', '-O2', '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-DFILM_RELOC_HOST',
                    '-fsanitize=address,undefined', '-I' + str(LAB / 'camera'),
                    str(LAB / 'tests/reloc_host.c'), str(LAB / 'camera/film_reloc.c'), '-o', str(exe)], check=True)
    out = Path(workdir) / 'copy.bin'
    subprocess.run([str(exe), str(rom_path), str(out), hex(out_addr), hex(trace), hex(observe)], check=True)
    b = out.read_bytes()
    n, h = struct.unpack_from('<iI', b)
    nmap = len(ADDRS) + (len(ADDRS) & 1)
    m = list(struct.unpack_from('<%dH' % len(ADDRS), b, 8))
    words = list(struct.unpack_from('<%dI' % max(n, 0), b, 8 + 2 * nmap))
    return n, h, m, words


def decode(word, addr):
    ins = list(md.disasm(struct.pack('<I', word), addr))
    return ins[0] if ins else None


def verify(rom, words, out_addr, trace, observe, label):
    """Raises AssertionError on the first difference; returns (veneers, literals)."""
    word_at = lambda a: struct.unpack_from('<I', rom, a - ROM_BASE)[0]
    def copy_at(a):
        assert out_addr <= a < out_addr + 4 * len(words) and a % 4 == 0, f'{a:08x} is outside the copy'
        return words[(a - out_addr) // 4]
    inside = lambda t: any(s <= t < e for s, e in RANGES)
    where = {a: out_addr + 4 * label[i] for i, a in enumerate(ADDRS)}
    veneers, literals = set(), set()

    def loaded(ins):
        """Value an LDR rd,[pc,#imm] in the copy loads, or None."""
        if ins is None or ins.mnemonic.split('.')[0][:3] != 'ldr' or ins.mnemonic[3:4] in ('b', 'h', 'd', 's'):
            return None
        mem = ins.operands[1].mem
        if ins.operands[1].type != 3 or mem.base != 11 or mem.index != 0:   # ARM_OP_MEM, ARM_REG_PC
            return None
        t = ins.address + 8 + mem.disp
        literals.add(t)
        return copy_at(t)

    expect_pos = 0
    for i, a in enumerate(ADDRS):
        pos = label[i]
        assert pos == expect_pos, f'{a:08x}: copy is not contiguous'
        p = out_addr + 4 * pos
        if a in HOOKS:
            k = HOOKS[a]
            got = [copy_at(p + 4 * j) for j in range(11)]
            middle = 0xe59d005c if k == 0 else 0xe3a00000 | k
            assert got[:4] == SAVE and got[4] == middle and got[6] == 0xe12fff3c and got[7:] == RESTORE, f'{a:08x}: hook differs'
            assert loaded(decode(got[5], p + 20)) == (trace if k == 0 else observe), f'{a:08x}: hook calls the wrong function'
            p += 44
        expect_pos = (p - out_addr) // 4 + 1
        w = word_at(a); new = copy_at(p)
        orig, ins = decode(w, a), decode(new, p)
        assert orig is not None, f'{a:08x}: ROM word does not decode'
        if (w & 0x0e000000) == 0x0a000000:
            assert w >> 28 != 15, f'{a:08x}: BLX immediate'
            assert ins is not None and ins.mnemonic == orig.mnemonic, f'{a:08x}: {orig.mnemonic} became {ins and ins.mnemonic}'
            t, nt = orig.operands[0].imm & 0xffffffff, ins.operands[0].imm & 0xffffffff
            if inside(t):
                assert nt == where[t], f'{a:08x}: branch to {t:08x} goes to {nt:08x}'
            else:
                assert copy_at(nt) == 0xe51ff004 and copy_at(nt + 4) == t, f'{a:08x}: no veneer to {t:08x}'
                veneers.add(nt)
        elif (w & 0x0f7f0000) == 0x051f0000:
            t = a + 8 + ((w & 4095) if w & (1 << 23) else -(w & 4095))
            assert ins.mnemonic == orig.mnemonic and ins.operands[0].reg == orig.operands[0].reg, f'{a:08x}: LDR changed'
            assert loaded(ins) == word_at(t), f'{a:08x}: LDR loads {loaded(ins)} not {word_at(t):08x}'
        elif (w & 0x0e0f0000) == 0x020f0000 and ((w >> 21) & 15) in (2, 4):
            imm = w & 255; rot = ((w >> 8) & 15) * 2
            imm = ((imm >> rot) | (imm << (32 - rot))) & 0xffffffff if rot else imm
            t = (a + 8 + (imm if ((w >> 21) & 15) == 4 else -imm)) & 0xffffffff
            assert not inside(t)
            cond = orig.mnemonic[3:5] if orig.mnemonic[:3] in ('add', 'sub', 'adr') else ''
            assert ins.mnemonic == 'ldr' + cond and ins.operands[0].reg == orig.operands[0].reg, f'{a:08x}: ADR changed'
            assert loaded(ins) == t, f'{a:08x}: ADR loads the wrong address'
        else:
            assert new == w, f'{a:08x}: {w:08x} became {new:08x}'
            if 'pc' in orig.op_str:
                assert a == TABLE_JUMP or orig.mnemonic.startswith(('pop', 'ldm')), f'{a:08x}: unhandled PC use {orig.mnemonic} {orig.op_str}'
    # The table jump's table runs on from it, with no hook inside.
    if any(a == TABLE_JUMP for a in ADDRS):
        cmp = decode(word_at(TABLE_JUMP - 4), TABLE_JUMP - 4)
        if cmp is not None and cmp.mnemonic == 'cmp':
            entries = cmp.operands[1].imm + 1
            for a in range(TABLE_JUMP, TABLE_JUMP + 4 * (entries + 2), 4):
                assert a not in HOOKS, f'hook {a:08x} inside the table'
    return veneers, literals


def symbols(elf):
    """Symbol values from readelf, which keeps the Thumb bit that nm drops."""
    readelf = os.environ.get('CROSS_COMPILE', 'arm-none-eabi-') + 'readelf'
    out = subprocess.run([readelf, '-sW', str(elf)], capture_output=True, text=True, check=True).stdout
    sym = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 8 and parts[0].endswith(':') and parts[3] in ('FUNC', 'OBJECT', 'NOTYPE'):
            sym.setdefault(parts[7], int(parts[1], 16))
    return sym


def load_segments(elf):
    """(address, bytes) of each loadable segment of a 32-bit little-endian ELF."""
    b = Path(elf).read_bytes()
    phoff, = struct.unpack_from('<I', b, 28); phentsize, phnum = struct.unpack_from('<HH', b, 42)
    segs = []
    for i in range(phnum):
        typ, off, vaddr, _, filesz, memsz = struct.unpack_from('<6I', b, phoff + i * phentsize)
        if typ == 1 and memsz:
            segs.append((vaddr, b[off:off + filesz] + bytes(memsz - filesz)))
    return segs


def emulate(rom, elf, sym):
    """Runs the build's film_develop_relocate on the ROM; returns the copy's words."""
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x07900000, 0x100000)   # the core, in EXMEM from MEMISOSTART 0x079089e0
    for addr, data in load_segments(elf):
        assert 0x07900000 <= addr and addr + len(data) <= 0x07a00000, hex(addr)
        uc.mem_write(addr, data)
    uc.mem_map(ROM_BASE, 0x800000)
    uc.mem_write(ROM_BASE, rom[:0x7f0000])
    uc.mem_map(0x01000000, 0x10000)   # stack
    uc.reg_write(UC_ARM_REG_SP, 0x0100f000)
    stop = 0x01000000
    uc.mem_write(stop, struct.pack('<I', 0xe1a00000))
    # Cache maintenance has nothing to do here: each becomes bx lr, and is counted.
    called = set()
    for name in ('dcache_clean_all', 'icache_flush_all'):
        assert not sym[name] & 1, name + ' is not ARM code'
        uc.mem_write(sym[name], struct.pack('<I', 0xe12fff1e))
        uc.hook_add(UC_HOOK_CODE, lambda uc, addr, size, _: called.add(addr), begin=sym[name], end=sym[name])
    uc.reg_write(UC_ARM_REG_LR, stop)
    uc.emu_start(sym['film_develop_relocate'] | 1, stop, count=2_000_000)
    entry = uc.reg_read(UC_ARM_REG_R0)
    status = struct.unpack('<i', uc.mem_read(sym['film_reloc_status'], 4))[0]
    assert status == 1 and entry == sym['develop_copy'], f'film_develop_relocate refused: status {status}'
    assert len(called) == 2, 'cache maintenance not reached'
    return bytes(uc.mem_read(sym['develop_copy'], 4096))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom')
    ap.add_argument('--camera', help='the copy read back from the camera')
    ap.add_argument('--emulate', action='store_true', help="run the build's relocator in an ARM emulator")
    ap.add_argument('--elf', help='main.elf of the build (platform/g12/sub/100g/main.elf)')
    a = ap.parse_args()
    rom = Path(a.rom).read_bytes()
    if a.camera or a.emulate:
        if not a.elf: sys.exit('--camera and --emulate need --elf')
        s = symbols(a.elf)
        out_addr, trace, observe = s['develop_copy'], s['film_develop_trace'], s['film_stage_observe']
    else:
        out_addr, trace, observe = 0x07a00000, 0x07a10001, 0x07a20001
    with tempfile.TemporaryDirectory() as tmp:
        n, h, label, words = host_build(a.rom, out_addr, trace, observe, tmp)
    if n <= 0: sys.exit(f'film_reloc_build refused this ROM ({n})')
    v, l = verify(rom, words, out_addr, trace, observe, label)
    print(f'Host copy: {n} words, {len(v)} veneers, {len(l)} literals, ROM hash 0x{h:08x}: matches the ROM')
    if a.camera or a.emulate:
        cam = Path(a.camera).read_bytes() if a.camera else emulate(rom, a.elf, symbols(a.elf))
        cam = list(struct.unpack_from('<%dI' % n, cam))
        diff = [i for i in range(n) if cam[i] != words[i]]
        if diff: sys.exit(f'Camera copy differs from the host build at {len(diff)} words, first at word {diff[0]}')
        print(f'{"Camera" if a.camera else "Emulated"} copy at 0x{out_addr:08x}: {n} words, identical to the host build')


if __name__ == '__main__':
    main()
