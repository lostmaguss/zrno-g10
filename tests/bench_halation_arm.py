# SPDX-License-Identifier: GPL-2.0-or-later
"""Owned ARM kernel output vs host, and Off/On instruction-count comparison.
Not ARM946 cycles, cache costs, full-camera time or a watchdog guarantee.
"""
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
compiler = os.environ.get('CROSS_COMPILE', 'arm-none-eabi-')
build = lab / 'build'; stem = build / 'halation-arm'
(build / 'bench_memset.c').write_text('void *memset(void *p,int v,unsigned n){unsigned char *q=p;while(n--)*q++=v;return p;}\nint memcmp(const void *a,const void *b,unsigned n){const unsigned char *x=a,*y=b;while(n--){int d=*x++-*y++;if(d)return d;}return 0;}\nvoid *memcpy(void *d,const void *s,unsigned n){unsigned char *x=d;const unsigned char *y=s;while(n--)*x++=*y++;return d;}\n')
subprocess.run([str(compiler) + 'gcc', '-O2', '-marm', '-mcpu=arm946e-s', '-fno-builtin', '-nostdlib', '-Wl,-Ttext=0x200000', '-Wl,--entry=film_uyvy_map_halo', str(lab / 'camera/film_colour.c'), str(lab / 'camera/film_retro.c'), str(lab / 'camera/film_toy.c'), str(lab / 'camera/film_print.c'), str(build / 'bench_memset.c'), '-lgcc', '-o', str(stem) + '.elf'], check=True)
subprocess.run([str(compiler) + 'objcopy', '-O', 'binary', str(stem) + '.elf', str(stem) + '.bin'], check=True)
nm = subprocess.run([str(compiler) + 'nm', '-n', str(stem) + '.elf'], capture_output=True, text=True, check=True).stdout
(stem.with_suffix('.symbols')).write_text(nm)
symbols = {line.split()[2]: int(line.split()[0], 16) for line in nm.splitlines() if len(line.split()) == 3}
host = C.CDLL(str(build / 'colour.dylib'))
host.film_uyvy_patch.argtypes = [C.c_void_p, C.c_size_t] + [C.c_uint] * 7 + [C.c_void_p]
host.film_uyvy_map_halo_level.argtypes = [C.c_void_p, C.c_size_t] + [C.c_uint] * 3 + [C.c_void_p, C.c_uint, C.c_void_p, C.c_size_t, C.c_void_p]
host.film_halo_work_size.argtypes=[C.c_uint];host.film_halo_work_size.restype=C.c_size_t
raw = (lab / 'luts/GOLD17.LUT').read_bytes(); lb = C.create_string_buffer(raw)
host.film_lut_prepare.argtypes = [C.c_void_p, C.c_size_t]; assert host.film_lut_prepare(lb, len(raw)) == 0
lut = lb.raw[:len(raw)]  # kernels take the prepared in-memory form
source = bytes(((i * 73 + (i // 7) * 29) & 255) for i in range(8192))
width, height = 128, 64
captured = (source * 4)[:width * height * 2]
lights = bytearray(bytes([0, 25, 0, 25]) * (width * height // 2))
for y in range(8, 56):
    for x in range(40, 88): lights[(y * width + x) * 2 + 1] = 255
report = []
for label, data in [('synthetic_colours', captured), ('highlight_rectangle', bytes(lights))]:
    results = []
    for level in (0,1,2,3):
        halo=level!=0
        u = Uc(UC_ARCH_ARM, UC_MODE_ARM); u.mem_map(0x100000, 0x500000)
        u.mem_write(0x100000, data); u.mem_write(0x200000, stem.with_suffix('.bin').read_bytes()); u.mem_write(0x300000, lut)
        u.reg_write(UC_ARM_REG_SP, 0x500000); u.reg_write(UC_ARM_REG_LR, 0x580000)
        for reg, value in [(UC_ARM_REG_R0, 0x100000), (UC_ARM_REG_R1, len(data)), (UC_ARM_REG_R2, width), (UC_ARM_REG_R3, height)]: u.reg_write(reg, value)
        need=host.film_halo_work_size(width)
        stack = [width * 2, 0x300000, level, 0x400000, need, 0] if halo else [width * 2, 0, 0, width, height, 0x300000]
        u.mem_write(0x500000, struct.pack('<' + 'I' * len(stack), *stack))
        md = Cs(CS_ARCH_ARM, CS_MODE_ARM); cache = {}; count = [0]
        def block(machine, address, size, context):
            key = (address, size)
            if key not in cache: cache[key] = sum(1 for _ in md.disasm(bytes(machine.mem_read(address, size)), address))
            count[0] += cache[key]
        u.hook_add(UC_HOOK_BLOCK, block)
        u.emu_start(symbols['film_uyvy_map_halo_level' if halo else 'film_uyvy_patch'], 0x580000, count=100000000)
        assert u.reg_read(UC_ARM_REG_PC) == 0x580000 and u.reg_read(UC_ARM_REG_R0) == 0
        memory = C.create_string_buffer(data)
        if halo:
            work = C.create_string_buffer(need)
            assert not host.film_uyvy_map_halo_level(memory, len(data), width, height, width * 2, lb, level, work, len(work), None)
        else: assert not host.film_uyvy_patch(memory, len(data), width, height, width * 2, 0, 0, width, height, lb)
        assert bytes(u.mem_read(0x100000, len(data))) == memory.raw[:len(data)]
        results.append(dict(halo_level=level, arm_instructions=count[0], exact_host_output=True))
    report.append(dict(source=label, pixels=width * height, runs=results, instruction_ratios={str(r['halo_level']):r['arm_instructions']/results[0]['arm_instructions'] for r in results[1:]}))
out = dict(caveat='Counts include a simple test memset/memcpy, not firmware implementations; not cycles or camera timing.', results=report)
(lab / 'validation/halation-arm-v0.4.0.json').write_text(json.dumps(out, indent=2) + '\n')
print(json.dumps(out, indent=2))
