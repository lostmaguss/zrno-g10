# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded signed UYVY -> original 3D LUT -> UYVY reference comparison."""
from pathlib import Path
import ctypes as C,subprocess,random,sys
ROOT=Path(__file__).resolve().parents[1]
subprocess.run(['cc','-shared','-fPIC','-O2','-Wall','-Wextra','camera/film_colour.c','camera/film_retro.c','camera/film_toy.c','camera/film_print.c','-o','build/colour.dylib'],cwd=ROOT,check=True)
lib=C.CDLL(str(ROOT/'build/colour.dylib'))
lib.film_uyvy_patch.argtypes=[C.c_void_p,C.c_size_t]+[C.c_uint]*7+[C.c_void_p]
lib.film_lut_pixel.argtypes=[C.c_void_p,C.c_void_p,C.c_void_p]
lut=(ROOT/'luts/GOLD17.LUT').read_bytes();lb=C.create_string_buffer(lut)
lib.film_lut_validate.argtypes=[C.c_void_p,C.c_size_t];assert lib.film_lut_validate(lb,len(lut))==0
lib.film_lut_prepare.argtypes=[C.c_void_p,C.c_size_t];assert lib.film_lut_prepare(lb,len(lut))==0
# Prepared LUTs fail checksum validation and cannot be prepared twice.
assert lib.film_lut_validate(lb,len(lut))==-1 and lib.film_lut_prepare(lb,len(lut))==-1
random.seed(34);width,height,stride=64,31,140
data=bytes(random.randrange(256) for _ in range(stride*height));reference=bytearray(data)
def rnd(v):return int(v+.5) if v>=0 else -int(-v+.5)
def clip(v):return min(255,max(0,v))
def transform(y,u,v):
 rgb=[clip(rnd(y+1.402*v)),clip(rnd(y-.344136*u-.714136*v)),clip(rnd(y+1.772*u))]
 inp=(C.c_ubyte*3)(*rgb);out=(C.c_ubyte*3)();lib.film_lut_pixel(lb,inp,out);return list(out)
for y in range(7,19):
 for x in range(8,40,2):
  off=y*stride+x*2;u,y0,v,y1=data[off:off+4];u=u if u<128 else u-256;v=v if v<128 else v-256
  r0,g0,b0=transform(y0,u,v);r1,g1,b1=transform(y1,u,v)
  vals=[rnd(-.168736*(r0+r1)/2-.331264*(g0+g1)/2+.5*(b0+b1)/2),clip(rnd(.299*r0+.587*g0+.114*b0)),rnd(.5*(r0+r1)/2-.418688*(g0+g1)/2-.081312*(b0+b1)/2),clip(rnd(.299*r1+.587*g1+.114*b1))]
  reference[off:off+4]=bytes([min(127,max(-128,vals[0]))%256,vals[1],min(127,max(-128,vals[2]))%256,vals[3]])
mem=C.create_string_buffer(b'guard'*8+data+b'guard'*8);ptr=C.cast(C.byref(mem,40),C.c_void_p)
assert lib.film_uyvy_patch(ptr,len(data),width,height,stride,8,7,32,12,lb)==0
out=mem.raw[40:40+len(data)]
assert mem.raw[:40]==b'guard'*8 and mem.raw[40+len(data):40+len(data)+40]==b'guard'*8
maxdiff=0
for y in range(height):
 for k in range(stride):
  off=y*stride+k
  if 7<=y<19 and 16<=k<80:
   av=out[off];bv=reference[off]
   if k%2==0:av=av if av<128 else av-256;bv=bv if bv<128 else bv-256
   maxdiff=max(maxdiff,abs(av-bv))
  else:assert out[off]==data[off]
assert maxdiff<=2,maxdiff
before=mem.raw
for args in [(width,height,stride,7,7,32,12),(width,height,stride,8,7,33,12),(width,height,stride,8,30,32,2),(width,height,100,8,7,32,12)]:
 assert lib.film_uyvy_patch(ptr,len(data),*args,lb)==-1
 assert mem.raw==before
print('UYVY LUT: signed floating reference within 2 levels, pair-chroma averaging, ROI/margins/canaries and rejected bounds passed; max difference',maxdiff)

# Copy only the ROI span. Scratch and frame padding must stay bounded.
lib.film_uyvy_map_cached.argtypes=lib.film_uyvy_patch.argtypes+[C.c_void_p,C.c_size_t,C.c_void_p]
cached=C.create_string_buffer(b'guard'*8+data+b'guard'*8)
cached_ptr=C.cast(C.byref(cached,40),C.c_void_p)
scratch=C.create_string_buffer(b'A'*16+b'X'*64+b'Z'*16)
scratch_ptr=C.cast(C.byref(scratch,16),C.c_void_p)
geometry=(width,height,stride,8,7,32,12)
for pointer,capacity in ((scratch_ptr,63),(cached_ptr,64),(lb,64)):
 assert lib.film_uyvy_map_cached(cached_ptr,len(data),*geometry,lb,pointer,capacity,None)==-1
 assert cached.raw[40:40+len(data)]==data
assert lib.film_uyvy_map_cached(cached_ptr,len(data),*geometry,lb,scratch_ptr,64,None)==0
assert cached.raw==mem.raw
assert scratch.raw[:16]==b'A'*16 and scratch.raw[80:96]==b'Z'*16
print('Cached UYVY: exact ROI bytes, scratch guards and rejection of undersized or overlapping buffers passed')

# The prepared tables fit after the cached row. A misaligned address keeps
# the row cache usable and must leave its spare bytes untouched.
capacity=16384
for offset in (16,17):
 cached=C.create_string_buffer(b'guard'*8+data+b'guard'*8)
 cached_ptr=C.cast(C.byref(cached,40),C.c_void_p)
 scratch=C.create_string_buffer(b'X'*(capacity+32))
 scratch_ptr=C.cast(C.byref(scratch,offset),C.c_void_p)
 assert lib.film_uyvy_map_cached(cached_ptr,len(data),*geometry,lb,scratch_ptr,capacity,None)==0
 assert cached.raw==mem.raw
 assert scratch.raw[:offset]==b'X'*offset and scratch.raw[offset+capacity:]==b'X'*(32-offset)+b'\0'
 tail=scratch.raw[offset+64:offset+capacity]
 assert (tail==b'X'*len(tail)) == (offset==17)
print('Prepared colour tables: exact ROI bytes with aligned scratch and an unchanged scratch tail on misalignment')
