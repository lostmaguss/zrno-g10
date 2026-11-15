/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Print: the third engine, a stencil-duplicator
 * (risograph-style) print. The photo is separated into two or three spot
 * inks, each screened with a random grain or with halftone dots, and the
 * inks overprint like multiply on off-white paper: every output pixel is
 * one of 4 (or 8) exact colours. Each ink's drum sits out of register and
 * slightly turned; noise is generated as the kernel goes (never a tile, so
 * it never repeats); the print flaws are lost dots, white specks and soft
 * streaks along the paper's feed.
 *
 * Tables the camera cannot cheaply compute (colour separations, overprint
 * colours, grain screens) come from CHDK/FILM/PRINT.BIN (tools/riso_pack.py).
 * Kernels only: no allocation, no file I/O, no divide in pixel loops. */
#ifndef FILM_RISO_H
#define FILM_RISO_H
#include <stddef.h>

#define FILM_RISO_INKS 3              /* most inks in a set */
#define FILM_RISO_TILE 128            /* screen tiles, wrapped by masks */
enum {
    FILM_RISO_SETS = 7, FILM_RISO_SEPARATIONS = 2, FILM_RISO_SCREENS = 2, FILM_RISO_NOISES = 5,
    FILM_RISO_REGISTERS = 4, FILM_RISO_FLAWS = 4, FILM_RISO_PAPERS = 3,
    FILM_RISO_STYLES = 7,          /* Riso, Screenprint, Engraving, Woodcut, Photocopy, Comic, Text mode */
    FILM_RISO_ALL_SETS = 22        /* the Riso ink sets, then each style's palettes */
};
enum { FILM_RISO_STYLE_RISO, FILM_RISO_STYLE_SCREENPRINT, FILM_RISO_STYLE_ENGRAVING, FILM_RISO_STYLE_WOODCUT,
       FILM_RISO_STYLE_PHOTOCOPY, FILM_RISO_STYLE_COMIC, FILM_RISO_STYLE_TEXT };

/* inks: the Riso ink set; style and variant (the style's palette) choose
 * the other looks, which bring their own inks, screens and paper. */
typedef struct {
    unsigned char inks, separation, screen, noise, reg, flaws, paper, style, variant;
} film_riso_settings;

const char *film_riso_style_name(unsigned v);
/* A style's palettes: how many, and their names. */
unsigned film_riso_variants(unsigned style);
const char *film_riso_variant_name(unsigned style, unsigned v);
/* The table set the settings print with (0 .. FILM_RISO_ALL_SETS - 1). */
unsigned film_riso_set(const film_riso_settings *s);

const char *film_riso_inks_name(unsigned v);
const char *film_riso_separation_name(unsigned v);
const char *film_riso_screen_name(unsigned v);
const char *film_riso_noise_name(unsigned v);
const char *film_riso_register_name(unsigned v);
const char *film_riso_flaws_name(unsigned v);
const char *film_riso_paper_name(unsigned v);

/* PRINT.BIN: 0 when data (size bytes) is a table file this code can use. */
#define FILM_RISO_DATA_BYTES 645760u
int film_riso_data_check(const unsigned char *data, size_t size);
/* Inks in a table set (1 to 3; 0 for text mode). */
unsigned film_riso_ink_count(const unsigned char *data, unsigned set);
/* Colours the live view needs for the settings (4 or 8). */
unsigned film_riso_live_colours(const unsigned char *data, const film_riso_settings *s);
/* Colour of ink mask m (bit k: ink k printed) on the paper, BT.601
 * full-range Y and signed U, V. */
void film_riso_colour(const unsigned char *data, const film_riso_settings *s, unsigned m, int *y, int *u, int *v);

/* ---- The shot ------------------------------------------------------------ */
/* Workspace for a frame width (sep tables, packed screens, a few rows). */
size_t film_riso_work_size(unsigned width);
/* Print the whole frame (signed UYVY422, width a multiple of 4, height even)
 * in place. seed varies the noise, register and flaws per shot. progress
 * (rows done, height) every 32 rows. Returns 0, or -1 for bad arguments. */
int film_riso_render(unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                     const unsigned char *data, const film_riso_settings *s, unsigned seed,
                     void *work, size_t work_bytes, void (*progress)(unsigned, unsigned));
/* The levels a frame is stretched by (1% and 99% of its luma, sampled). */
void film_riso_levels_uyvy(const unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                           int *lo, int *hi);

/* ---- Live view ----------------------------------------------------------- */
/* The live picture: cw x ch (at most 320 x 240) samples of the 720 x 480
 * UYVYYY viewport, mirrored when the screen faces the lens. yuv (3 * cw *
 * ch bytes, halfword aligned): the luma plane, then each sample's table
 * index as a halfword (rawU >> 4 << 9 | rawV >> 4 << 5 | Y >> 3). */
#define FILM_RISO_LIVE_W 320
#define FILM_RISO_LIVE_H 240
void film_riso_live_sample(const unsigned char *view, unsigned width, unsigned height, unsigned rowbytes,
                           unsigned cw, unsigned ch, int mirror, unsigned char *yuv);
/* Levels of the samples, and the live levels following them (a quarter of
 * the way per frame; fresh: jump). */
void film_riso_live_levels(const unsigned char *yuv, unsigned n, int fresh, int *lo, int *hi);
/* Ink masks (0..7) for the samples: a coarser screen than the photo's, the
 * register scaled down; sep: 3 * 8192 bytes of workspace. */
void film_riso_live_render(const unsigned char *yuv, unsigned cw, unsigned ch, const unsigned char *data,
                           const film_riso_settings *s, int lo, int hi, unsigned char *sep, unsigned char *canvas);
/* Paint the masks over the 720 x 240 overlay in whole screen pixels (index:
 * the palette entry of each mask). */
void film_riso_live_paint(const unsigned char *canvas, unsigned cw, unsigned ch, const unsigned char index[8],
                          unsigned char *overlay, unsigned stride, unsigned cols, unsigned rows);
#endif
