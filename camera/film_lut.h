/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Resident look selection; all file I/O belongs to SpyTask. */
#ifndef FILM_LUT_H
#define FILM_LUT_H
void film_lut_init(void);
void film_lut_poll(int stage_busy);
/* The loaded look's DIGIC colour table (0: none could be loaded). */
const void *film_lut_data(void);
const char *film_lut_name(void);
const char *film_lut_file(void);
void *film_lut_halo_work(void);
unsigned film_lut_halo_level(void);
void *film_lut_grain_work(void);
unsigned film_lut_grain_level(void);
const char *film_lut_menu_grain(int change,int arg);
const char *film_lut_menu_halo(int change,int arg);
/* Halation colour: Red (default) or Amber, the glow's colour before the look. */
const char *film_lut_menu_halo_colour(int change,int arg);
const char *film_lut_menu_choice(int change,int arg);
const char *film_lut_menu_status(int change,int arg);
void *film_lut_bloom_work(void);
unsigned film_lut_bloom_level(void);
const char *film_lut_menu_bloom(int change,int arg);
/* Soft focus: Off/Low/Mid/High (RETRO.CFG byte 11 bits 2-3), run before
 * Bloom in the Bloom workspace (the effect pool), or 0 when off. */
unsigned film_lut_soft_level(void);
void *film_lut_soft_work(void);
const char *film_lut_menu_soft(int change,int arg);
/* Canon's sharpening while processing is on: Canon, Soft, Off, Crisp. */
const char *film_lut_menu_sharpen(int change,int arg);
/* Processing switch: 0 leaves captures untouched, 1 enables the route. */
int film_lut_processing(void);
const char *film_lut_menu_processing(int change,int arg);
/* Shooting-screen indicator: Off, Bottom (default) or Top. */
const char *film_lut_menu_indicator(int change,int arg);
/* Vignette and light leaks: Off/Low/Mid/High, stored in RETRO.CFG. */
unsigned film_lut_vignette_level(void);
unsigned film_lut_leak_level(void);
const char *film_lut_menu_vignette(int change,int arg);
const char *film_lut_menu_leaks(int change,int arg);
/* Toy lens: Off/Low/Mid/High (RETRO.CFG byte 10) and its workspace of
 * film_toy_work_size(3648) bytes in the effect pool, or 0 when off. */
unsigned film_lut_toy_level(void);
void *film_lut_toy_work(void);
const char *film_lut_menu_toy(int change,int arg);
/* Busy LEDs beside the viewfinder while processing: On (default) or Off. */
int film_lut_leds(void);
const char *film_lut_menu_leds(int change,int arg);
/* S button (CHDK's <ALT> button on the G12): 1 opens the Zrno
 * menu (default), 0 enters <ALT> mode as stock CHDK. */
int film_lut_s_opens_film_menu(void);
const char *film_lut_menu_sbutton(int change,int arg);
/* Dust and scratches, and the date stamp: menus, and the settings for the
 * next shot (the date read now). Monochrome: the look's table has no
 * chroma. */
#include "film_print.h"
int film_lut_mono(void);
const char *film_lut_menu_dust(int change,int arg);
const char *film_lut_menu_stamp(int change,int arg);
const char *film_lut_menu_stamp_mode(int change,int arg);
const char *film_lut_menu_stamp_text(int change,int arg);
void film_lut_print_settings(film_print_settings *s);
/* Looks: saved sets of the LUT, effects, dust and date stamp
 * (CHDK/FILM/LOOKS.TXT). The Look row recalls them; the four actions save
 * the settings as a new look, update the look they came from, rename it
 * (text box) or delete it (SET twice). Labels name the look they act on;
 * *enabled is 0 when the action has nothing to do. */
enum {FILM_LOOK_SAVE,FILM_LOOK_UPDATE,FILM_LOOK_RENAME,FILM_LOOK_DELETE};
const char *film_lut_menu_look(int change,int arg);
void film_lut_look_action(int item);
const char *film_lut_look_label(int item,int *enabled);
void film_lut_look_disarm(void);
/* How many looks are saved. */
int film_lut_looks_saved(void);
/* With Processing on, the look's hardware table is installed for
 * Canon's develop; this returns it, or 0 when Canon's own table is in place. */
const unsigned *film_lut_hw_table(void);
/* The installed look's halation tint (see film_uyvy_graded_retro), or 0. */
#include "film_colour.h"
const film_halo_tint *film_lut_hw_tint(void);
#endif
