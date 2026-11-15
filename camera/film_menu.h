/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. The Zrno menu: items, renderer and palette. */
#ifndef FILM_MENU_H
#define FILM_MENU_H

/* While the menu is up, four CHDK colour slots take the menu's palette
 * (G12 entries are 0xOOYYUUVV: opacity 3, BT.601 Y and signed U, V). The
 * platform palette loader restores CHDK's colours once the menu closes. */
#define FILM_PAL_BG      0x030F06FD   /* #0B0F1A background */
#define FILM_PAL_SURFACE 0x032229EA   /* #03246B value cards */
#define FILM_PAL_ACCENT  0x0382B755   /* #F95F01 cursor */
#define FILM_PAL_MUTED   0x039A0FF7   /* #8E9BB5 secondary text */
/* Slots used, as offsets from CHDK_COLOR_BASE: dark red, light red, dark
 * green and light green (red, green, blue and yellow stay as they are). */
#define FILM_PAL_SLOT_BG      1
#define FILM_PAL_SLOT_SURFACE 2
#define FILM_PAL_SLOT_ACCENT  4
#define FILM_PAL_SLOT_MUTED   5
/* The shooting-screen indicator's orange, in CHDK's dark-blue slot: the
 * battery and SD-space icons use the four slots above on that screen, and
 * histogram and zebra the bright blue, cyan and magenta. */
#define FILM_PAL_SLOT_ICON    7

/* The theme colours, as CHDK colour indexes (the slots above). */
#ifndef FILM_BG
#define FILM_BG      (chdk_colors[IDX_COLOR_RED_DK])
#define FILM_SURFACE (chdk_colors[IDX_COLOR_RED_LT])
#define FILM_ACCENT  (chdk_colors[IDX_COLOR_GREEN_DK])
#define FILM_MUTED   (chdk_colors[IDX_COLOR_GREEN_LT])
#define FILM_ICON_ACCENT (chdk_colors[IDX_COLOR_BLUE_DK])
#endif

/* Filled rectangle with corners of radius 2, 4 or 6 in colour c; the
 * corners take colour out. Shared with the developing overlay. */
void film_menu_fill(int x1, int y1, int x2, int y2, int c, int out, int radius);

/* Tear-free drawing of one screen region (CHDK coordinates, inclusive):
 * draw between the two calls; the end writes only the changed pixels, to
 * both bitmap buffers. Returns 0 when another task is drawing a frame
 * (skip this one; do not call film_draw_end). */
int film_draw_begin(int x1, int y1, int x2, int y2);
/* The same for a frame that will draw every pixel of the region: the shadow
 * is not seeded from the screen first. */
int film_draw_begin_cover(int x1, int y1, int x2, int y2);
void film_draw_end(void);
/* Inside a frame: fill the whole region with colour c at memory speed.
 * Returns 0 when drawing is direct (no shadow); draw the fill instead. */
int film_draw_clear(int c);
/* Inside a frame: the shadow screen itself (buffer bytes, *stride per
 * row), for drawing a whole screen at memory speed; 0 when drawing is
 * direct or the OSD is rotated. film_draw_end writes it out as usual. */
unsigned char *film_draw_shadow(unsigned *stride);
/* Nonzero while the screen is swung round to face the lens: the overlays
 * are then written mirrored, as Canon draws its own. */
int film_screen_facing_lens(void);

/* The menu's font, from the card; CHDK's built-in font if it is missing. */
#define FILM_MENU_FONT "A/CHDK/FILM/MENU.RBF"

/* Which film colours the palette loader holds: 2 while the Zrno menu (or
 * its Settings submenu) or the developing overlay is up (the theme, and the orange in the
 * icon slot), 1 while only the shooting-screen indicator shows (the orange
 * alone), else 0. */
int film_menu_palette(void);

/* Nonzero while the Zrno menu, its Settings submenu or the look preview is
 * on screen (the preview counts as a menu mode). */
int film_menu_showing(void);

/* Nonzero while the look preview is up. */
int film_preview_showing(void);

/* Called whenever the menu becomes current: hides "Re-develop photo"
 * outside the gallery. */
void film_menu_prepare(void);

/* Draw the menu (everything when full, else the rows), keeping the current
 * item visible by moving *top. Returns the number of rows on screen. */
#ifdef GUI_MENU_H
int film_menu_draw(const CMenu *menu, int *top, int curr, int full);
#endif

/* In rbf_font.c: swap the menu font in (on) or back out; returns the
 * height of the font now in use. */
int rbf_film_font(const char *file, int on);

#ifdef GUI_MENU_H
extern CMenu film_lut_menu, film_settings_menu;
/* SET on a menu row: opens the look preview for the LUT or Look row in
 * shooting mode and returns 1; 0 leaves SET to the menu. */
int film_preview_open(const CMenuItem *m);
extern gui_handler filmPreviewGuiHandler;
/* Nonzero for the Zrno menu and its submenu, which draw themselves. */
int film_menu_ours(const CMenu *menu);
#endif
#endif
