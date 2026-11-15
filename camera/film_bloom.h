/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Neutral highlight diffusion on signed UYVY422. */
#ifndef FILM_BLOOM_H
#define FILM_BLOOM_H
#include <stddef.h>
size_t film_bloom_work_size(unsigned width);
unsigned film_bloom_radius(unsigned level);
unsigned film_bloom_strength(unsigned level);
typedef struct {
 unsigned char *scratch;
 size_t capacity;
 void (*finish_row)(unsigned char *,void *);
 void *context;
} film_bloom_row_pass;
/* A row pass receives finished pixels before the row is copied to the image. */
int film_uyvy_bloom_rows(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned level,void *work,size_t work_size,
 const film_bloom_row_pass *pass,void (*progress)(unsigned,unsigned));
int film_uyvy_bloom(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned level,void *work,size_t work_size,
 void (*progress)(unsigned,unsigned));
/* FILM_HWMASK (off by default; tools/integrate.py sets it with
 * FILM_HWMASK=1): Bloom can take its highlight mask from the DIGIC resize
 * engine's 1/4 shrink, with CHDK/FILM/HWMASK.TXT on the card. Tested on the
 * camera: it works (80-100 ms) but saves no measurable time, so
 * it stays out of the normal build.
 *
 * The next Bloom reads its highlights from this 1/4-size UYVY image (width
 * and height (frame+3)/4) instead of the full frame; ignored if the sizes
 * don't match. Used once. */
#ifdef FILM_HWMASK
void film_bloom_use_quarter(const unsigned char *uyvy,unsigned stride,unsigned width,unsigned height);
#endif
/* An exact table for the chroma fade of Bloom's screen blend and soft
 * focus (faded(value, amount) for every amount and byte): film_bloom_fade_table_size()
 * bytes, built once by film_bloom_fade_table (about 64K entries), then used
 * by every Bloom until 0 is passed. The output is the same either way; the
 * table saves the arithmetic where much of the frame glows. */
size_t film_bloom_fade_table_size(void);
/* Fast Bloom (FASTBLOOM.TXT; not soft focus): the highlight mask from rows
 * 0 and 2 of each 4x4 cell, and one glow amount per pixel pair for luma.
 * Not the same bytes: within 1-2 levels of Bloom on real photos, about
 * 16-18% faster in the model. */
void film_bloom_fast(int on);
/* Frame size relative to L (Q16, 65536 = L; set by the stage for M1, M2 and
 * S): Bloom's and soft focus's radius shrink with it. Taken when a pass starts. */
void film_bloom_scale(unsigned scale);
void film_bloom_fade_table(unsigned char *memory);
/* Soft focus (diffusion filter): Off/Low/Mid/High, the Bloom workspace.
 * Every pixel is blended toward the wide diffusion of the luma around it:
 * lifted where that is brighter (mist, halos into shadows), softened a
 * little where it is darker. */
unsigned film_soft_radius(unsigned level);
int film_uyvy_soften(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned level,void *work,size_t work_size,void (*progress)(unsigned,unsigned));
#endif
