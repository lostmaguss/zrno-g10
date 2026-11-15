#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Apply Zrno to an unmodified CHDK release-1_6 r6355 source tree (G12 only).

Usage: integrate.py TREE [FW]. FW is the G12 firmware sub-port to patch
(platform/g12/sub/FW, default 100g, the only one Zrno is tested on; see
tools/firmware_variants.py)."""
from pathlib import Path
import os,shutil,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from menu_patch import menu_edits
from osd_patch import osd_edits
root=Path(sys.argv[1]).resolve()
fw=sys.argv[2] if len(sys.argv)>2 else '100g'
sub=f'platform/g12/sub/{fw}'
if not (root/sub/'makefile.inc').is_file(): raise SystemExit(f'{root/sub}: no such G12 firmware port')
lab=Path(__file__).resolve().parents[1]
def edit(name,old,new):
    p=root/name;s=p.read_text()
    if s.count(old)!=1: raise SystemExit(f'{name}: expected exactly one patch location')
    p.write_text(s.replace(old,new))
shutil.copy2(lab/'camera/film_queue.h',root/'include/film_queue.h')
shutil.copy2(lab/'camera/film_queue.c',root/'core/film_queue.c')
shutil.copy2(lab/'camera/film_ui.c',root/'core/film_ui.c')
shutil.copy2(lab/'camera/film_ui.h',root/'include/film_ui.h')
for name in ['film_raw.c','film_stage.c','film_colour.c','film_retro.c','film_toy.c','film_print.c','film_lut.c','film_grain.c','film_bloom.c','film_menu.c','film_hwcolour.c','film_gboy.c','film_gbui.c','film_riso.c','film_rsui.c','film_grade.c','film_grui.c','film_reloc.c']:shutil.copy2(lab/'camera'/name,root/'core'/name)
for name in ['film_raw.h','film_pipe.h','film_stage.h','film_colour.h','film_retro.h','film_toy.h','film_print.h','film_font.h','film_clock.h','film_lut.h','film_grain.h','film_bloom.h','film_menu.h','film_hwcolour.h','film_compact.h','film_gboy.h','film_gbui.h','film_riso.h','film_rsui.h','film_grade.h','film_grui.h','film_reloc.h']:shutil.copy2(lab/'camera'/name,root/'include'/name)
# FILM_HWMASK=1: the experimental hardware Bloom mask (see camera/film_bloom.h).
# FILM_DEVTRACE=1: research build logging the develop task's messages (film_stage.h).
if os.environ.get('FILM_DEVTRACE')=='1':edit('include/film_stage.h','#define FILM_STAGE_H','#define FILM_STAGE_H\n#define FILM_DEVTRACE 1')
if os.environ.get('FILM_HWMASK')=='1':edit('include/film_bloom.h','#define FILM_BLOOM_H','#define FILM_BLOOM_H\n#define FILM_HWMASK 1')
edit('lib/core/Makefile','$Ousb_remote.o $Oautoiso.o','$Ousb_remote.o $Oautoiso.o $Ofilm_queue.o $Ofilm_ui.o $Ofilm_raw.o $Ofilm_stage.o $Ofilm_colour.o $Ofilm_retro.o $Ofilm_toy.o $Ofilm_print.o $Ofilm_lut.o $Ofilm_grain.o $Ofilm_bloom.o $Ofilm_menu.o $Ofilm_hwcolour.o $Ofilm_gboy.o $Ofilm_gbui.o $Ofilm_riso.o $Ofilm_rsui.o $Ofilm_grade.o $Ofilm_grui.o $Ofilm_reloc.o')
edit('lib/core/Makefile','all: $Olibcore.a','''all: $Olibcore.a

$Ofilm_retro.o: $(core)/film_retro.c
\t$(CC) $(CFLAGS) -O2 -finline -marm -nostdinc -fno-builtin -c -o $@ $<

$Ofilm_toy.o: $(core)/film_toy.c
\t$(CC) $(CFLAGS) -O2 -finline -marm -nostdinc -fno-builtin -c -o $@ $<
''')
edit('lib/core/Makefile','all: $Olibcore.a','''all: $Olibcore.a

$Ofilm_bloom.o: $(core)/film_bloom.c
\t$(CC) $(CFLAGS) -O2 -finline -marm -nostdinc -fno-builtin -c -o $@ $<
''')
edit('lib/core/Makefile','all: $Olibcore.a','''all: $Olibcore.a

$Ofilm_grain.o: $(core)/film_grain.c
\t$(CC) $(CFLAGS) -O2 -finline -marm -nostdinc -fno-builtin -c -o $@ $<

$Ofilm_gboy.o: $(core)/film_gboy.c
\t$(CC) $(CFLAGS) -O2 -finline -marm -nostdinc -fno-builtin -c -o $@ $<

$Ofilm_riso.o: $(core)/film_riso.c
\t$(CC) $(CFLAGS) -O2 -finline -marm -nostdinc -fno-builtin -c -o $@ $<
''')
edit('lib/core/Makefile','all: $Olibcore.a','''all: $Olibcore.a

$Ofilm_colour.o: $(core)/film_colour.c
\t$(CC) $(CFLAGS) -O2 -finline -marm -nostdinc -fno-builtin -DFILM_LAB_CAMERA -c -o $@ $<
''')
edit('core/main.c','#include "platform.h"','#include "platform.h"\n#include "film_queue.h"\n#include "film_ui.h"')
edit('core/main.c','        mode_get();','        mode_get();\n        film_boot_poll();\n        film_ui_tick();')
edit('core/main.c','''        if (camera_info.state.state_shooting_progress != SHOOTING_PROGRESS_PROCESSING)
        {
            if (conf.show_histo)''','''        /* Canon capture suppresses the normal GUI redraw above. Keep only
         * the Zrno overlay alive, from SpyTask, during long grading. */
        if (camera_info.state.state_shooting_progress == SHOOTING_PROGRESS_PROCESSING &&
            !recreview_hold && film_ui_busy())
        {
            if (((cnt++) & 3) == 0)
                film_ui_draw();
        }

        if (camera_info.state.state_shooting_progress != SHOOTING_PROGRESS_PROCESSING)
        {
            if (conf.show_histo)''')
edit('core/raw.c','void raw_process(void)\n{','#include "film_queue.h"\n#include "film_raw.h"\nvoid raw_process(void)\n{\n    film_capture_latch();')
edit('core/raw.c','''#ifdef OPT_CURVES
    if (conf.curve_enable)
        libcurves->curve_apply();
#endif
}''','''#ifdef OPT_CURVES
    if (conf.curve_enable)
        libcurves->curve_apply();
#endif
    film_raw_process(rawadr);
}''')
edit('platform/generic/filewrite.c','int fwt_open(const char *name, int flags, int mode) {','#include "film_queue.h"\nint fwt_open(const char *name, int flags, int mode) {')
edit('platform/generic/filewrite.c','        return _Open(name, flags, mode);','        int fd = _Open(name, flags, mode);\n        film_opened(name, fd);\n        return fd;')
edit('platform/generic/filewrite.c','        return _Write(fd, buffer, nbytes);','        int ret = _Write(fd, buffer, nbytes);\n        film_write_result(fd, nbytes, ret);\n        return ret;')
edit('platform/generic/filewrite.c','        int ret = _Close(fd);','        int ret = _Close(fd);\n        film_closed(fd, ret);')
edit(f'{sub}/filewrite.c','"    BL      sub_FF873DD4 \\n"','"    BL      sub_FF873DD4 \\n"\n"    BL      film_special_closed \\n"')
# Text box: offer every printable ASCII character. The default keyboard
# lacks '*', '\\' and '|' and has a non-ASCII pound sign; the file is not UTF-8.
def edit_bytes(name,old,new):
    p=root/name;b=p.read_bytes()
    if b.count(old)!=1: raise SystemExit(f'{name}: expected exactly one patch location')
    p.write_bytes(b.replace(old,new))
edit_bytes('modules/gui_tbox.c',b'{".,:;?!","@#$%^&\xa3","()[]{}","<>\\"\'`~_"},\n        {0}\n    };\n\ncmap tbox_chars_german',
           b'{".,:;?!","@#$%^&*","()[]{}","<>\\"\'`~_\\\\|"},\n        {0}\n    };\n\ncmap tbox_chars_german')
# S button (CHDK's <ALT> button): open the Zrno menu straight
# away unless set to CHDK. MENU from there opens CHDK's main menu, as with
# CHDK's own "User Menu: On Direct"; Back returns to plain <ALT> mode.
edit('core/gui.c','''	    gui_user_menu_flag = 0;
	    if ((conf.user_menu_enable == 2) && !camera_info.state.state_kbd_script_run) {''','''	    gui_user_menu_flag = 0;
	    if (film_lut_s_opens_film_menu() && !camera_info.state.state_kbd_script_run) {
		    gui_menu_init(&film_lut_menu);
		    gui_set_mode(&menuGuiHandler);
		    gui_user_menu_flag = 1;
	    } else if ((conf.user_menu_enable == 2) && !camera_info.state.state_kbd_script_run) {''')

edit('core/gui.c','#include "platform.h"','#include "platform.h"\n#include "film_ui.h"\n#include "film_lut.h"')
edit('core/gui.c','static CMenuItem root_menu_items[] = {','''#include "film_menu.h"

static CMenuItem root_menu_items[] = {
    MENU_ITEM(0x28,"Zrno menu", MENUITEM_SUBMENU, &film_lut_menu, 0),''')
edit('core/gui.c','        gui_mode->redraw(flag_gui_enforce_redraw);','        gui_mode->redraw(flag_gui_enforce_redraw);\n    film_ui_draw();')
# CHDK's own shooting-screen OSD off by default, and the Zrno indicator drawn
# in its pass: see tools/osd_patch.py.
osd_edits(edit)
# CHDK's startup splash (a red box with CHDK's version, build and camera, over
# the shooting screen for a few seconds after boot): never shown. It is forced
# off rather than defaulted off, since a saved CHDK/CCHDK4.CFG keeps
# splash_show on; its Misc menu toggle goes with it.
edit('core/gui.c','    gui_splash = (conf.splash_show) ? SPLASH_TIME : 0;','    gui_splash = 0;')
edit('core/gui.c','    MENU_ITEM   (0x5c,LANG_MENU_MISC_SHOW_SPLASH,           MENUITEM_BOOL,      &conf.splash_show, 0 ),\n','')
edit('core/conf.c','#include "platform.h"','#include "platform.h"\n#include "film_ui.h"')
edit('core/conf.c','if(conf.alt_prevent_shutdown == ALT_PREVENT_SHUTDOWN_ALWAYS','if(film_ui_busy() || conf.alt_prevent_shutdown == ALT_PREVENT_SHUTDOWN_ALWAYS')

# Canon's develop task with Zrno's hooks: camera/film_reloc.c copies it from
# the camera's own ROM into RAM when the task starts (0 if the ROM is not
# G12 1.00G: the camera then runs Canon's own task, without Zrno's hooks).
edit(f'{sub}/boot.c','void taskHook(context_t **context)','#include "film_reloc.h"\nvoid taskHook(context_t **context)')
edit(f'{sub}/boot.c','// Replace firmware task addresses with ours','// Replace firmware task addresses with ours\n    if(tcb->entry == (void*)0xff8e6f3c){void *e=film_develop_relocate();if(e)tcb->entry=e;}')
# The start mode (shooting or gallery), as Canon's own sub_FF864BE0 decides
# it: the gallery only when the playback button's line (0xC022010C) reads
# pressed. CHDK's port takes "power button (0xC0220108) no longer pressed"
# to mean a playback start, but it reads it after Canon has loaded the whole
# DISKBOOT.BIN, and with Zrno's (about 300 KB) a normal press of the power
# button is over by then: the camera started in the gallery, where Canon's
# playback button switches a playback-started camera off.
edit(f'{sub}/boot.c','*(int*)(0x25E0) = (*(int*)0xC0220108)&1 ? 0x100000 : 0x200000;','*(int*)(0x25E0) = (*(volatile int*)0xC022010C)&1 ? 0x200000 : 0x100000;')
# The core outgrew the 200 KB AgentRAM block: CHDK
# loads into EXMEM instead (4 MB from exmem pool 0, at MEMISOSTART 0x79089e0),
# and AgentRAM becomes a CHDK heap. tools/exmem_boot_check.py checks it on the camera.
edit(f'{sub}/makefile.inc','# Force ARAM on for G12, and load CHDK in ARAM\noverride OPT_ARAM_MALLOC=1\noverride OPT_CHDK_IN_ARAM=1\n','# Zrno: CHDK loads into EXMEM (4 MB); AgentRAM becomes a CHDK heap\noverride OPT_ARAM_MALLOC=1\noverride OPT_EXMEM_MALLOC=1\noverride OPT_CHDK_IN_EXMEM=1\n')
# Zrno menu renderer (camera/film_menu.c): see tools/menu_patch.py.
menu_edits(edit)
# Re-develop (camera/film_redev.c, built as the CHDK module redev.flt): the film
# code loads a sensor backup over the next capture and grades it without a new
# backup. The module reaches the core through three exports.
shutil.copy2(lab/'camera/film_redev.c',root/'modules/film_redev.c')
(root/'modules/makefile_usermodules.inc').write_text('FLTS+=$Oredev.flt\n$Oredev.flt: $Ofilm_redev.o\n')
edit('modules/module_exportlist.c','            raw_prepare_develop\n','            raw_prepare_develop\n            film_raw_redevelop\n            film_ui_busy\n            &film_stage_probe\n')
print('Integrated Zrno into',root)
