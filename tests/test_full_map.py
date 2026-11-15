# SPDX-License-Identifier: GPL-2.0-or-later
"""Full G12 geometry mapping, canaries and progressive callback publication."""
import ctypes as C,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
subprocess.run(['cc','-shared','-fPIC','-O2','camera/film_colour.c','camera/film_retro.c','camera/film_toy.c','camera/film_print.c','-o','build/colour.dylib'],cwd=ROOT,check=True)
lib=C.CDLL(str(ROOT/'build/colour.dylib'));CB=C.CFUNCTYPE(None,C.c_uint,C.c_uint)
args=[C.c_void_p,C.c_size_t]+[C.c_uint]*7+[C.c_void_p]
lib.film_uyvy_patch.argtypes=args;lib.film_uyvy_map.argtypes=args+[CB]
raw=(ROOT/'luts/GOLD17.LUT').read_bytes();lut=C.create_string_buffer(raw)
lib.film_lut_prepare.argtypes=[C.c_void_p,C.c_size_t];assert lib.film_lut_prepare(lut,len(raw))==0
# Four representative signed UYVY pairs, including extremes.
pattern=bytes([0,0,0,255,213,76,127,76,127,100,128,200,40,50,20,220])
small=C.create_string_buffer(pattern)
assert lib.film_uyvy_patch(small,16,8,1,16,0,0,8,1,lut)==0
expected=small.raw[:16];size=3648*2736*2
mem=C.create_string_buffer(b'X'*32+pattern*(size//16)+b'Z'*32)
ptr=C.cast(C.byref(mem,32),C.c_void_p);rows=[]
@CB
def progress(row,total):
 assert total==2736 and (not rows or row>rows[-1]);rows.append(row)
 # Only completed rows are reported; the last pair in that row is mapped.
 assert C.string_at(ptr.value+row*7296-16,16)==expected
assert lib.film_uyvy_map(ptr,size,3648,2736,7296,0,0,3648,2736,lut,progress)==0
assert rows==list(range(32,2736,32))+[2736]
assert mem.raw[:32]==b'X'*32 and mem.raw[size+32:size+64]==b'Z'*32
assert mem.raw[32:32+size]==expected*(size//16)
print('Full 3648x2736 UYVY map: all pixels match pair reference, 86 ordered progress callbacks, completed-row publication and image canaries passed')
