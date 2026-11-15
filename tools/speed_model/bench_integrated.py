# SPDX-License-Identifier: GPL-2.0-or-later
"""Integrated branch kernels vs ec4007c: exactness and modelled cost on the camera fixture."""
import sys,json; sys.path.insert(0,str(__import__('pathlib').Path(__file__).resolve().parent))
from harness import *
from profile_fused import compile_with_include,fused
import numpy as np
import os
NEW=lab/'camera'
# Reference sources: a camera/ folder from the baseline commit, e.g.
#   git show ec4007c:camera/film_colour.c > ref/film_colour.c (and bloom/grain files)
OLD=Path(os.environ.get('G12_REFERENCE',root/'work/explore-speed/build/ref_cam'))
def prepared(raw):
    a=bytearray(raw); nodes=np.frombuffer(bytes(a[16:]),'<u2').reshape(-1,3)
    mono=bool(((nodes[:,0]==nodes[:,1])&(nodes[:,1]==nodes[:,2])).all())
    a[16:]=(nodes^0x8000).astype('<u2').tobytes(); a[0:8]=b'G12LUTP\0'; a[10]=1 if mono else 0; a[11]=0
    return bytes(a)
def measure(k,entry,lut,rows=(48,96)):
    rs=[]
    for h in rows:
        frame,w,_=band(h,fixture='camera')
        rs.append(run(k,frame,colour_args(entry,w,h),lut,regs=(IMAGE,len(frame),w,h),cache_sizes=(8192,)))
    px=w*(rows[1]-rows[0]); d=lambda f:(f(rs[1])-f(rs[0]))/px
    return dict(cycles=d(lambda r:r['cycles']),uncached=d(lambda r:r['uncached'])),[r['image'] for r in rs]
res=[]
for entry in ('film_uyvy_map_cached','film_uyvy_map_halo_cached'):
    old=compile_arm(OLD/'film_colour.c',entry,'old-'+entry); new=compile_arm(NEW/'film_colour.c',entry,'new-'+entry)
    for name in ('PORTRA.LUT','HP5.LUT','TRIX.LUT'):
        raw=lut_bytes(name); (a,oa),(b,ob)=measure(old,entry,raw),measure(new,entry,prepared(raw))
        r=dict(kernel=entry,lut=name,exact=oa==ob,old_cycles=round(a['cycles'],1),new_cycles=round(b['cycles'],1),
               old_uncached=a['uncached'],new_uncached=b['uncached'],saved_pct=round(100*(1-b['cycles']/a['cycles']),2))
        print(r,flush=True);res.append(r);assert r['exact']
old=compile_with_include(OLD,'old-fused');new=compile_with_include(NEW,'new-fused')
for level in (1,2,3):
    (a,oa),(b,ob)=fused(old,'camera',level),fused(new,'camera',level)
    r=dict(kernel='bloom+grain(High)',bloom=level,exact=oa==ob,old_cycles=round(a['cycles'],1),new_cycles=round(b['cycles'],1),
           old_uncached=a['uncached'],new_uncached=b['uncached'],saved_pct=round(100*(1-b['cycles']/a['cycles']),2))
    print(r,flush=True);res.append(r);assert r['exact']
json.dump(res,open(build/'bench-integrated.json','w'),indent=1)
