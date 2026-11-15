/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Toy-camera lens on signed UYVY422 input rows: soft
 * corners and lateral chromatic aberration, with a sharp centre. Applied to
 * each input row before the LUT, like a lens. */
#ifndef FILM_TOY_H
#define FILM_TOY_H
#include <stddef.h>
#define FILM_TOY_MIN_CELL 32u     /* smallest grid cell (sizes the workspace) */
#define FILM_TOY_RING 6u          /* grid rows kept: B-spline taps j-1..j+2 for shifts under one cell */
#define FILM_TOY_CA_ROWS 10u      /* original red-plane rows cached, by row number */
typedef struct {
 const unsigned char *image;     /* frame being processed; rows ahead must be untouched */
 unsigned width,height,stride,level,pairs,blocks,gw,gh,cell,shift;
 unsigned bshift;                /* log2 of pixel pairs per sample block (3 at L) */
 unsigned long long diag2;
 int aligned;
 unsigned built,smoothed;        /* raw and smoothed grid rows done so far */
 unsigned ca_scale;              /* red sample position scale about the centre, Q16 (< 1: inward) */
 unsigned ca_base;               /* red source column of pixel 0, Q16; pixel x adds x*ca_scale */
 unsigned ca_tag[FILM_TOY_CA_ROWS]; /* row held in each cache slot (~0: none) */
 unsigned char *ca_ring;         /* FILM_TOY_CA_ROWS x width original red plane, slot = row % rows */
 const unsigned char *ca_clamp;  /* 0..255 clamp for -180..434 */
 unsigned char *ca_line;         /* red plane at the current row's source row (width+1) */
 unsigned char *raw_y,*ring_y;   /* FILM_TOY_RING x gw cell averages: raw, then smoothed */
 signed char *raw_u,*raw_v,*ring_u,*ring_v;
 unsigned short *nx2;            /* per block: 255*dx^2/diag2 at its centre, Q8 */
 unsigned short *soft;           /* 256-entry blur weights, Q8 */
 unsigned short *xs;             /* per knot x positions in the blur grid (Q4): centre, red source */
 short *lines;                   /* Y, U, V, then Y and V at the red source row, x (gw+3), x16, padded */
 unsigned line_row;              /* row the lines were made for (+1; 0 = none) */
 unsigned ny2;
 unsigned char *dither;          /* 256 fixed random rounding offsets */
} film_toy;
size_t film_toy_work_size(unsigned width);
/* Frame size relative to L (Q16, 65536 = L; set by the stage for M1, M2 and
 * S): the blur cells shrink with it. Taken by film_toy_begin. */
void film_toy_scale(unsigned scale);
/* Level 0..3 (Off/Low/Mid/High). Returns 1 when active, 0 when off, -1 for
 * invalid geometry, level or workspace. image/stride describe the frame the
 * grid is read from; rows below the current one must stay untouched until
 * film_toy_row has passed them. */
int film_toy_begin(film_toy *t,const unsigned char *image,unsigned stride,unsigned width,
 unsigned height,unsigned level,void *work,size_t work_size);
/* Apply to a span of input row `row` (even start_col and span). Rows must be
 * visited in increasing order; spans of one row may be applied separately. */
void film_toy_row(film_toy *t,unsigned row,unsigned char *pixels,unsigned start_col,unsigned span);
#endif
