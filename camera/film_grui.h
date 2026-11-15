/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Grade on the camera: four grades (slots) in
 * CHDK/FILM/GRADE.CFG, the Zrno menu rows that edit the current one, and
 * the colour table built from it (film_grade.c has the maths; film_lut.c
 * installs the table as Canon's "My Colors Off", as it does a film look's). */
#ifndef FILM_GRUI_H
#define FILM_GRUI_H
#include "film_grade.h"

#define FILM_GRUI_SLOTS 4

/* SpyTask (from film_gbui_poll): load the grades once, store changes a
 * second after the last one, once no shot is in progress. */
void film_grui_poll(void);

/* Menu rows (CHDK enum callbacks). field: a FILM_GRADE_* outside the HSL
 * ones as the row's arg. hsl: the chosen colour's hue (arg 0),
 * saturation (1) or lightness (2). */
const char *film_grui_menu_slot(int change, int arg);
const char *film_grui_menu_field(int change, int arg);
const char *film_grui_menu_colour(int change, int arg);
const char *film_grui_menu_hsl(int change, int arg);
/* "Red hue", "Red saturation", "Red lightness" for the chosen colour. */
const char *film_grui_hsl_label(int arg);
/* Reset grade (a CHDK proc row): SET twice puts the current grade back to
 * neutral. The label says what the next SET does; disarm when the cursor
 * leaves the row. */
void film_grui_reset(int arg);
const char *film_grui_reset_label(int *enabled);
void film_grui_reset_disarm(void);

/* Changes on every edit of the current grade (and a change of slot). */
unsigned film_grui_serial(void);
/* Builds the current grade's table into a buffer other than avoid (the one
 * Canon may be reading) and returns it; 0 without memory. SpyTask only. */
const unsigned *film_grui_build(const unsigned *avoid);
/* Looks (film_lut.c, the keyboard task): the grade being edited (slot 0..3)
 * and its values; setting them chooses that slot and overwrites it (an
 * invalid grade is ignored). */
void film_grui_get(int *slot, film_grade_settings *g);
void film_grui_set(int slot, const film_grade_settings *g);
/* "Grade 1" .. "Grade 4", for the indicator. */
const char *film_grui_slot_name(void);
#endif
