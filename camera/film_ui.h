/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Capture publishes state; SpyTask owns drawing/power updates. */
#ifndef FILM_UI_H
#define FILM_UI_H
#define FILM_UI_SAVED 1
#define FILM_UI_FAILED 3
#define FILM_UI_STAGE_BLOOM_GRAIN 7
#define FILM_UI_STAGE_SOFT 8
#define FILM_UI_STAGE_GBOY 9
void film_ui_raw_begin(void);
void film_ui_set_look(const char *name);
/* The saved look the settings match, shown small beside the LUT's name on
 * the indicator ("" or 0 for none). */
void film_ui_set_preset(const char *name);
void film_ui_set_halation(int enabled);
void film_ui_set_grain(int level);
void film_ui_set_bloom(int level);
void film_ui_set_vignette(int level);
void film_ui_set_leaks(int level);
void film_ui_set_toy(int level);
/* Indicator codes for dust (0-3) and the date stamp. */
void film_ui_set_print(int dust,int stamp);
void film_ui_set_soft(int level);
void film_ui_raw_stage(int stage);
/* Bloom and grain will run as one pass (FUSEFX.TXT): counted as one stage. */
void film_ui_set_fused(int on);
void film_ui_raw_filename(const char *src);
void film_ui_progress(int row,int total);
void film_ui_result(int result,int detail);
int film_ui_busy(void);
/* Nonzero while the developing overlay or its result is on screen. */
int film_ui_themed(void);
/* Nonzero for a second after the shooting-screen indicator was drawn. */
int film_ui_indicator_shown(void);
void film_ui_tick(void);
void film_ui_draw(void);
/* Shooting-screen settings line; called from CHDK's OSD drawing. */
#define FILM_UI_INDICATOR_OFF 0
#define FILM_UI_INDICATOR_BOTTOM 1
#define FILM_UI_INDICATOR_TOP 2
void film_ui_set_processing(int on);
void film_ui_set_indicator(int where);
/* Flash the viewfinder LEDs while processing (default on). */
void film_ui_set_leds(int on);
void film_ui_draw_osd(void);
/* Small dot-matrix text (the date stamp's glyphs, set proportionally) and a
 * tab of colour c rising FILM_UI_TAB_H rows from a card's top-left at row y
 * (the card's top-left corner squared under it): shared by the indicator and
 * the look preview. */
#define FILM_UI_TAB_H 11
/* The indicator's effect icons, 13 x 11, in this order; film_ui_icon draws
 * one with its white, orange and grey parts in the colours given (the Zrno
 * menu shows them on the effect rows). */
enum {FILM_UI_ICON_HALATION,FILM_UI_ICON_BLOOM,FILM_UI_ICON_SOFT,FILM_UI_ICON_GRAIN,FILM_UI_ICON_VIGNETTE,
      FILM_UI_ICON_LEAKS,FILM_UI_ICON_TOY,FILM_UI_ICON_DUST,FILM_UI_ICON_STAMP,FILM_UI_ICONS};
#define FILM_UI_ICON_W 13
#define FILM_UI_ICON_H 11
void film_ui_icon(int x,int y,int icon,int white,int accent,int grey);
int film_ui_tiny_width(const char *s);
void film_ui_tiny_text(int x,int y,const char *s,int c);
void film_ui_tab(int x1,int ty,int x2,int y,int c);
#endif
