#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fail unless the G12 core leaves CHDK's heap enough room.

The build now loads CHDK into EXMEM (4 MB at 0x79089e0; see
tools/integrate.py), where the core leaves megabytes. Older builds loaded it
into AgentRAM (0x3ce000, 0x32000 bytes), where CHDK turns what the core
leaves into a small heap. With very little left (80
bytes in 0.10.0-exp24) the heap's end guard lands on its own terminator cell
and the first allocation asserts ("suba_alloc bad cell"): the camera turns
off at start-up. Builds that booted left 288 bytes or more; this asks for 512.
Usage: core_room.py [main.elf] (default: build/firmware/chdk's)."""
from pathlib import Path
import os
import subprocess, sys
lab = Path(__file__).resolve().parents[1]
elf = Path(sys.argv[1]) if len(sys.argv) > 1 else lab / 'build/firmware/chdk/platform/g12/sub/100g/main.elf'
nm = os.environ.get('CROSS_COMPILE', 'arm-none-eabi-') + 'nm'
syms = dict((l.split()[2], int(l.split()[0], 16)) for l in subprocess.run([str(nm), str(elf)], capture_output=True, text=True, check=True).stdout.splitlines() if len(l.split()) == 3)
NEED = 512
blocks = {0x3ce000: ('ARAM', 0x32000), 0x79089e0: ('EXMEM', 0x400000)}
start = syms['_start']
assert start in blocks, f"core at neither the ARAM nor the EXMEM start: {start:#x}"
name, size = blocks[start]
room = start + size - syms['_end']
print(f'core {syms["_end"] - start} bytes in {name}; heap room {room} bytes (need {NEED})')
sys.exit(0 if room >= NEED else 1)
