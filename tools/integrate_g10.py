#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Apply Magus, the G10 1.03B port of Zrno, to an unmodified CHDK r6355 tree.

Always: the develop-task copy (camera/film_reloc.c with MAGUS_G10), started
by the G10's task-creation hook instead of Canon's DevelopModule task
(0xff8b1c8c). On a ROM whose task hash differs, the copy is refused and
Canon's own task runs. See docs/PORT-G10.md.

Usage: integrate_g10.py TREE [--magus | --square | --grain]
  --magus   Magus: looks, halation, Bloom and grain, with a Magus menu
            (camera/g10_magus.c)
  (none)    observe only: the develop task's messages are logged
  --square  test build: invert a 256 x 256 square in each L photo
  --grain   test build: Zrno's grain (High) on each L photo
"""
from pathlib import Path
import shutil, sys

root = Path(sys.argv[1]).resolve()
lab = Path(__file__).resolve().parents[1]
sub = 'platform/g10/sub/103b'
if not (root / sub / 'boot.c').is_file():
    raise SystemExit(f'{root / sub}: no G10 1.03B port')
magus, grain = '--magus' in sys.argv, '--grain' in sys.argv


def edit(name, old, new):
    p = root / name
    s = p.read_text()
    if s.count(old) != 1:
        raise SystemExit(f'{name}: expected exactly one patch location')
    p.write_text(s.replace(old, new))


def g10_copy(name, folder):
    """A camera file for this tree: .c files get MAGUS_G10 defined first."""
    text = (lab / 'camera' / name).read_text(encoding='utf-8')
    if name.endswith('.c'):
        text = '#define MAGUS_G10 1\n' + text
    (root / folder / name).write_text(text, encoding='utf-8')


ARM = '\t$(CC) $(CFLAGS) -O2 -finline -marm -nostdinc -fno-builtin {extra}-c -o $@ $<\n'
objs = ['film_reloc']
kernels = []                                  # built as ARM code at -O2, as the G12 build does
if magus:
    objs += ['g10_magus']
    kernels = ['film_colour', 'film_retro', 'film_toy', 'film_print', 'film_bloom', 'film_grain']
    for h in ['film_colour.h', 'film_retro.h', 'film_toy.h', 'film_print.h', 'film_font.h', 'film_bloom.h',
              'film_grain.h', 'film_compact.h', 'film_width.h']:
        g10_copy(h, 'include')
else:
    objs += ['g10_observe']
    if grain:
        kernels = ['film_grain']
        for h in ['film_grain.h', 'film_width.h']:
            g10_copy(h, 'include')
for name in objs + kernels:
    g10_copy(name + '.c', 'core')
for flag, define in (('--square', 'MAGUS_G10_SQUARE'), ('--grain', 'MAGUS_G10_GRAIN')):
    if flag in sys.argv and not magus:
        o = (root / 'core/g10_observe.c').read_text(encoding='utf-8')
        (root / 'core/g10_observe.c').write_text(f'#define {define} 1\n' + o, encoding='utf-8')
g10_copy('film_stage.h', 'include')
h = (lab / 'camera/film_reloc.h').read_text()
(root / 'include/film_reloc.h').write_text(h.replace('#define FILM_RELOC_H', '#define FILM_RELOC_H\n#define MAGUS_G10 1', 1))

edit('lib/core/Makefile', '$Ousb_remote.o $Oautoiso.o',
     '$Ousb_remote.o $Oautoiso.o ' + ' '.join('$O' + o + '.o' for o in objs + kernels))
rules = ''.join(f'\n$O{k}.o: $(core)/{k}.c\n' + ARM.format(extra='-DFILM_LAB_CAMERA ' if k == 'film_colour' else '')
                for k in kernels)
if rules:
    edit('lib/core/Makefile', 'all: $Olibcore.a', 'all: $Olibcore.a\n' + rules)

HOOK = '    if (p[0]==(int)0xff8b1c8c) { void *e=film_develop_relocate(); if (e) p[0]=(int)e; }  // Magus G10\n'
edit(f'{sub}/boot.c', 'void taskCreateHook(int *p) {\n    p-=17;\n',
     '#include "film_reloc.h"\nvoid taskCreateHook(int *p) {\n    p-=17;\n' + HOOK)
edit(f'{sub}/boot.c', 'void taskCreateHook2(int *p) {\n    p-=17;\n',
     'void taskCreateHook2(int *p) {\n    p-=17;\n' + HOOK)

if magus:
    edit('core/main.c', '        mode_get();\n', '        mode_get();\n        { extern void g10_magus_poll(void); g10_magus_poll(); }  // Magus G10\n')
    edit('core/gui.c', 'static CMenuItem root_menu_items[] = {\n',
         'extern CMenu magus_g10_menu;\nstatic CMenuItem root_menu_items[] = {\n'
         '    MENU_ITEM   (0x5c,(int)"Magus",                      MENUITEM_SUBMENU,   &magus_g10_menu, 0 ),\n')
print('G10', 'Magus' if magus else 'test' if grain or '--square' in sys.argv else 'observe', 'build applied to', root)
