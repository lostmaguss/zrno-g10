/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Print on the camera: its settings
 * (CHDK/FILM/PRINT.CFG), its tables (CHDK/FILM/PRINT.BIN), its Zrno menu
 * rows, the shot and the live picture (film_riso.c has the kernels). The
 * engine switch, the live task and the overlay belong to film_gbui.c,
 * which calls in here while Print is the engine. */
#ifndef FILM_RSUI_H
#define FILM_RSUI_H
#include "film_riso.h"

/* SpyTask (from film_gbui_poll): load the settings once, store changes,
 * load the tables and the shot's memory while active is nonzero. */
void film_rsui_poll(int active);
/* 1 when the tables and memory are loaded. */
int film_rsui_ready(void);

/* Menu rows (CHDK enum callbacks). */
const char *film_rsui_menu_inks(int change, int arg);
const char *film_rsui_menu_separation(int change, int arg);
const char *film_rsui_menu_screen(int change, int arg);
const char *film_rsui_menu_noise(int change, int arg);
const char *film_rsui_menu_register(int change, int arg);
const char *film_rsui_menu_flaws(int change, int arg);
const char *film_rsui_menu_paper(int change, int arg);

/* The shot: arm copies the settings (1 when ready to print); render prints
 * the whole frame in Canon's develop task. Returns 0, or a negative code. */
int film_rsui_arm(unsigned seed);
int film_rsui_render(unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                     void (*progress)(unsigned, unsigned));

/* Live view (ZrnoLive task): the masks for the viewport into canvas
 * (FILM_RISO_LIVE_W x FILM_RISO_LIVE_H); yuv: 3 * W * H bytes of workspace.
 * fresh: the first frame after a pause (the levels jump). */
void film_rsui_live_frame(const unsigned char *view, unsigned width, unsigned height, unsigned rowbytes,
                          int mirror, int fresh, unsigned char *yuv, unsigned char *canvas);
/* Colour of mask m as 0xOOYYUUVV for the overlay palette (the settings of
 * the live frame), and how many masks the inks make (4 or 8). */
unsigned film_rsui_live_colour(unsigned m);
unsigned film_rsui_live_colours(void);
/* What the developing panel says while the armed shot prints. */
const char *film_rsui_stage_text(void);
/* The Style row and its Palette row (the styles' palettes; the Riso style
 * uses the Inks row). style_changed: set when Style changes, so the menu
 * shows that style's rows (taken: returns it and clears it). */
const char *film_rsui_menu_style(int change, int arg);
const char *film_rsui_menu_palette(int change, int arg);
int film_rsui_style_changed(void);
unsigned film_rsui_style(void);
/* Looks (film_lut.c, the keyboard task): the settings the rows show, and
 * setting them all at once (the style's rows switch with a new style; a
 * value out of range is left as it is). */
void film_rsui_get(film_riso_settings *s);
void film_rsui_set(const film_riso_settings *s);
#endif
