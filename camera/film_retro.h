/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Retro lens and film effects on signed UYVY422 rows:
 * vignette (circular lens falloff) and light leaks (warm edge fogging).
 * Applied to graded rows inside the colour pass, before Bloom and grain. */
#ifndef FILM_RETRO_H
#define FILM_RETRO_H
#include <stddef.h>
#define FILM_RETRO_MAX_LEAKS 2
typedef struct {
 unsigned edge,strength;              /* edge 0 left, 1 right, 2 top, 3 bottom */
 unsigned center,half,depth;          /* along-edge centre/half-width, across depth, in pixels */
 int y,u,v;                           /* leak colour: luma 0..255, signed chroma */
} film_retro_leak;
typedef struct {
 unsigned width,height,pairs,vignette,leaks,mono,count;
 unsigned long long diag2;            /* (W/2)^2+(H/2)^2, the corner distance squared */
 unsigned char *nx2;                  /* per pixel pair: 255*dx^2/diag2 */
 unsigned short *gain;                /* 256-entry vignette gain, Q12 */
 unsigned char *column[FILM_RETRO_MAX_LEAKS]; /* per pixel pair leak profile 0..255 */
 unsigned lo[FILM_RETRO_MAX_LEAKS],hi[FILM_RETRO_MAX_LEAKS]; /* pairs where each profile is non-zero */
 film_retro_leak leak[FILM_RETRO_MAX_LEAKS];
} film_retro;
/* Workspace bytes for a frame width; 0 for an invalid width. */
size_t film_retro_work_size(unsigned width);
/* Levels are 0..3 (Off/Low/Mid/High). Returns 1 when an effect is active,
 * 0 when both are off, and -1 for invalid geometry, levels or workspace.
 * The seed varies the light leaks per shot; mono keeps leaks neutral. */
int film_retro_begin(film_retro *r,unsigned width,unsigned height,unsigned vignette,
 unsigned leaks,unsigned seed,int mono,void *work,size_t work_size);
/* Apply to one span of a graded row: pixels start at even column start_col
 * and cover span (even) columns. Spans of one row may be applied separately. */
void film_retro_row(const film_retro *r,unsigned row,unsigned char *pixels,
 unsigned start_col,unsigned span);
#endif
