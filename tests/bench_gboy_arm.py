# SPDX-License-Identifier: GPL-2.0-or-later
"""ZrnoBoy live view: ARM instruction count of one frame (sample, levels,
picture, overlay) for the G12's ARM946E-S. Counts are not camera time:
no cache, bus or scheduling. Needs unicorn and capstone (pip install unicorn capstone)."""
import subprocess, sys
from pathlib import Path
import os
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_BLOCK
from unicorn.arm_const import *
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
lab=Path(__file__).resolve().parents[1]; d=lab/'build'; d.mkdir(exist_ok=True); cc=os.environ.get('CROSS_COMPILE','arm-none-eabi-')
subprocess.run([cc+'gcc','-O2','-marm','-mcpu=arm946e-s','-fno-builtin','-nostdlib','-I'+str(lab/'camera'),'-Wl,-Ttext=0x8000','-Wl,--entry=live_frame',str(lab/'tests/gboy_arm_frame.c'),str(lab/'camera/film_gboy.c'),'-lgcc','-o',str(d/'gboy-arm.elf')],check=True)
subprocess.run([cc+'objcopy','-O','binary',str(d/'gboy-arm.elf'),str(d/'gboy-arm.bin')],check=True)
nm=subprocess.run([cc+'nm','-n',str(d/'gboy-arm.elf')],capture_output=True,text=True,check=True).stdout
sym={l.split()[2]:int(l.split()[0],16) for l in nm.splitlines() if len(l.split())==3}
for frame,res in ((0,0),(1,0),(0,1),(1,1)):
    u=Uc(UC_ARCH_ARM,UC_MODE_ARM); u.mem_map(0,0x600000)
    u.mem_write(0x8000,(d/'gboy-arm.bin').read_bytes())
    u.mem_write(0x100000,bytes((i*7//9)&255 for i in range(1080*480)))
    u.reg_write(UC_ARM_REG_SP,0x5f0000); u.reg_write(UC_ARM_REG_LR,0x580000); u.reg_write(UC_ARM_REG_R0,frame); u.reg_write(UC_ARM_REG_R1,res)
    md=Cs(CS_ARCH_ARM,CS_MODE_ARM); cache={}; n=[0]
    funcs=sorted((v,k) for k,v in sym.items() if v>=0x8000 and v<0x100000)
    per={}
    import bisect
    addrs=[f[0] for f in funcs]
    def blk(m,a,s,c):
        k=(a,s)
        if k not in cache: cache[k]=sum(1 for _ in md.disasm(bytes(m.mem_read(a,s)),a))
        n[0]+=cache[k]
        f=funcs[bisect.bisect_right(addrs,a)-1][1]; per[f]=per.get(f,0)+cache[k]
    u.hook_add(UC_HOOK_BLOCK,blk); u.emu_start(sym['live_frame'],0x580000,count=500000000)
    print(f'frame {frame} resolution {res}: {n[0]/1e6:.1f} M instructions (~{n[0]/144e3*1.5:.0f} ms at 144 MHz, CPI 1.5, excluding memory stalls)'); print({k:round(v/1e6,2) for k,v in sorted(per.items(),key=lambda x:-x[1])[:8]})
