#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare develop register snapshots from tools/probes/dg_regs_probe.c.

Each DGREGn.BIN holds, for one capture, the engine-register shadow
(0x400000 + (reg & 0x3FFFF) mirrors 0xC0F00000..0xC0F3FFFF), ManageImgTbl's
state at 0x6748, the develop parameter blocks around 0xb3ed4, the colour
engine ("Lucky") table buffer and the 504-entry "angel" table with its base.

Usage:
  python3 tools/dg_regs_diff.py base=DGREG0.BIN contrast+2=DGREG1.BIN ...

The first file is the baseline. For every other file the report lists the
registers whose shadow value differs, grouped by block, and how the tables
differ. --csv DIR also writes each snapshot's angel table (504 entries,
signed and unsigned) and register differences as CSV for plotting.
"""
import argparse
import csv
import os
import struct
import sys

MAGIC = 0x31524744
HDR = 0x80

# Block owners from the G12 1.00G ROM: which source module writes each 4 KB
# block, from EngDrvOut / Cartridge call sites (static reading).
BLOCKS = {
    0xC0F01000: 'Pwrcnt (block power)',
    0xC0F03000: 'Sharemem',
    0xC0F04000: 'EDMAC channels',
    0xC0F05000: 'EDMAC connections',
    0xC0F06000: 'Head',
    0xC0F07000: 'Head / angel',
    0xC0F08000: 'PrePro (raw: dark, shading, defect)',
    0xC0F09000: 'Qtable / WBExt',
    0xC0F0A000: 'obwb / WbThumb',
    0xC0F0B000: 'WbThumb / Af',
    0xC0F0C000: 'WbThumb',
    0xC0F0D000: 'DevelopPath / ProcessObWb',
    0xC0F0E000: 'ProcessObWb / MedianFilter',
    0xC0F0F000: 'MedianFilter',
    0xC0F10000: 'Af / Hist',
    0xC0F11000: 'resize (FilteringResize side)',
    0xC0F12000: 'Taiwan / H.264',
    0xC0F13000: 'ForLife / JpCore',
    0xC0F14000: 'display',
    0xC0F15000: 'Postpro',
    0xC0F16000: 'Hist',
    0xC0F18000: 'display timing',
    0xC0F1B000: 'Lotus / Fencing',
    0xC0F1C000: 'Fencing',
    0xC0F1D000: 'Cartridge / Saridon2',
    0xC0F1E000: 'Lotus / Huffman',
    0xC0F20000: 'Lucky table port (colour table)',
    0xC0F21000: 'angel table port',
    0xC0F22000: 'Lucky (colour engine)',
    0xC0F24000: 'Cartridge write port',
    0xC0F26000: 'EDMAC channels',
    0xC0F28000: 'Dister',
    0xC0F29000: 'Saridon2 / Dister',
    0xC0F2B000: 'Taiwan',
    0xC0F2C000: 'Fencing / Sumia',
    0xC0F2E000: 'OhyearKi (AE/WB statistics)',
    0xC0F30000: 'EDMAC channels',
}


def block_name(reg):
    return BLOCKS.get(reg & ~0xFFF, '?')


class Snapshot:
    def __init__(self, label, path):
        self.label = label
        data = open(path, 'rb').read()
        if len(data) < HDR:
            sys.exit(f'{path}: too short')
        h = struct.unpack_from('<32I', data, 0)
        if h[0] != MAGIC or h[1] != 1:
            sys.exit(f'{path}: not a DGR1 version 1 snapshot')
        self.slot, self.tick = h[2], h[3]
        self.ptr = {'lucky': h[10], 'angel': h[12], 'base': h[14]}
        self.ok = h[16]
        off = HDR
        sections = {}
        for name, size in (('shadow', h[5]), ('state', h[7]), ('param', h[9]),
                           ('lucky', h[11]), ('angel', h[13]), ('base', h[15])):
            sections[name] = data[off:off + size]
            off += size
        if off > len(data):
            sys.exit(f'{path}: truncated ({len(data)} of {off} bytes)')
        self.s = sections
        self.shadow_base = 0xC0F00000

    def words(self, name):
        b = self.s[name]
        return struct.unpack(f'<{len(b) // 4}I', b[:len(b) // 4 * 4])

    def halves(self, name, n):
        return struct.unpack(f'<{n}H', self.s[name][:2 * n])

    def shalves(self, name, n):
        return struct.unpack(f'<{n}h', self.s[name][:2 * n])


def diff_words(a, b, base):
    return [(base + 4 * i, x, y) for i, (x, y) in enumerate(zip(a, b)) if x != y]


def describe_table(name, a, b, n):
    da = [y - x for x, y in zip(a[:n], b[:n])]
    changed = [i for i, d in enumerate(da) if d]
    if not changed:
        return f'  {name}: identical'
    return (f'  {name}: {len(changed)} of {n} entries differ, first {changed[0]}, '
            f'last {changed[-1]}, delta {min(da)}..{max(da)}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('snapshots', nargs='+', help='label=path (first is the baseline)')
    ap.add_argument('--csv', help='write angel tables and register diffs as CSV here')
    ap.add_argument('--max', type=int, default=40, help='registers to list per block (default 40)')
    args = ap.parse_args()
    snaps = []
    for item in args.snapshots:
        label, _, path = item.rpartition('=')
        snaps.append(Snapshot(label or os.path.basename(path), path))
    base = snaps[0]
    print(f'baseline {base.label}: slot {base.slot}, pointers '
          + ', '.join(f'{k}={v:#010x}' for k, v in base.ptr.items()) + f', ok={base.ok:#x}')
    for s in snaps[1:]:
        print(f'\n=== {s.label} vs {base.label} (slot {s.slot}, {s.tick - base.tick} ms later)')
        if s.ptr != base.ptr:
            print('  table pointers moved: ' + ', '.join(f'{k} {base.ptr[k]:#x}->{s.ptr[k]:#x}'
                                                       for k in s.ptr if s.ptr[k] != base.ptr[k]))
        d = diff_words(base.words('shadow'), s.words('shadow'), s.shadow_base)
        by_block = {}
        for reg, x, y in d:
            by_block.setdefault(reg & ~0xFFF, []).append((reg, x, y))
        print(f'  shadow: {len(d)} registers differ in {len(by_block)} blocks')
        for blk in sorted(by_block):
            rows = by_block[blk]
            print(f'  {blk:#010x} {block_name(blk)}: {len(rows)}')
            for reg, x, y in rows[:args.max]:
                print(f'      {reg:#010x}: {x:#010x} -> {y:#010x}')
            if len(rows) > args.max:
                print(f'      ... {len(rows) - args.max} more')
        for name in ('state', 'param'):
            dd = diff_words(base.words(name), s.words(name), {'state': 0x6748, 'param': 0xb3e00}[name])
            print(f'  {name} RAM: {len(dd)} words differ' + ''.join(
                f'\n      {a:#07x}: {x:#010x} -> {y:#010x}' for a, x, y in dd[:args.max]))
        print(describe_table('colour table (Lucky, 1000 words)', base.words('lucky'), s.words('lucky'), 1000))
        print(describe_table('Lucky buffer tail (kind 3, to 5,104 bytes)', base.words('lucky')[1000:],
                             s.words('lucky')[1000:], 276))
        print(describe_table('angel table (504 x 16 bit)', base.shalves('angel', 504), s.shalves('angel', 504), 504))
        print(describe_table('angel base (1008 x 16 bit)', base.shalves('base', 1008), s.shalves('base', 1008), 1008))
    if args.csv:
        os.makedirs(args.csv, exist_ok=True)
        with open(os.path.join(args.csv, 'angel.csv'), 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['index'] + [f'{s.label} u16' for s in snaps] + [f'{s.label} s16' for s in snaps])
            us = [s.halves('angel', 504) for s in snaps]
            ss = [s.shalves('angel', 504) for s in snaps]
            for i in range(504):
                w.writerow([i] + [u[i] for u in us] + [v[i] for v in ss])
        with open(os.path.join(args.csv, 'registers.csv'), 'w', newline='') as f:
            w = csv.writer(f)
            regs = set()
            for s in snaps[1:]:
                regs.update(r for r, _, _ in diff_words(base.words('shadow'), s.words('shadow'), s.shadow_base))
            w.writerow(['register', 'block'] + [s.label for s in snaps])
            sw = [s.words('shadow') for s in snaps]
            for r in sorted(regs):
                i = (r - 0xC0F00000) // 4
                w.writerow([f'{r:#010x}', block_name(r)] + [f'{x[i]:#010x}' for x in sw])
        print(f'\nCSV written to {args.csv}')


if __name__ == '__main__':
    main()
