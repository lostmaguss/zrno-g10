#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build and exercise native Zrno on the host; requires numpy, Pillow and capstone."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

lab=Path(__file__).resolve().parents[1]
(lab/'build').mkdir(exist_ok=True);(lab/'validation').mkdir(exist_ok=True)
def run(args):
    print('+ '+' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),cwd=lab,check=True)

# gcc (Linux CI) has warnings clang lacks, and they misfire on this code:
# strncpy into fixed-width, zero-padded text fields truncates on purpose
# (-Wstringop-truncation); several statements share a line, as in the clamps
# "if(v>255)v=255;if(v<0)v=0;" (-Wmisleading-indentation); and sprintf of the
# bounded backup ids 1..10000 into 24-byte names is flagged for ids gcc cannot
# rule out (-Wformat-overflow). clang rejects these flags, so only gcc gets them.
GCC='clang' not in subprocess.run(['cc','--version'],capture_output=True,text=True).stdout
GCC_ONLY=['-Wno-stringop-truncation','-Wno-misleading-indentation','-Wno-format-overflow'] if GCC else []
# With -fsanitize=undefined, gcc's shift check bounds a loop index only by the
# shift width, so "for(k=0;k<n;k++)...<<k" over 3-ink arrays is reported as
# index 32 (film_riso.c; n<=3 is checked when PRINT.BIN loads). Sanitized gcc
# builds only; the other builds keep -Warray-bounds.
GCC_SANITIZE=['-Wno-array-bounds'] if GCC else []

# AddressSanitizer checks for leaks at exit on Linux, not on macOS. Zrno's
# buffers live until the camera powers off, and the tests start the stage again
# and again as the camera does after a restart, so every test would "leak".
# Leaks stay unchecked on both, unless ASAN_OPTIONS is set by the caller.
os.environ.setdefault('ASAN_OPTIONS','detect_leaks=0')

def ctest(name,includes=(),extra=(),sanitize=False,args=()):
    out='build/'+name
    run(['cc','-O2','-std=gnu99','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-int-to-pointer-cast']+GCC_ONLY+
        (['-fsanitize=address,undefined','-fno-omit-frame-pointer']+GCC_SANITIZE if sanitize else [])+
        ['-I'+p for p in includes]+['tests/'+name+'.c']+list(extra)+['-lm','-o',out])   # libm: separate on Linux, in libSystem on macOS
    run([str((lab/out).resolve())]+list(args))

run([sys.executable,'tools/build_lut_pack.py'])
ctest('test_queue',['tests/stubs','camera'])
ctest('test_ui',['tests/ui_stubs','camera'])
with tempfile.TemporaryDirectory() as tmp:
    ctest('test_lut_menu',['tests/lut_stubs','tests/native_stubs','camera'],['camera/film_colour.c','camera/film_retro.c','camera/film_toy.c','camera/film_print.c','camera/film_grain.c','camera/film_bloom.c','camera/film_grade.c'],args=[tmp,str(lab/'card/CHDK/FILM')])
ctest('test_raw_native',['tests/native_stubs','camera'],sanitize=True)
ctest('test_stage',['tests/native_stubs','camera'],sanitize=True)
ctest('test_pipe',['camera'])
ctest('test_hwcolour',['camera'],sanitize=True)
ctest('test_halo_bounds',['camera'],['camera/film_colour.c','camera/film_retro.c','camera/film_toy.c','camera/film_print.c'],True,args=['luts/GOLD17.LUT'])
ctest('test_bloom_bounds',['camera'],['-DFILM_HWMASK','camera/film_bloom.c','camera/film_grain.c'],True)
ctest('test_grain_bounds',['camera'],['camera/film_grain.c'],True)
ctest('test_retro',['camera'],['camera/film_retro.c','camera/film_colour.c','camera/film_toy.c','camera/film_print.c'],True,args=['card/CHDK/FILM/LUTS/PORTRA.LUT'])
ctest('test_toy',['camera'],['camera/film_toy.c','camera/film_print.c','camera/film_colour.c','camera/film_retro.c'],True,args=['card/CHDK/FILM/LUTS/PORTRA.LUT'])
ctest('test_print',['camera'],['camera/film_print.c','camera/film_colour.c','camera/film_retro.c','camera/film_toy.c'],True,args=['card/CHDK/FILM/LUTS/PORTRA.LUT'])
with tempfile.TemporaryDirectory() as tmp:
    ctest('test_redev',[],sanitize=True,args=[tmp])
ctest('test_soft',['camera'],['camera/film_bloom.c'],True)
ctest('test_bloom_table',['camera'],['camera/film_bloom.c'],True)
ctest('test_halo_fast',['camera'],['camera/film_colour.c','camera/film_retro.c','camera/film_toy.c','camera/film_print.c'],True,args=['luts/GOLD17.LUT'])
ctest('test_gboy',['camera'],['camera/film_gboy.c'],True)
ctest('test_riso',['camera'],['camera/film_riso.c'],True,args=['card/CHDK/FILM/PRINT.BIN'])
ctest('test_grade',['camera'],['camera/film_grade.c'],True,args=[a for a in [os.environ.get('ZRNO_ROM','rom/PRIMARY.BIN')] if (lab/a).exists()])
ctest('test_graded',['camera'],['camera/film_colour.c','camera/film_retro.c','camera/film_toy.c','camera/film_print.c'],True)
for name in ['test_uyvy_lut','test_full_map','test_halation','test_grain','test_lut_pack','test_warm','test_installer','test_bloom','test_menu','test_hw_table','test_reloc']:
    run([sys.executable,'tests/'+name+'.py'])
print('All native host test groups passed.',flush=True)
