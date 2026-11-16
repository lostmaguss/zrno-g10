/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Shared LUT arithmetic and signed UYVY422 mapping. */
#ifndef FILM_COLOUR_H
#define FILM_COLOUR_H
#include <stddef.h>
#include "film_print.h"
int film_lut_validate(const void *,size_t);
/* Validate a G12LUT1 file image, then prepare it in place for the mapping
 * kernels: nodes are biased for signed DSP multiplies and monochrome LUTs are
 * flagged. The checksum no longer applies afterwards. Kernels reject LUTs
 * that have not been prepared. */
int film_lut_prepare(void *,size_t);
/* 1: the passes work on the frame rows directly instead of through the
 * scratch row (the frame is in cached memory); 0: copy (default). */
void film_colour_rows_in_place(int on);
/* Fast halation (FASTHALO.TXT): the mask from the top row of each 2x2 block.
 * Approximate; taken when each pass starts. */
void film_halo_fast(int on);
/* Coarse halation (the G10, docs/PORT-G10.md): the glow mask in 4x4-pixel cells
 * with half the box, interpolated back per pixel pair. About the same glow,
 * a quarter of the mask work. Taken when each pass starts. */
void film_halo_coarse(int on);
/* Frame size relative to L (Q16, 65536 = L; set by the stage for M1, M2 and
 * S): the glow's radius shrinks with it. Taken when each pass starts. */
void film_halo_scale(unsigned scale);
/* Reference interpolation for a validated file image or a prepared LUT. */
void film_lut_pixel(const void *,const unsigned char[3],unsigned char[3]);
int film_uyvy_patch(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned x,unsigned y,unsigned w,unsigned h,const void *lut);
int film_uyvy_map(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned x,unsigned y,unsigned w,unsigned h,const void *lut,
 void (*progress)(unsigned,unsigned));
size_t film_halo_work_size(unsigned width);
unsigned film_halo_radius(unsigned level);
unsigned film_halo_strength(unsigned level);
size_t film_halo_level_work_size(unsigned width,unsigned level);
int film_uyvy_map_halo_level(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,const void *lut,unsigned level,void *work,size_t work_size,
 void (*progress)(unsigned,unsigned));
int film_uyvy_map_halo(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,const void *lut,void *work,size_t work_size,
 void (*progress)(unsigned,unsigned));
/* Optional non-overlapping scratch, with room for the selected row in bytes. */
int film_uyvy_map_cached(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned x,unsigned y,unsigned w,unsigned h,const void *lut,
 unsigned char *scratch,size_t capacity,void (*progress)(unsigned,unsigned));
int film_uyvy_map_halo_cached(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,const void *lut,unsigned level,void *work,size_t work_size,
 unsigned char *scratch,size_t capacity,void (*progress)(unsigned,unsigned));
/* The same passes with retro effects. Toy lens (level 0..3, its own
 * workspace of film_toy_work_size bytes) softens and fringes each input row
 * before grading; vignette and light leaks apply to each graded row and need
 * the scratch row cache; the seed varies leaks and dust. Print effects (dust
 * and scratches, date stamp; NULL for none) follow on each graded row
 * and need no workspace. */
int film_uyvy_map_cached_retro(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,const void *lut,unsigned char *scratch,size_t capacity,
 void (*progress)(unsigned,unsigned),unsigned vignette,unsigned leaks,unsigned seed,
 unsigned toy,void *toy_work,size_t toy_size,const film_print_settings *print);
int film_uyvy_map_halo_cached_retro(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,const void *lut,unsigned level,void *work,size_t work_size,
 unsigned char *scratch,size_t capacity,void (*progress)(unsigned,unsigned),
 unsigned vignette,unsigned leaks,unsigned seed,unsigned toy,void *toy_work,size_t toy_size,
 const film_print_settings *print);
/* The same effects for a look in the look's hardware colour table (the DIGIC
 * colour engine), which Canon applies when it encodes the JPEG, after these
 * effects: the frame still has the scene's colours, also for a
 * black-and-white look. Toy lens, vignette and light leaks, print effects
 * (as above), and halation (level 1..3, 0 = off; its workspace as
 * film_halo_level_work_size and the tint are required). tint gives the
 * glow's colour, fitted as if added to the look's output: Q8
 * gains on the halation's red linear energy for the red, green and blue
 * outputs (today's ungraded halation is 256, 51, 0 before the LUT), and the
 * glow mask's highlight weight per graded luma (the look undone). Pixels
 * no effect touches keep their bytes; with nothing to do it returns at once. */
typedef struct {
 unsigned short red,green,blue;
 const unsigned char *highlight;   /* 256 highlight weights per graded luma, or 0 for today's */
} film_halo_tint;
int film_uyvy_graded_retro(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,int mono,unsigned level,const film_halo_tint *tint,void *work,size_t work_size,
 unsigned char *scratch,size_t capacity,void (*progress)(unsigned,unsigned),
 unsigned vignette,unsigned leaks,unsigned seed,unsigned toy,void *toy_work,size_t toy_size,
 const film_print_settings *print);
#endif
