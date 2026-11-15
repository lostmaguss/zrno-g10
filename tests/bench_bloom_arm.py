# SPDX-License-Identifier: GPL-2.0-or-later
"""Cross-compiled ARM bloom must match host; counts are not camera timings."""
from pathlib import Path
import os
import ctypes as C
import json
import struct
import subprocess
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_BLOCK
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_PC
from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
lab=Path(__file__).resolve().parents[1]
compiler=os.environ.get('CROSS_COMPILE','arm-none-eabi-');stem=lab/'build/bloom-arm'
support=lab/'build/bloom_mem.c';support.write_text('void *memset(void *p,int v,unsigned n){unsigned char *q=p;while(n--)*q++=v;return p;}\nvoid *memcpy(void *d,const void *s,unsigned n){unsigned char *x=d;const unsigned char *y=s;while(n--)*x++=*y++;return d;}\n')
subprocess.run([str(compiler)+'gcc','-O2','-marm','-mcpu=arm946e-s','-fno-builtin','-nostdlib','-Wl,-Ttext=0x200000','-Wl,--entry=film_uyvy_bloom',str(lab/'camera/film_bloom.c'),str(support),'-lgcc','-o',str(stem)+'.elf'],check=True)
subprocess.run([str(compiler)+'objcopy','-O','binary',str(stem)+'.elf',str(stem)+'.bin'],check=True)
nm=subprocess.run([str(compiler)+'nm','-n',str(stem)+'.elf'],capture_output=True,text=True,check=True).stdout
symbols={line.split()[2]:int(line.split()[0],16) for line in nm.splitlines() if len(line.split())==3}
host=C.CDLL(str(lab/'build/bloom.dylib'));host.film_uyvy_bloom.argtypes=[C.c_void_p,C.c_size_t]+[C.c_uint]*4+[C.c_void_p,C.c_size_t,C.c_void_p]
host.film_bloom_work_size.argtypes=[C.c_uint];host.film_bloom_work_size.restype=C.c_size_t
width,height=128,67
source=bytes(((i*73+(i//7)*29)&255) for i in range(width*height*2));results=[]
for level in range(4):
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x100000,0x500000)
 u.mem_write(0x100000,source);u.mem_write(0x200000,stem.with_suffix('.bin').read_bytes())
 u.reg_write(UC_ARM_REG_SP,0x500000);u.reg_write(UC_ARM_REG_LR,0x580000)
 for reg,value in [(UC_ARM_REG_R0,0x100000),(UC_ARM_REG_R1,len(source)),(UC_ARM_REG_R2,width),(UC_ARM_REG_R3,height)]:u.reg_write(reg,value)
 capacity=host.film_bloom_work_size(width)
 u.mem_write(0x500000,struct.pack('<5I',width*2,level,0x400000,capacity,0))
 md=Cs(CS_ARCH_ARM,CS_MODE_ARM);cache={};count=[0]
 def block(machine,address,size,ctx):
  key=(address,size)
  if key not in cache:cache[key]=sum(1 for _ in md.disasm(bytes(machine.mem_read(address,size)),address))
  count[0]+=cache[key]
 u.hook_add(UC_HOOK_BLOCK,block);u.emu_start(symbols['film_uyvy_bloom'],0x580000,count=100000000)
 assert u.reg_read(UC_ARM_REG_PC)==0x580000 and u.reg_read(UC_ARM_REG_R0)==0
 image=C.create_string_buffer(source);work=C.create_string_buffer(capacity)
 assert not host.film_uyvy_bloom(image,len(source),width,height,width*2,level,work,capacity,None)
 assert bytes(u.mem_read(0x100000,len(source)))==image.raw[:len(source)]
 results.append(dict(level=level,pixels=width*height,instructions=count[0],instructions_per_pixel=count[0]/(width*height),host_output_exact=True))
report=dict(caveat='Instruction counts omit real cache, memory, scheduling and progress yields; hardware wall time not measured.',results=results)
(lab/'validation/bloom-arm-v0.8.1.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
