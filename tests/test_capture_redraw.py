# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual SpyTask redraw block with Canon capture active.

The v0.1.10 block fails the active-capture assertion, reproducing the camera's
stale 0% banner. This checks dispatch only; camera display refresh still needs
a hardware test. Pass a modified CHDK source root as the sole argument.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source = (Path(sys.argv[1]) / 'core/main.c').read_text()
start = source.index('        if ((camera_info.state.state_shooting_progress != SHOOTING_PROGRESS_PROCESSING) || recreview_hold)')
end = source.index('        if (camera_info.state.state_shooting_progress != SHOOTING_PROGRESS_PROCESSING)\n        {\n            if (conf.show_histo)', start)
block = source[start:end]
harness = r'''
#include <assert.h>
#include <stdio.h>
enum { SHOOTING_PROGRESS_PROCESSING=1, SHOOTING_PROGRESS_DONE=2 };
static struct { struct { int state_shooting_progress; } state; } camera_info;
static int recreview_hold, busy, normal_draws, film_draws;
static unsigned cnt;
static int film_ui_busy(void) { return busy; }
static void film_ui_draw(void) { film_draws++; }
static void gui_redraw(void) { normal_draws++; }
static void tick(void) {
BLOCK
}
static void run(int processing, int review, int active, int expected_normal, int expected_film) {
 camera_info.state.state_shooting_progress=processing ? SHOOTING_PROGRESS_PROCESSING : SHOOTING_PROGRESS_DONE;
 recreview_hold=review; busy=active; cnt=0; normal_draws=0; film_draws=0;
 for(int i=0;i<100;i++) tick();
 assert(normal_draws==expected_normal);
 assert(film_draws==expected_film);
}
int main(void) {
 (void)film_ui_busy; (void)film_ui_draw; /* Also compile the old suppressed path. */
 run(1,0,1,0,25); /* Native capture: only the overlay, at ordinary cadence. */
 run(1,0,0,0,0);  /* Other Canon captures retain normal redraw suppression. */
 run(1,1,1,25,0); /* Review hold keeps its original normal-GUI path. */
 run(1,1,0,25,0);
 run(0,0,1,25,0); /* Ordinary redraw already includes the Zrno overlay. */
 run(0,0,0,25,0);
 run(0,1,1,25,0);
 run(0,1,0,25,0);
 puts("SpyTask redraw: active capture overlay dispatched 25/100 ticks; normal GUI, review hold and inactive capture behavior retained");
}
'''.replace('BLOCK', block)
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'redraw.c').write_text(harness)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(path / 'redraw.c'), '-o', str(path / 'redraw')], check=True)
    subprocess.run([str(path / 'redraw')], check=True)
