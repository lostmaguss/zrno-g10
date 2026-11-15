/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Print on the ARM946E-S: print a strip of an L frame (3648 wide) with
 * the tables at DATA, for tests/bench_riso_arm.py. */
#include "film_riso.h"
#define SW 3648
#ifndef FLAWS
#define FLAWS 3
#endif
static unsigned char work[200 * 1024];
static film_riso_settings s;
/* inks: a Riso ink set, or 100 + style * 8 + palette for the other styles
 * (with their default noise, register and flaws). */
void strip(unsigned rows, unsigned inks, unsigned char *frame, const unsigned char *data)
{
    static const unsigned char rows_of[7][3] = {{2, 2, 3}, {0, 2, 0}, {0, 0, 0}, {1, 2, 1}, {3, 0, 2}, {0, 1, 0}, {0, 0, 0}};
    s.inks = (unsigned char)(inks < 100 ? inks : 0); s.separation = 0; s.screen = 0; s.noise = 2; s.reg = 2; s.flaws = FLAWS; s.paper = 0;
    s.style = 0; s.variant = 0;
    if (inks >= 100) {
        s.style = (unsigned char)((inks - 100) >> 3); s.variant = (unsigned char)((inks - 100) & 7);
        s.noise = rows_of[s.style][0]; s.reg = rows_of[s.style][1]; s.flaws = rows_of[s.style][2];
    }
    film_riso_render(frame, SW, rows, SW * 2, data, &s, 1234, work, sizeof(work), 0);
}
