# SPDX-License-Identifier: GPL-2.0-or-later
import sys; sys.path.insert(0,str(__import__('pathlib').Path(__file__).resolve().parent))
from harness import *
import harness, subprocess
cam=lab/'camera'
def build(src_dir,tag,defines=()):
    # compile_arm takes one file; point the include path at the chosen camera dir.
    orig=harness.compile_arm
    elf=build_dir=None
    return compile_with_include(src_dir,tag,defines)
def compile_with_include(src_dir,tag,defines=()):
    elf=harness.build/f'{tag}.elf';binary=harness.build/f'{tag}.bin'
    subprocess.run([harness.prefix+'gcc','-O2','-g','-finline','-fno-strict-aliasing','-fno-schedule-insns2','-marm','-mcpu=arm946e-s',
        '-fno-builtin','-nostdlib','-Wl,-Ttext=0x200000','-Wl,--entry=bench_fused','-I',str(src_dir),*[f'-D{d}' for d in defines],
        str(here/'fused_entry.c'),str(harness.support),'-lgcc','-o',str(elf)],check=True)
    subprocess.run([harness.prefix+'objcopy','-O','binary',str(elf),str(binary)],check=True)
    nm=subprocess.check_output([harness.prefix+'nm','-n',str(elf)],text=True)
    sy={p[2]:int(p[0],16) for l in nm.splitlines() if len(p:=l.split())==3}
    return dict(binary=binary.read_bytes(),entry=sy['bench_fused'],elf=elf,text=0)
def fused(k,fixture,level=3,grain=3,rows=(48,96),caches=(),profile=False):
    rs=[]
    for h in rows:
        frame,w,_=band(h,fixture=fixture)
        rs.append(run(k,frame,[level,WORK,SCRATCH,grain],regs=(IMAGE,len(frame),w,h),cache_sizes=caches or (8192,),profile=profile))
    px=w*(rows[1]-rows[0]);d=lambda f:(f(rs[1])-f(rs[0]))/px
    m=dict(instr=d(lambda r:r['instructions']),cycles=d(lambda r:r['cycles']),uncached=d(lambda r:r['uncached']))
    for s in caches: m[f'dram{s//1024}k']=d(lambda r:r['caches'][s]['linefills']+r['caches'][s]['writebacks'])
    if profile: m['profile']={l:(rs[1]['profile'][l]-rs[0]['profile'].get(l,0))/px for l in rs[1]['profile']}
    return m,[r['image'] for r in rs]
if __name__=='__main__':
    k=compile_with_include(cam,'fused-ref')
    for fx in ('jpeg','camera'):
        m,_=fused(k,fx,profile=True,caches=(8192,))
        print(fx,{a:round(b,2) for a,b in m.items() if a!='profile'})
        for l,v in sorted(m['profile'].items(),key=lambda x:-x[1])[:16]: print(f"  {v:6.1f} {l}")
