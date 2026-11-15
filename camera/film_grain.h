/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Bounded monochrome grain on signed UYVY422. */
#ifndef FILM_GRAIN_H
#define FILM_GRAIN_H
#include <stddef.h>
size_t film_grain_work_size(unsigned width);
unsigned film_grain_amplitude(unsigned level);
/* Frame size relative to L (Q16, 65536 = L; set by the stage for M1, M2 and
 * S): the grain matches L's shrunk to this size. Taken when a pass starts. */
void film_grain_scale(unsigned scale);
/* State for a top-to-bottom pass over complete UYVY rows. */
typedef struct {
 signed char *upper,*lower;
 unsigned columns,height,row,coarse,fine;
 unsigned short weights[256];
} film_grain_rows;
int film_grain_begin_rows(film_grain_rows *state,unsigned width,unsigned height,
 unsigned level,unsigned seed,void *work,size_t work_size);
void film_grain_finish_row(unsigned char *pixels,void *state);
int film_uyvy_grain(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned level,unsigned seed,void *work,size_t work_size,
 void (*progress)(unsigned,unsigned));
#endif
