/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. ZrnoBoy on the camera: the engine switch (Film,
 * ZrnoBoy, Print, Grade) and ZrnoBoy's settings (CHDK/FILM/ZRNOBOY.CFG), its
 * Zrno menu rows, the live view and the shot (film_gboy.c has the kernels).
 * The live task, overlay and palette slots serve Print too
 * (film_rsui.c). */
#ifndef FILM_GBUI_H
#define FILM_GBUI_H
#include "film_gboy.h"

/* Load the settings (the first poll does). */
void film_gbui_init(void);
/* SpyTask: store changed settings once no shot is in progress, and set up
 * the engine's memory when it is chosen. */
void film_gbui_poll(void);
/* 1 when the ZrnoBoy engine is chosen. */
int film_gboy_engine(void);
/* 1 when the Print engine is chosen. */
int film_riso_engine(void);
/* 1 when the Grade engine is chosen (film_grui.c). */
int film_grade_engine(void);
/* Nonzero for any engine but Film (no film look or effects). */
int film_engine_alt(void);
/* "Film", "ZrnoBoy", "Print" or "Grade". */
const char *film_gbui_engine_name(void);
/* What the developing panel says while a picture engine works on the shot. */
const char *film_gbui_stage_text(void);

/* Menu rows (CHDK enum callbacks). */
const char *film_gbui_menu_engine(int change, int arg);
const char *film_gbui_menu_palette(int change, int arg);
const char *film_gbui_menu_frame(int change, int arg);
const char *film_gbui_menu_contrast(int change, int arg);
const char *film_gbui_menu_brightness(int change, int arg);
const char *film_gbui_menu_edges(int change, int arg);
const char *film_gbui_menu_dither(int change, int arg);
const char *film_gbui_menu_resolution(int change, int arg);
/* Settings row "Live preview": On draws the ZrnoBoy or Print screen over
 * the shooting screen; Off leaves Canon's live view (and the indicator).
 * The preview of a row (SET on it) still shows the engine's screen. */
const char *film_gbui_menu_live(int change, int arg);
/* Set by the Engine row when it changes: the menu shows the other engine's
 * rows; film_menu.c takes it (returns it and clears it). */
int film_gbui_engine_changed(void);
/* Looks (film_lut.c, the keyboard task) keep the engine and its settings.
 * The engine: 0 Film, 1 ZrnoBoy, 2 Print, 3 Grade; setting another one
 * switches the menu's rows as the Engine row does. A value out of range is
 * left as it is. */
#define FILM_ENGINES 4
int film_gbui_engine(void);
void film_gbui_set_engine(int engine);
void film_gbui_get(film_gboy_settings *s);
void film_gbui_set(const film_gboy_settings *s);

/* The shot. arm (from film_stage_arm, any task): copies the settings and
 * returns 1 when the shot will be rendered by ZrnoBoy. render (Canon's
 * develop task): the whole frame, signed UYVY; row: stride bytes of
 * scratch. Returns 0, or a negative code. */
int film_gbui_arm(unsigned seed);
int film_gbui_render(unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                     unsigned char *row, void (*progress)(unsigned, unsigned));

/* Live view: draws the ZrnoBoy screen over the camera's live view when the
 * engine, Processing and the shooting screen allow it, and returns 1; else
 * returns 0 (and erases what it drew before). card: drawn over it in the
 * same frame (the look preview's card), or 0. */
int film_gbui_live_draw(void (*card)(void));
/* Nonzero for a second after the live view was drawn: the palette loader
 * then holds the palette's four shades in the four slots below (Print:
 * its ink colours, three more slots with three inks). */
int film_gbui_live_shown(void);
/* Palette value (0xOOYYUUVV) the loader should put in CHDK colour slot
 * `slot` (an offset from CHDK_COLOR_BASE), or deflt when ZrnoBoy does not
 * use it now. Slots: green, yellow, dark yellow and light yellow. */
#define FILM_GBUI_SLOT0 3
#define FILM_GBUI_SLOT1 10
#define FILM_GBUI_SLOT2 11
#define FILM_GBUI_SLOT3 12
#define FILM_GBUI_SLOT4 6
#define FILM_GBUI_SLOT5 8
#define FILM_GBUI_SLOT6 9
unsigned film_gbui_palette_slot(int slot, unsigned deflt);
#endif
