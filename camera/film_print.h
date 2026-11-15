/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Print and film-handling effects on graded signed UYVY422
 * rows: dust and scratches and a date stamp. Applied
 * inside the colour pass after vignette and light leaks, before Bloom and
 * grain, so grain covers them like the rest of the picture. */
#ifndef FILM_PRINT_H
#define FILM_PRINT_H
#define FILM_PRINT_TEXT 24              /* stamp characters */
enum {FILM_STAMP_OFF,FILM_STAMP_BOTTOM_RIGHT,FILM_STAMP_BOTTOM_LEFT,FILM_STAMP_TOP_RIGHT,FILM_STAMP_TOP_LEFT};
typedef struct {
 unsigned dust;                         /* 0..3 (Off/Low/Mid/High) */
 unsigned dust_dark;                    /* 1: dark specks (slide and instant looks), 0: light (prints from negatives) */
 unsigned stamp;                        /* FILM_STAMP_* corner */
 char text[FILM_PRINT_TEXT+1];          /* printable ASCII; other bytes print as '?' */
} film_print_settings;
typedef struct {
 unsigned width,height,mono,dust,dust_dark,seed,scale;
 unsigned stamp,len,pitch,radius16,glow_inv;
 int tx,ty;                             /* stamp text origin (top-left of the first dot cell) */
 unsigned x0,x1,y0,y1;                  /* stamp area including the glow */
 unsigned char glyph[FILM_PRINT_TEXT];  /* font indices */
} film_print;
/* Returns 1 when any effect is active, 0 when none is, -1 for invalid input.
 * The seed varies dust and scratches per shot. */
int film_print_begin(film_print *p,unsigned width,unsigned height,const film_print_settings *s,unsigned seed,int mono);
/* Apply to one span of a graded row (even start_col and span). */
void film_print_row(const film_print *p,unsigned row,unsigned char *pixels,unsigned start_col,unsigned span);
#endif
