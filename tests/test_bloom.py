# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual bloom vs independent full-mask convolution and white-screen reference."""
from pathlib import Path
import ctypes as C
import json
import subprocess
import numpy as np
lab=Path(__file__).resolve().parents[1]
subprocess.run(['cc','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror',str(lab/'camera/film_bloom.c'),'-o',str(lab/'build/bloom.dylib')],check=True)
lib=C.CDLL(str(lab/'build/bloom.dylib'))
lib.film_bloom_work_size.argtypes=[C.c_uint];lib.film_bloom_work_size.restype=C.c_size_t
lib.film_uyvy_bloom.argtypes=[C.c_void_p,C.c_size_t]+[C.c_uint]*4+[C.c_void_p,C.c_size_t,C.c_void_p]
def reference(data,w,h,stride,level):
    if not level:return data
    source=np.frombuffer(data,dtype=np.uint8).reshape(h,stride)
    y=source[:,1:2*w:2].astype(np.int64)
    threshold=(0,192,168,144)[level]
    span=(255-threshold)**2
    energy=(np.maximum(y-threshold,0)**2*255+span//2)//span
    gw,gh=(w+3)//4,(h+3)//4
    pad=np.zeros((gh*4,gw*4),dtype=np.int64);pad[:h,:w]=energy
    valid=np.zeros_like(pad);valid[:h,:w]=1
    sums=pad.reshape(gh,4,gw,4).sum(axis=(1,3));counts=valid.reshape(gh,4,gw,4).sum(axis=(1,3))
    field=(sums+counts//2)//counts
    r=(0,8,16,24)[level];den=(r+1)**2
    # Direct separable convolution, independent of the C rolling sums/rings.
    for axis in (1,0):
        padded=np.pad(field,((r,r),(0,0)) if axis==0 else ((0,0),(r,r)),mode='edge')
        total=np.zeros_like(field)
        for k in range(-r,r+1):
            weight=r+1-abs(k)
            total += weight*(padded[k+r:k+r+gh,:] if axis==0 else padded[:,k+r:k+r+gw])
        field=(total+den//2)//den
    yy=np.arange(h)//4;yn=np.minimum(yy+1,gh-1);fy=(np.arange(h)&3)[:,None]
    xx=np.arange(w)//4;xn=np.minimum(xx+1,gw-1);fx=(np.arange(w)&3)[None,:]
    a=field[yy[:,None],xx[None,:]]*(4-fy)+field[yn[:,None],xx[None,:]]*fy
    b=field[yy[:,None],xn[None,:]]*(4-fy)+field[yn[:,None],xn[None,:]]*fy
    glow=(((a*(4-fx)+b*fx+8)//16)*(0,32,120,192)[level]+127)//255
    out=source.copy();out[:,1:2*w:2]=y+((255-y)*glow+127)//255
    average=(glow[:,0::2]+glow[:,1::2]+1)//2
    for offset in (0,2):
        chroma=source[:,offset:2*w:4].astype(np.int64);chroma=np.where(chroma<128,chroma,chroma-256)
        result=np.sign(chroma)*((np.abs(chroma)*(255-average)+127)//255)
        out[:,offset:2*w:4]=result%256
    return out.tobytes()

def actual(data,w,h,stride,level,callbacks=False):
    need=lib.film_bloom_work_size(w);image=C.create_string_buffer(b'A'*32+data+b'Z'*32)
    work=C.create_string_buffer(b'B'*32+b'X'*need+b'Y'*32);rows=[]
    @C.CFUNCTYPE(None,C.c_uint,C.c_uint)
    def progress(row,total):assert total==h;rows.append(row)
    assert not lib.film_uyvy_bloom(C.byref(image,32),len(data),w,h,stride,level,C.byref(work,32),need,C.cast(progress,C.c_void_p) if callbacks else None)
    assert image.raw[:32]==b'A'*32 and image.raw[32+len(data):64+len(data)]==b'Z'*32
    assert work.raw[:32]==b'B'*32 and work.raw[32+need:64+need]==b'Y'*32
    if callbacks:assert rows==list(range(32,h,32))+[h]
    return image.raw[32:32+len(data)]
rng=np.random.default_rng(4128)
for w,h in [(2,1),(6,3),(10,11),(80,79),(256,193),(3744,7)]:
    stride=w*2+6;source=rng.integers(0,256,(h,stride),dtype=np.uint8).tobytes()
    for level in (0,1,2,3):assert actual(source,w,h,stride,level)==reference(source,w,h,stride,level),(w,h,level)
for tone in (0,30,128,144,255):
    source=bytes([0,tone,0,tone])*(128*96//2)
    for level in (1,2,3):assert actual(source,128,96,256,level)==source
# Large neutral highlight: glow grows with strength, preserves white/neutral,
# reaches outside the source and leaves distant black unchanged.
w,h=384,320;source=np.zeros((h,w*2),dtype=np.uint8);source[96:224,128*2+1:256*2:2]=255
measure=[]
for level in (1,2,3):
    result=actual(source.tobytes(),w,h,w*2,level);assert result==reference(source.tobytes(),w,h,w*2,level)
    out=np.frombuffer(result,dtype=np.uint8).reshape(h,w*2)
    assert np.all(out[:,0::2]==0) and out[160,190*2+1]==255 and out[0,1]==0
    measure.append(int(out[160,120*2+1]));assert measure[-1]>0
assert measure[0]<measure[1]<measure[2],measure
# Bright areas below the old cutoff should now glow on Mid and High.
source[96:224,128*2+1:256*2:2]=188
source_bytes=source.tobytes()
assert actual(source_bytes,w,h,w*2,1)==source_bytes
assert actual(source_bytes,w,h,w*2,2)!=source_bytes
assert actual(source_bytes,w,h,w*2,3)!=source_bytes
# Full G12 field, including exact bytes and the progress callback sequence.
w,h,stride=3648,2736,7296
source=np.zeros((h,stride),dtype=np.uint8);source[:,1::2]=45;source[800:1600,1400*2+1:2000*2:2]=255
result=actual(source.tobytes(),w,h,stride,3,True)
assert result==reference(source.tobytes(),w,h,stride,3)
report={'workspace_3648_bytes':lib.film_bloom_work_size(3648),'reference':'Exact bytes against independent full-grid triangular convolution, bilinear upsampling and white-screen blend at six geometries/all levels','full_frame':'3648x2736 exact reference, canaries and 86 ordered progress callbacks pass','edge_luma_by_level':measure,'neutral_tones':'Black/midtones/white unchanged without nearby highlights; neutral remains neutral','hardware_timing':'not measured'}
(lab/'validation/bloom-kernel-v0.8.1.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
