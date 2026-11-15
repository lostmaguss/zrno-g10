# SPDX-License-Identifier: GPL-2.0-or-later
"""Run from the repository root with G12_WORKSPACE=$PWD:
  work/test-venv/bin/python tools/speed_model/exact_toy.py REFERENCE_CAMERA_DIR
Byte-exactness of the toy lens against a reference camera/ copy: levels
1-3, graded and CPU-LUT passes, with and without halation and leaks, four
full frames, plus narrow and odd frames and direct film_toy_row calls in
uneven spans."""
import ctypes as C,sys,subprocess,hashlib,numpy as np
from pathlib import Path
sys.path.insert(0,'tools/speed_model')
import exact_halation as eh
W,H=eh.W,eh.H
ref=eh.lib(Path(sys.argv[1] if len(sys.argv)>1 else sys.exit("usage: exact_toy.py REFERENCE_CAMERA_DIR")),'toy-ref');new=eh.lib(Path('camera'),'toy-new')
for L in (ref,new):
    eh.setup(L)
    L.film_toy_begin.argtypes=[C.c_void_p,C.c_void_p,C.c_uint,C.c_uint,C.c_uint,C.c_uint,C.c_void_p,C.c_size_t]
    L.film_toy_row.argtypes=[C.c_void_p,C.c_uint,C.c_void_p,C.c_uint,C.c_uint]
f400,keep=eh.tint('F400H')
luts={}
raw=Path('card/CHDK/FILM/LUTS/PORTRA.LUT').read_bytes()
for i,L in enumerate((ref,new)):
    b=C.create_string_buffer(raw,len(raw));assert L.film_lut_prepare(b,len(raw))==0;luts[i]=b
def run(L,i,frame,w,h,toy,mode,leaks):
    img=C.create_string_buffer(frame,len(frame));work=C.create_string_buffer(max(1,L.film_halo_work_size(w)));sc=C.create_string_buffer(16384)
    tw=L.film_toy_work_size(w);toyw=C.create_string_buffer(tw)
    if mode=='graded':rc=L.film_uyvy_graded_retro(img,len(frame),w,h,w*2,0,3,C.byref(f400),work,len(work),sc,16384,None,1,leaks,0x1234,toy,toyw,tw,None)
    elif mode=='graded0':rc=L.film_uyvy_graded_retro(img,len(frame),w,h,w*2,0,0,C.byref(f400),None,0,sc,16384,None,0,leaks,0x1234,toy,toyw,tw,None)
    else:rc=L.film_uyvy_map_cached_retro(img,len(frame),w,h,w*2,luts[i],sc,16384,None,0,leaks,0x1234,toy,toyw,tw,None)
    assert rc==0,(mode,rc);return hashlib.sha256(img.raw).hexdigest()
bad=n=0
for fname,frame in eh.frames():
    for toy in (1,2,3):
        for mode,leaks in (('graded',1),('graded0',0),('plain',2)):
            a=run(ref,0,frame,W,H,toy,mode,leaks);b=run(new,1,frame,W,H,toy,mode,leaks);n+=1
            if a!=b:bad+=1;print('BAD',fname,toy,mode,flush=True)
            if hasattr(new,'film_colour_rows_in_place'):        # rows processed in place (FRAMECACHE)
                new.film_colour_rows_in_place.argtypes=[C.c_int];new.film_colour_rows_in_place(1)
                c=run(new,1,frame,W,H,toy,mode,leaks);new.film_colour_rows_in_place(0);n+=1
                if a!=c:bad+=1;print('BAD in place',fname,toy,mode,flush=True)
    print('ok',fname,flush=True)
rng=np.random.default_rng(5)
for w in (2,6,16,30,34,64,66,130,250,1000):
    for h in (1,2,7,33,65):
        fr=rng.integers(0,256,w*h*2,dtype=np.uint8).tobytes()
        for toy in (1,2,3):
            outs=[]
            for i,L in enumerate((ref,new)):
                img=C.create_string_buffer(fr,len(fr));tw=L.film_toy_work_size(w);toyw=C.create_string_buffer(max(1,tw))
                st=C.create_string_buffer(512);src=C.create_string_buffer(fr,len(fr))
                rc=L.film_toy_begin(st,src,w*2,w,h,toy,toyw,tw)
                if rc==1:
                    for y in range(h):
                        rowp=C.addressof(img)+y*w*2;col=0;step=[2,6,14,18][y%4]
                        while col<w:
                            sp=min(step,w-col);L.film_toy_row(st,y,rowp+col*2,col,sp);col+=sp
                outs.append((rc,img.raw))
            n+=1
            if outs[0]!=outs[1]:bad+=1;print('BAD narrow',w,h,toy,outs[0][0],outs[1][0],flush=True)
print(f'{n} cases, {bad} differ');sys.exit(1 if bad else 0)
