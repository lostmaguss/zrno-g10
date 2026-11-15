# SPDX-License-Identifier: GPL-2.0-or-later
"""Print shot: ARM instruction count of film_riso_render on strips of an
L frame (3648 wide), two and three inks, noise 2, heavy print flaws. Two
strip heights separate the per-shot setup from the per-row cost; the frame
estimate is setup + 2736 rows. Counts are not camera time: no cache, bus
or scheduling (~1.5 cycles per instruction at 144 MHz is a fair guess).
Needs unicorn and capstone (pip install unicorn capstone)."""
import bisect, subprocess, sys
from pathlib import Path
import os
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_BLOCK
from unicorn.arm_const import *
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
lab = Path(__file__).resolve().parents[1]; d = lab / 'build'; d.mkdir(exist_ok=True)
cc = os.environ.get('CROSS_COMPILE', 'arm-none-eabi-')
subprocess.run([cc + 'gcc', '-O2', '-finline', '-marm', '-mcpu=arm946e-s', '-fno-builtin', '-nostdlib', '-I' + str(lab / 'camera'),
                '-Wl,-Ttext=0x8000', '-Wl,--entry=strip', str(lab / 'tests/riso_arm_frame.c'), str(lab / 'camera/film_riso.c'),
                '-lgcc', '-o', str(d / 'riso-arm.elf')], check=True)
subprocess.run([cc + 'objcopy', '-O', 'binary', str(d / 'riso-arm.elf'), str(d / 'riso-arm.bin')], check=True)
nm = subprocess.run([cc + 'nm', '-n', str(d / 'riso-arm.elf')], capture_output=True, text=True, check=True).stdout
sym = {l.split()[2]: int(l.split()[0], 16) for l in nm.splitlines() if len(l.split()) == 3}
data = (lab / 'card/CHDK/FILM/PRINT.BIN').read_bytes()
FRAME, DATA = 0x1000000, 0x3000000


def count(rows, inks):
    u = Uc(UC_ARCH_ARM, UC_MODE_ARM); u.mem_map(0, 0x4000000)
    u.mem_write(0x8000, (d / 'riso-arm.bin').read_bytes())
    u.mem_write(FRAME, bytes((i * 37 >> 3) & 255 for i in range(3648 * 2 * 16)) * (rows // 16))
    u.mem_write(DATA, data)
    u.reg_write(UC_ARM_REG_SP, 0xFF0000); u.reg_write(UC_ARM_REG_LR, 0xF00000)
    for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), (rows, inks, FRAME, DATA)):
        u.reg_write(r, v)
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM); cache = {}; n = [0]
    def blk(m, a, s, c):
        if (a, s) not in cache: cache[(a, s)] = sum(1 for _ in md.disasm(bytes(m.mem_read(a, s)), a))
        n[0] += cache[(a, s)]
    u.hook_add(UC_HOOK_BLOCK, blk); u.emu_start(sym['strip'], 0xF00000)
    return n[0]


CASES = [(0, 'Riso, two inks'), (6, 'Riso, three inks'), (108, 'Screenprint'), (116, 'Engraving'), (118, 'Engraving, two plates'),
         (124, 'Woodcut'), (125, 'Woodcut, two blocks'), (132, 'Photocopy'), (140, 'Comic'), (148, 'Text mode'), (150, 'Text mode, colour')]
for inks, name in CASES if len(sys.argv) < 2 or sys.argv[1] != 'riso' else CASES[:2]:
    a, b = count(64, inks), count(192, inks)
    per_row = (b - a) / 128
    setup = a - 64 * per_row
    total = setup + 2736 * per_row
    print(f'{name}: setup {setup / 1e6:.1f} M, {per_row / 3648:.1f} instr/pixel; L frame {total / 1e6:.0f} M instr '
          f'(~{total * 1.5 / 144e6:.1f} s at 144 MHz, CPI 1.5, before memory stalls)')
