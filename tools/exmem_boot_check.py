#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""After booting an EXMEM core (CHDK loaded into exmem pool 0), check over USB
that the pool really covers it.

With OPT_CHDK_IN_EXMEM the loader copies the core to MEMISOSTART before Canon
starts, and only later does CHDK's spytask take exmem pool 0 around it
(exmem_malloc_init). If Canon had allocated other exmem first (EXMEM_COM sits
at the top once the camera is running), pool 0 lands lower and the core is
left in memory Canon hands out. This prints Canon's exmem table (ROM
0xff890270 stacks allocations down from 0x48000000; table at 0x3bfc8, an
address and a length per type), CHDK's heaps, and compares the core's code
in RAM with the build, byte for byte.

Usage: exmem_boot_check.py [build tree] (default build/firmware/chdk)."""
from pathlib import Path
import os
import subprocess, sys, tempfile
lab = Path(__file__).resolve().parents[1]
tree = Path(sys.argv[1]) if len(sys.argv) > 1 else lab / 'build/firmware/chdk'
elf = tree / 'platform/g12/sub/100g/main.elf'
core = (tree / 'platform/g12/sub/100g/main.bin').read_bytes()
nm = os.environ.get('CROSS_COMPILE', 'arm-none-eabi-') + 'nm'
syms = dict((l.split()[2], int(l.split()[0], 16)) for l in subprocess.run(
    [str(nm), str(elf)], capture_output=True, text=True, check=True).stdout.splitlines() if len(l.split()) == 3)
start, end, n = syms['_start'], syms['_end'], syms['link_data_start'] - syms['_start']
usb = [sys.executable, str(lab / 'tools/g12_usb.py')]

lua = ("local t={} for i=0,18 do local a,l=peek(0x3bfc8+i*8),peek(0x3bfc8+i*8+4) "
       "if l~=0 then t[#t+1]=string.format('type %d %08x len %08x end %08x',i,a,l,a+l) end end "
       "for _,h in ipairs({'exmem','aram'}) do local m=get_meminfo(h) if m then "
       "t[#t+1]=string.format('%s heap %08x-%08x free %d',h,m.start_address,m.end_address,m.free_size) "
       "else t[#t+1]=h..' heap: none' end end return table.concat(t,' | ')")
p = subprocess.run(usb + ['lua', lua], capture_output=True, text=True)
ret = [l.split(':return:', 1)[1].strip("'") for l in p.stdout.splitlines() if ':return:' in l]
if not ret:
    sys.exit((p.stdout + p.stderr).strip() or 'no reply from the camera')
print('\n'.join(ret[0].split(' | ')))

# The code: from _start to the start of .data (the build's text and rodata).
with tempfile.TemporaryDirectory() as d:
    f = Path(d) / 'core.bin'
    subprocess.run(usb + ['raw', f'rmem {start:#x} {n} -f={f}'], capture_output=True, text=True)
    ram = f.read_bytes() if f.exists() else b''
if len(ram) != n:
    sys.exit(f'could not read the core back ({len(ram)} of {n} bytes)')
diff = [i for i in range(n) if ram[i] != core[i]]
print(f'core {start:#010x}-{end:#010x} ({end - start} bytes); code compared {n} bytes: '
      + ('identical' if not diff else f'{len(diff)} bytes differ, first at +{diff[0]:#x}, last +{diff[-1]:#x}'))
sys.exit(1 if [i for i in diff if i >= 32] else 0)   # the first 32 bytes run once at boot
