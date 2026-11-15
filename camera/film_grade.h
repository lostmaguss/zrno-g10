/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Grade: the fourth engine, a colour grade made in the
 * camera. The grade is the colour table Canon's develop already applies (the
 * DIGIC 4 9x9x9 RGB -> YCbCr table, film_hwcolour.h): every setting is folded
 * into its 729 nodes, so a graded shot takes no longer than Canon's own and
 * the live view shows the grade itself. See GRADE.md.
 *
 * The grade, in order, on Canon's display RGB:
 *   1. colour balance: temperature (blue-amber) and tint (green-magenta);
 *   2. tone, one curve on each channel: contrast, midtones, shadows,
 *      highlights, blacks (the darkest 64 levels), then fade (lifted blacks);
 *   3. saturation (-5 is black and white) and vibrance (muted colours more);
 *   4. HSL: hue, saturation and lightness of eight colours;
 *   5. whites (the brightest 64 levels), on each channel;
 *   6. split toning: a colour in the shadows and one in the highlights.
 * Black and white (a filter, or none) changes the order: colour balance,
 * HSL, then the grey mix through the filter, the tone curve on that grey,
 * whites and split toning; saturation and vibrance have nothing to act on.
 * No file or CHDK dependency (host tests build it as it is). */
#ifndef FILM_GRADE_H
#define FILM_GRADE_H

/* The eight colours of the HSL and split-toning rows. */
#define FILM_GRADE_HUES 8
const char *film_grade_hue_name(unsigned hue);

/* Black and white: 0 Off (colour), then the grey mix of each filter. */
#define FILM_GRADE_MONOS 7
const char *film_grade_mono_name(unsigned mono);

/* One grade: FILM_GRADE_FIELDS signed values, each within its field's
 * range (film_grade_min/max). Hue fields of the split tone: 0 Off, 1..8 a
 * colour of film_grade_hue_name (hue - 1). The fields after the HSL ones
 * came later: a grade stored without them has them 0. */
enum {
    FILM_GRADE_CONTRAST, FILM_GRADE_MIDTONES, FILM_GRADE_SHADOWS, FILM_GRADE_HIGHLIGHTS, FILM_GRADE_FADE,
    FILM_GRADE_TEMPERATURE, FILM_GRADE_TINT, FILM_GRADE_SATURATION, FILM_GRADE_VIBRANCE,
    FILM_GRADE_SHADOW_HUE, FILM_GRADE_SHADOW_AMOUNT, FILM_GRADE_HIGHLIGHT_HUE, FILM_GRADE_HIGHLIGHT_AMOUNT,
    FILM_GRADE_HSL_HUE,                                          /* 8 fields, one per colour */
    FILM_GRADE_HSL_SAT = FILM_GRADE_HSL_HUE + FILM_GRADE_HUES,
    FILM_GRADE_HSL_LIGHT = FILM_GRADE_HSL_SAT + FILM_GRADE_HUES,
    FILM_GRADE_BLACKS = FILM_GRADE_HSL_LIGHT + FILM_GRADE_HUES,  /* 37 */
    FILM_GRADE_WHITES, FILM_GRADE_MONO,
    FILM_GRADE_FIELDS                                            /* 40 */
};
typedef struct { signed char v[FILM_GRADE_FIELDS]; } film_grade_settings;
int film_grade_min(unsigned field);
int film_grade_max(unsigned field);
/* The neutral grade: every value 0 (split-tone amounts 3, hues Off). */
void film_grade_neutral(film_grade_settings *s);
/* 1 if every value is within its range. */
int film_grade_valid(const film_grade_settings *s);
/* 1 if the grade changes nothing (the table is then Canon's own, within a
 * step of its rounding). */
int film_grade_is_neutral(const film_grade_settings *s);

/* What the grade makes of one colour: rgb in Q12 (4096 = 256 levels, the
 * table's node 8), out: Y, U, V in Q12 (BT.601, full range). */
void film_grade_pixel(const film_grade_settings *s, const int rgb[3], int out[3]);
/* The 1000-word hardware table for the grade (index r + 10g + 100b; node n
 * at RGB 32n; node 9 repeats node 8). Workspace on the stack: under 1.2 KB. */
void film_grade_table(const film_grade_settings *s, unsigned *table);
#endif
