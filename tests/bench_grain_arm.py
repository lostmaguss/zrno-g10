# SPDX-License-Identifier: GPL-2.0-or-later
"""Cross-compiled ARM grain must match host output. Counts are not camera time."""
from pathlib import Path
import os
import ctypes as C
import json
import struct
import subprocess
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_BLOCK
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_PC
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
lab = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CROSS_COMPILE', 'arm-none-eabi-'); stem = lab / 'build/grain-arm'
subprocess.run([str(compiler) + 'gcc', '-O2', '-marm', '-mcpu=arm946e-s', '-fno-builtin', '-nostdlib', '-Wl,-Ttext=0x200000', '-Wl,--entry=film_uyvy_grain', str(lab / 'camera/film_grain.c'), '-lgcc', '-o', str(stem) + '.elf'], check=True)
subprocess.run([str(compiler) + 'objcopy', '-O', 'binary', str(stem) + '.elf', str(stem) + '.bin'], check=True)
nm = subprocess.run([str(compiler) + 'nm', '-n', str(stem) + '.elf'], capture_output=True, text=True, check=True).stdout
symbols = {line.split()[2]: int(line.split()[0], 16) for line in nm.splitlines() if len(line.split()) == 3}
host = C.CDLL(str(lab / 'build/grain.dylib')); host.film_uyvy_grain.argtypes = [C.c_void_p, C.c_size_t] + [C.c_uint] * 5 + [C.c_void_p, C.c_size_t, C.c_void_p]
width, height, seed = 128, 64, 0xcafebabe
source = bytes(((i * 73 + (i // 7) * 29) & 255) for i in range(8192))
report = []
for label, data in [('midtone', bytes([0, 128, 0, 128]) * (width * height // 2)), ('synthetic', (source * 4)[:width * height * 2])]:
    for level in range(4):
        u = Uc(UC_ARCH_ARM, UC_MODE_ARM); u.mem_map(0x100000, 0x500000)
        u.mem_write(0x100000, data); u.mem_write(0x200000, stem.with_suffix('.bin').read_bytes())
        u.reg_write(UC_ARM_REG_SP, 0x500000); u.reg_write(UC_ARM_REG_LR, 0x580000)
        for reg, value in [(UC_ARM_REG_R0, 0x100000), (UC_ARM_REG_R1, len(data)), (UC_ARM_REG_R2, width), (UC_ARM_REG_R3, height)]: u.reg_write(reg, value)
        capacity = (width // 2 + 1) * 2
        u.mem_write(0x500000, struct.pack('<6I', width * 2, level, seed, 0x400000, capacity, 0))
        md = Cs(CS_ARCH_ARM, CS_MODE_ARM); cache = {}; count = [0]
        def block(machine, address, size, context):
            key = (address, size)
            if key not in cache: cache[key] = sum(1 for _ in md.disasm(bytes(machine.mem_read(address, size)), address))
            count[0] += cache[key]
        u.hook_add(UC_HOOK_BLOCK, block); u.emu_start(symbols['film_uyvy_grain'], 0x580000, count=100000000)
        assert u.reg_read(UC_ARM_REG_PC) == 0x580000 and u.reg_read(UC_ARM_REG_R0) == 0
        image = C.create_string_buffer(data); work = C.create_string_buffer(capacity)
        assert not host.film_uyvy_grain(image, len(data), width, height, width * 2, level, seed, work, capacity, None)
        assert bytes(u.mem_read(0x100000, len(data))) == image.raw[:len(data)]
        report.append(dict(source=label, level=level, pixels=width * height, instructions=count[0], instructions_per_pixel=count[0] / (width * height), host_output_exact=True))
out = dict(caveat='Instruction counts exclude camera cache, memory bus, scheduling and progress yields; not wall time.', results=report)
(lab / 'validation/grain-arm-v0.5.0.json').write_text(json.dumps(out, indent=2) + '\n'); print(json.dumps(out, indent=2))
