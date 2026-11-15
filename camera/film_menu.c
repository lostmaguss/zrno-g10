/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. The Zrno menu. It opens straight from the S button by
 * default; S or a half-press of the shutter leaves it for the camera. Its Settings submenu, at the bottom, holds the camera-side rows
 * (indicator, Busy LEDs, the S button) and "CHDK menu", which goes on to
 * CHDK's main menu (MENU does too).
 *
 * CHDK's gui_menu.c keeps the navigation (keys, scrolling, value changes)
 * and hands drawing to film_menu_draw: a rounded cursor, each value in its
 * own card, a larger font and a palette of the menu's own. */
#include "camera_info.h"
#include "modes.h"
#include "conf.h"
#include "font.h"
#include "lang.h"
#include "gui.h"
#include "gui_draw.h"
#include "gui_menu.h"
#include "gui_lang.h"
#include "film_lut.h"
#include "film_gbui.h"
#include "film_rsui.h"
#include "film_grui.h"
#include "film_menu.h"
#include "film_ui.h"
#include "film_queue.h"
#include "keyboard.h"
#include "string.h"

static void film_open_chdk_menu(__attribute__((unused)) int arg)
{
    gui_menu_init(&root_menu);
}

/* Settings: how Zrno behaves on the camera, rather than how photos look. */
static CMenuItem film_settings_items[] = {
    MENU_ITEM(0x5f,"Indicator", MENUITEM_ENUM, film_lut_menu_indicator, 0),
    MENU_ITEM(0x5f,"Busy LEDs", MENUITEM_ENUM, film_lut_menu_leds, 0),
    MENU_ITEM(0x5f,"S button opens", MENUITEM_ENUM, film_lut_menu_sbutton, 0),
    MENU_ITEM(0x5f,"Live preview", MENUITEM_ENUM, film_gbui_menu_live, 0),
    MENU_ITEM(0x28,"CHDK menu", MENUITEM_PROC, film_open_chdk_menu, 0),
    MENU_ITEM(0x51,LANG_MENU_BACK, MENUITEM_UP, 0, 0),
    {0}
};
CMenu film_settings_menu = {0x28,(int)"Settings",film_settings_items};

extern int module_run(char *name);
static CMenuItem film_lut_menu_items[] = {
    MENU_ITEM(0x28,"Re-develop photo", MENUITEM_PROC, module_run, "redev.flt"),
    MENU_ITEM(0x5f,"Processing", MENUITEM_ENUM, film_lut_menu_processing, 0),
    MENU_ITEM(0x5f,"Look", MENUITEM_ENUM, film_lut_menu_look, 0),
    MENU_ITEM(0x5f,"Engine", MENUITEM_ENUM, film_gbui_menu_engine, 0),
    MENU_ITEM(0x5f,"LUT", MENUITEM_ENUM, film_lut_menu_choice, 0),
    MENU_ITEM(0x5f,"Halation", MENUITEM_ENUM, film_lut_menu_halo, 0),
    MENU_ITEM(0x5f,"Halation colour", MENUITEM_ENUM, film_lut_menu_halo_colour, 0),
    MENU_ITEM(0x5f,"Bloom", MENUITEM_ENUM, film_lut_menu_bloom, 0),
    MENU_ITEM(0x5f,"Soft focus", MENUITEM_ENUM, film_lut_menu_soft, 0),
    MENU_ITEM(0x5f,"Sharpening", MENUITEM_ENUM, film_lut_menu_sharpen, 0),
    MENU_ITEM(0x5f,"Grain", MENUITEM_ENUM, film_lut_menu_grain, 0),
    MENU_ITEM(0x5f,"Vignette", MENUITEM_ENUM, film_lut_menu_vignette, 0),
    MENU_ITEM(0x5f,"Light leaks", MENUITEM_ENUM, film_lut_menu_leaks, 0),
    MENU_ITEM(0x5f,"Toy lens", MENUITEM_ENUM, film_lut_menu_toy, 0),
    MENU_ITEM(0x5f,"Dust & scratches", MENUITEM_ENUM, film_lut_menu_dust, 0),
    MENU_ITEM(0x5f,"Date stamp", MENUITEM_ENUM, film_lut_menu_stamp, 0),
    MENU_ITEM(0x5f,"Stamp shows", MENUITEM_ENUM, film_lut_menu_stamp_mode, 0),
    MENU_ITEM(0x5f,"Stamp text", MENUITEM_ENUM, film_lut_menu_stamp_text, 0),
    MENU_ITEM(0x28,"Save as new look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_SAVE),
    MENU_ITEM(0x28,"Update look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_UPDATE),
    MENU_ITEM(0x28,"Rename look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_RENAME),
    MENU_ITEM(0x28,"Delete look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_DELETE),
    MENU_ITEM(0x28,"Settings", MENUITEM_SUBMENU, &film_settings_menu, 0),
    MENU_ITEM(0x51,LANG_MENU_BACK, MENUITEM_UP, 0, 0),
    {0}
};
/* The ZrnoBoy engine's rows: the same first four (Re-develop, Processing,
 * Look, Engine), so the cursor stays on Look or Engine when either switches
 * the list, and the four look rows before Settings, as in every engine. */
static CMenuItem film_gboy_menu_items[] = {
    MENU_ITEM(0x28,"Re-develop photo", MENUITEM_PROC, module_run, "redev.flt"),
    MENU_ITEM(0x5f,"Processing", MENUITEM_ENUM, film_lut_menu_processing, 0),
    MENU_ITEM(0x5f,"Look", MENUITEM_ENUM, film_lut_menu_look, 0),
    MENU_ITEM(0x5f,"Engine", MENUITEM_ENUM, film_gbui_menu_engine, 0),
    MENU_ITEM(0x5f,"Palette", MENUITEM_ENUM, film_gbui_menu_palette, 0),
    MENU_ITEM(0x5f,"Frame", MENUITEM_ENUM, film_gbui_menu_frame, 0),
    MENU_ITEM(0x5f,"Resolution", MENUITEM_ENUM, film_gbui_menu_resolution, 0),
    MENU_ITEM(0x5f,"Contrast", MENUITEM_ENUM, film_gbui_menu_contrast, 0),
    MENU_ITEM(0x5f,"Brightness", MENUITEM_ENUM, film_gbui_menu_brightness, 0),
    MENU_ITEM(0x5f,"Edges", MENUITEM_ENUM, film_gbui_menu_edges, 0),
    MENU_ITEM(0x5f,"Dither", MENUITEM_ENUM, film_gbui_menu_dither, 0),
    MENU_ITEM(0x28,"Save as new look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_SAVE),
    MENU_ITEM(0x28,"Update look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_UPDATE),
    MENU_ITEM(0x28,"Rename look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_RENAME),
    MENU_ITEM(0x28,"Delete look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_DELETE),
    MENU_ITEM(0x28,"Settings", MENUITEM_SUBMENU, &film_settings_menu, 0),
    MENU_ITEM(0x51,LANG_MENU_BACK, MENUITEM_UP, 0, 0),
    {0}
};
/* The Print engine's rows, after the same first four: Style, then the
 * Riso rows, another style's rows, or text mode's. Style is the fifth row
 * in each, so the cursor stays on it when it switches the list. */
static CMenuItem film_riso_menu_items[] = {
    MENU_ITEM(0x28,"Re-develop photo", MENUITEM_PROC, module_run, "redev.flt"),
    MENU_ITEM(0x5f,"Processing", MENUITEM_ENUM, film_lut_menu_processing, 0),
    MENU_ITEM(0x5f,"Look", MENUITEM_ENUM, film_lut_menu_look, 0),
    MENU_ITEM(0x5f,"Engine", MENUITEM_ENUM, film_gbui_menu_engine, 0),
    MENU_ITEM(0x5f,"Style", MENUITEM_ENUM, film_rsui_menu_style, 0),
    MENU_ITEM(0x5f,"Inks", MENUITEM_ENUM, film_rsui_menu_inks, 0),
    MENU_ITEM(0x5f,"Separation", MENUITEM_ENUM, film_rsui_menu_separation, 0),
    MENU_ITEM(0x5f,"Screen", MENUITEM_ENUM, film_rsui_menu_screen, 0),
    MENU_ITEM(0x5f,"Noise", MENUITEM_ENUM, film_rsui_menu_noise, 0),
    MENU_ITEM(0x5f,"Register", MENUITEM_ENUM, film_rsui_menu_register, 0),
    MENU_ITEM(0x5f,"Print flaws", MENUITEM_ENUM, film_rsui_menu_flaws, 0),
    MENU_ITEM(0x5f,"Paper", MENUITEM_ENUM, film_rsui_menu_paper, 0),
    MENU_ITEM(0x28,"Save as new look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_SAVE),
    MENU_ITEM(0x28,"Update look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_UPDATE),
    MENU_ITEM(0x28,"Rename look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_RENAME),
    MENU_ITEM(0x28,"Delete look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_DELETE),
    MENU_ITEM(0x28,"Settings", MENUITEM_SUBMENU, &film_settings_menu, 0),
    MENU_ITEM(0x51,LANG_MENU_BACK, MENUITEM_UP, 0, 0),
    {0}
};
static CMenuItem film_print_style_items[] = {
    MENU_ITEM(0x28,"Re-develop photo", MENUITEM_PROC, module_run, "redev.flt"),
    MENU_ITEM(0x5f,"Processing", MENUITEM_ENUM, film_lut_menu_processing, 0),
    MENU_ITEM(0x5f,"Look", MENUITEM_ENUM, film_lut_menu_look, 0),
    MENU_ITEM(0x5f,"Engine", MENUITEM_ENUM, film_gbui_menu_engine, 0),
    MENU_ITEM(0x5f,"Style", MENUITEM_ENUM, film_rsui_menu_style, 0),
    MENU_ITEM(0x5f,"Palette", MENUITEM_ENUM, film_rsui_menu_palette, 0),
    MENU_ITEM(0x5f,"Noise", MENUITEM_ENUM, film_rsui_menu_noise, 0),
    MENU_ITEM(0x5f,"Register", MENUITEM_ENUM, film_rsui_menu_register, 0),
    MENU_ITEM(0x5f,"Print flaws", MENUITEM_ENUM, film_rsui_menu_flaws, 0),
    MENU_ITEM(0x28,"Save as new look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_SAVE),
    MENU_ITEM(0x28,"Update look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_UPDATE),
    MENU_ITEM(0x28,"Rename look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_RENAME),
    MENU_ITEM(0x28,"Delete look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_DELETE),
    MENU_ITEM(0x28,"Settings", MENUITEM_SUBMENU, &film_settings_menu, 0),
    MENU_ITEM(0x51,LANG_MENU_BACK, MENUITEM_UP, 0, 0),
    {0}
};
static CMenuItem film_print_text_items[] = {
    MENU_ITEM(0x28,"Re-develop photo", MENUITEM_PROC, module_run, "redev.flt"),
    MENU_ITEM(0x5f,"Processing", MENUITEM_ENUM, film_lut_menu_processing, 0),
    MENU_ITEM(0x5f,"Look", MENUITEM_ENUM, film_lut_menu_look, 0),
    MENU_ITEM(0x5f,"Engine", MENUITEM_ENUM, film_gbui_menu_engine, 0),
    MENU_ITEM(0x5f,"Style", MENUITEM_ENUM, film_rsui_menu_style, 0),
    MENU_ITEM(0x5f,"Palette", MENUITEM_ENUM, film_rsui_menu_palette, 0),
    MENU_ITEM(0x28,"Save as new look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_SAVE),
    MENU_ITEM(0x28,"Update look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_UPDATE),
    MENU_ITEM(0x28,"Rename look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_RENAME),
    MENU_ITEM(0x28,"Delete look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_DELETE),
    MENU_ITEM(0x28,"Settings", MENUITEM_SUBMENU, &film_settings_menu, 0),
    MENU_ITEM(0x51,LANG_MENU_BACK, MENUITEM_UP, 0, 0),
    {0}
};
/* The Grade engine's rows, after the same first four: which grade,
 * tone, colour balance, saturation, black and white, HSL of the chosen
 * colour, split toning,
 * Canon's sharpening (the Film engine's row), Reset, and the look rows. */
static CMenuItem film_grade_menu_items[] = {
    MENU_ITEM(0x28,"Re-develop photo", MENUITEM_PROC, module_run, "redev.flt"),
    MENU_ITEM(0x5f,"Processing", MENUITEM_ENUM, film_lut_menu_processing, 0),
    MENU_ITEM(0x5f,"Look", MENUITEM_ENUM, film_lut_menu_look, 0),
    MENU_ITEM(0x5f,"Engine", MENUITEM_ENUM, film_gbui_menu_engine, 0),
    MENU_ITEM(0x5f,"Grade", MENUITEM_ENUM, film_grui_menu_slot, 0),
    MENU_ITEM(0x5f,"Contrast", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_CONTRAST),
    MENU_ITEM(0x5f,"Midtones", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_MIDTONES),
    MENU_ITEM(0x5f,"Shadows", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_SHADOWS),
    MENU_ITEM(0x5f,"Highlights", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_HIGHLIGHTS),
    MENU_ITEM(0x5f,"Blacks", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_BLACKS),
    MENU_ITEM(0x5f,"Whites", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_WHITES),
    MENU_ITEM(0x5f,"Fade", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_FADE),
    MENU_ITEM(0x5f,"Temperature", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_TEMPERATURE),
    MENU_ITEM(0x5f,"Tint", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_TINT),
    MENU_ITEM(0x5f,"Saturation", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_SATURATION),
    MENU_ITEM(0x5f,"Vibrance", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_VIBRANCE),
    MENU_ITEM(0x5f,"Black & white", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_MONO),
    MENU_ITEM(0x5f,"HSL colour", MENUITEM_ENUM, film_grui_menu_colour, 0),
    MENU_ITEM(0x5f,"Hue", MENUITEM_ENUM, film_grui_menu_hsl, 0),
    MENU_ITEM(0x5f,"Saturation", MENUITEM_ENUM, film_grui_menu_hsl, 1),
    MENU_ITEM(0x5f,"Lightness", MENUITEM_ENUM, film_grui_menu_hsl, 2),
    MENU_ITEM(0x5f,"Shadow tone", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_SHADOW_HUE),
    MENU_ITEM(0x5f,"Shadow amount", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_SHADOW_AMOUNT),
    MENU_ITEM(0x5f,"Highlight tone", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_HIGHLIGHT_HUE),
    MENU_ITEM(0x5f,"Highlight amount", MENUITEM_ENUM, film_grui_menu_field, FILM_GRADE_HIGHLIGHT_AMOUNT),
    MENU_ITEM(0x5f,"Sharpening", MENUITEM_ENUM, film_lut_menu_sharpen, 0),
    MENU_ITEM(0x28,"Reset grade", MENUITEM_PROC, film_grui_reset, 0),
    MENU_ITEM(0x28,"Save as new look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_SAVE),
    MENU_ITEM(0x28,"Update look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_UPDATE),
    MENU_ITEM(0x28,"Rename look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_RENAME),
    MENU_ITEM(0x28,"Delete look", MENUITEM_PROC, film_lut_look_action, FILM_LOOK_DELETE),
    MENU_ITEM(0x28,"Settings", MENUITEM_SUBMENU, &film_settings_menu, 0),
    MENU_ITEM(0x51,LANG_MENU_BACK, MENUITEM_UP, 0, 0),
    {0}
};
static CMenuItem *riso_rows(void)
{
    unsigned st = film_rsui_style();
    return st == 0 ? film_riso_menu_items : st == FILM_RISO_STYLE_TEXT ? film_print_text_items : film_print_style_items;
}
CMenu film_lut_menu = {0x28,(int)"Zrno menu",film_lut_menu_items};

int film_menu_ours(const CMenu *menu)
{
    return menu == &film_lut_menu || menu == &film_settings_menu;
}

/* "Re-develop photo" (the first item) only shows in the gallery; the rows
 * are the chosen engine's. */
static const CMenuItem *volatile preview_item;  /* the look preview's row, while it is up (cleared by the keyboard task) */
void film_menu_prepare(void)
{
    film_lut_look_disarm();
    preview_item = 0;               /* opened afresh: a preview left by another way out is over */
    film_lut_menu.menu = (film_gboy_engine() ? film_gboy_menu_items : film_riso_engine() ? riso_rows() :
                          film_grade_engine() ? film_grade_menu_items : film_lut_menu_items) +
                         !camera_info.state.mode_play;
}

int film_menu_showing(void)
{
    return camera_info.state.gui_mode == GUI_MODE_MENU && film_menu_ours(get_curr_menu());
}

int film_menu_palette(void)
{
    if ((camera_info.state.gui_mode == GUI_MODE_MENU && film_menu_ours(get_curr_menu())) || film_ui_themed())
        return 2;
    return film_ui_indicator_shown() && camera_info.state.gui_mode != GUI_MODE_MENU;
}

/* Layout in CHDK screen pixels (360 x 240; on the G12 each is a little
 * narrower than tall, so the corner insets run wider than they are tall). */
#define SCREEN_W 360
#define SCREEN_H 240
#define LEFT     6      /* rows span LEFT..RIGHT */
#define RIGHT    345
#define TOP      28     /* first row; the title sits above */
#define PAD      7      /* row height = font height + PAD */
#define TEXT_X   12     /* label inset from LEFT */
#define CARD_W   128    /* value card width */
#define CARD_IN  6      /* value card inset from RIGHT */
#define BAR_X    350    /* scroll bar, 3 px wide */

#define BG      FILM_BG
#define SURFACE FILM_SURFACE
#define ACCENT  FILM_ACCENT
#define MUTED   FILM_MUTED

/* Corner insets per row from the top or bottom edge: radius 6, 4 and 2. */
static const unsigned char round6[4] = {4, 2, 1, 1}, round4[4] = {2, 1, 0, 0}, round2[4] = {1, 0, 0, 0}, round0[4];

void film_menu_fill(int x1, int y1, int x2, int y2, int c, int out, int radius)
{
    const unsigned char *r = radius >= 6 ? round6 : radius >= 4 ? round4 : radius >= 2 ? round2 : round0;
    int y;
    for (y = y1; y <= y2; y++) {
        int d = y - y1 < y2 - y ? y - y1 : y2 - y, i = d < 4 ? r[d] : 0;
        if (i) {
            draw_hline(x1, y, i, out);
            draw_hline(x2 - i + 1, y, i, out);
        }
        draw_hline(x1 + i, y, x2 - x1 + 1 - 2 * i, c);
    }
}

/* Tear-free drawing. CHDK draws straight onto the visible bitmap, so a
 * card painted as background first and text after shows every pixel
 * twice per frame, and SpyTask repaints about every 40 ms: the overlays
 * flickered and the menu tore. The G12 also has two bitmap buffers and
 * CHDK draws only on the active one, so when Canon switched buffers an
 * overlay vanished until the next repaint.
 *
 * Between film_draw_begin and film_draw_end every CHDK draw lands in a
 * shadow copy of the screen (seeded with what is on screen, so pixels
 * not drawn stay as they are). The end copies the region to both bitmap
 * buffers, writing only bytes that differ: an unchanged frame writes
 * nothing, and each changed pixel goes straight to its final colour.
 * Without memory for the shadow, drawing is direct, as before.
 *
 * With the screen swung round to face the lens in shooting mode, Canon
 * mirrors its own drawing across the whole 960-byte buffer row (column x
 * goes to 959 - x, rows unchanged; measured over USB on Canon's Av and
 * flash icons) and the panel shows that mirrored window. The shadow is
 * composed as usual and written to the mirrored columns, so the overlays
 * read correctly there. */
extern char *bitmap_buffer[];
extern int active_bitmap_buffer;
extern void (*draw_pixel_proc_norm)(unsigned int offset, color cl);
extern unsigned int rotate_base;
static unsigned char *shadow;
static int shadow_failed, region[4], mirror;
static volatile int composing;
static void (*saved_proc)(unsigned int offset, color cl);
static void shadow_pixel(unsigned int offset, color cl)
{
    if (offset < (unsigned)camera_screen.buffer_size) shadow[offset] = cl;
}
/* CHDK x to buffer x: physical_width is ASPECT_XCORRECTION(width), which
 * module-side code cannot include (2 on the G12, which doubles each x). */
#define BUF_X(x) ((x) * camera_screen.physical_width / camera_screen.width)
int film_screen_facing_lens(void)
{
    /* Only the shooting screen: in playback Canon draws the right way
     * round whichever way the screen faces (seen over USB). */
    return camera_info.state.mode_rec &&
           (camera_info.state.mode & (MODE_SCREEN_OPENED | MODE_SCREEN_ROTATED)) == (MODE_SCREEN_OPENED | MODE_SCREEN_ROTATED);
}
/* Where shadow byte o goes on screen. */
static unsigned target(unsigned o)
{
    unsigned w = camera_screen.buffer_width;
    return mirror ? o - o % w + (w - 1 - o % w) : o;
}
/* Byte range of one row of the region in the buffer, rotation included. */
static void row_range(int y, unsigned *lo, unsigned *hi)
{
    unsigned a = y * camera_screen.buffer_width + BUF_X(region[0]);
    unsigned b = y * camera_screen.buffer_width + BUF_X(region[2] + 1) - 1;
    if (conf.rotate_osd) { unsigned t = rotate_base - b; b = rotate_base - a; a = t; }
    if (a == 0) a = 1;                               /* CHDK's guard pixel */
    if (b >= (unsigned)camera_screen.buffer_size) b = camera_screen.buffer_size - 1;
    *lo = a; *hi = b;
}
static int draw_begin(int x1, int y1, int x2, int y2, int seed)
{
    int y;
    if (composing) return 0;                         /* another task is mid-frame: skip this one */
    composing = 1;
    if (!shadow && !shadow_failed) {
        shadow = malloc(camera_screen.buffer_size);
        shadow_failed = !shadow;
    }
    if (!shadow) return 1;
    region[0] = x1 < 0 ? 0 : x1; region[1] = y1 < 0 ? 0 : y1;
    region[2] = x2 >= camera_screen.width ? camera_screen.width - 1 : x2;
    region[3] = y2 >= camera_screen.height ? camera_screen.height - 1 : y2;
    mirror = film_screen_facing_lens();
    for (y = region[1]; seed && y <= region[3]; y++) {
        unsigned lo, hi, o;
        const unsigned char *screen = (const unsigned char *)bitmap_buffer[active_bitmap_buffer];
        row_range(y, &lo, &hi);
        if (!mirror) { if (hi >= lo) memcpy(shadow + lo, screen + lo, hi - lo + 1); }
        else {
            /* The mirrored byte of o on its row, without a division per byte
             * (the camera has no divide instruction). */
            unsigned m = target(lo) + lo;
            for (o = lo; o <= hi; o++) shadow[o] = screen[m - o];
        }
    }
    saved_proc = draw_pixel_proc_norm;
    draw_set_draw_proc(shadow_pixel);
    return 1;
}
int film_draw_begin(int x1, int y1, int x2, int y2) { return draw_begin(x1, y1, x2, y2, 1); }
int film_draw_begin_cover(int x1, int y1, int x2, int y2) { return draw_begin(x1, y1, x2, y2, 0); }
unsigned char *film_draw_shadow(unsigned *stride)
{
    if (!shadow || !composing || conf.rotate_osd) return 0;
    *stride = camera_screen.buffer_width;
    return shadow;
}
int film_draw_clear(int c)
{
    int y;
    if (!shadow) return 0;
    for (y = region[1]; y <= region[3]; y++) {
        unsigned lo, hi;
        row_range(y, &lo, &hi);
        if (hi >= lo) memset(shadow + lo, c, hi - lo + 1);
    }
    return 1;
}
void film_draw_end(void)
{
    int y;
    if (shadow) {
        unsigned char *b0 = (unsigned char *)bitmap_buffer[0], *b1 = (unsigned char *)bitmap_buffer[1];
        draw_set_draw_proc(saved_proc);
        for (y = region[1]; y <= region[3]; y++) {
            unsigned lo, hi, o;
            row_range(y, &lo, &hi);
            /* Unmirrored, four bytes at a time where aligned (the buffers
             * and the shadow are word aligned): the full-screen developing
             * panel compares 170 KB a frame. */
            if (!mirror)
                for (o = lo; o <= hi; ) {
                    if (!(o & 3) && o + 3 <= hi) {
                        unsigned v = *(unsigned *)(shadow + o);
                        if (*(unsigned *)(b0 + o) != v) *(unsigned *)(b0 + o) = v;
                        if (*(unsigned *)(b1 + o) != v) *(unsigned *)(b1 + o) = v;
                        o += 4;
                    } else {
                        unsigned char v = shadow[o];
                        if (b0[o] != v) b0[o] = v;
                        if (b1[o] != v) b1[o] = v;
                        o++;
                    }
                }
            else {
                unsigned m = target(lo) + lo;            /* o goes to m - o on its row */
                for (o = lo; o <= hi; o++) {
                    unsigned char v = shadow[o];
                    unsigned t = m - o;
                    if (b0[t] != v) b0[t] = v;
                    if (b1[t] != v) b1[t] = v;
                }
            }
        }
    }
    composing = 0;
}

/* Small solid triangle pointing left (dir -1) or right (dir 1) at x. */
static void arrow(int x, int cy, int dir, color c)
{
    int k;
    for (k = 0; k < 5; k++)
        draw_vline(x + dir * (4 - k), cy - k, 2 * k + 1, c);
}

static int look_action(const CMenuItem *m)
{
    return (m->type & MENUITEM_MASK) == MENUITEM_PROC && m->value == (int *)film_lut_look_action;
}

/* Effect rows carry the shooting-screen indicator's icon for the effect;
 * the other rows carry icons of their own (menu_art). */
#define ICON_COL (FILM_UI_ICON_W + 6)           /* label shift in a menu with icons */
static int row_icon(const CMenuItem *m)
{
    static const char *(*const effect[FILM_UI_ICONS])(int, int) = {
        film_lut_menu_halo, film_lut_menu_bloom, film_lut_menu_soft, film_lut_menu_grain, film_lut_menu_vignette,
        film_lut_menu_leaks, film_lut_menu_toy, film_lut_menu_dust, film_lut_menu_stamp};
    int i;
    if ((m->type & MENUITEM_MASK) != MENUITEM_ENUM) return -1;
    for (i = 0; i < FILM_UI_ICONS; i++)
        if (m->value == (int *)effect[i]) return i;
    if (m->value == (int *)film_lut_menu_halo_colour) return FILM_UI_ICON_HALATION;   /* the glow's colour */
    return -1;
}

/* Icons for the other rows, in the indicator icons' size and colours, as
 * pixel art: '.' clear, 'W' white, 'O' orange, 'g' grey. */
enum {ART_REDEVELOP,ART_PROCESSING,ART_LOOK,ART_LUT,ART_SHARPEN,ART_STAMP_SHOWS,ART_STAMP_TEXT,ART_SAVE,ART_UPDATE,ART_RENAME,ART_DELETE,ART_SETTINGS,ART_BACK,ART_INDICATOR,ART_LEDS,ART_SBUTTON,ART_CHDK,
     ART_ENGINE,ART_PALETTE,ART_FRAME,ART_CONTRAST,ART_BRIGHTNESS,ART_EDGES,ART_DITHER,ART_RESOLUTION,
     ART_INKS,ART_SEPARATION,ART_SCREEN,ART_NOISE,ART_REGISTER,ART_FLAWS,ART_PAPER,ART_LIVE,ART_STYLE,
     ART_GRADE,ART_CURVE,ART_BALANCE,ART_SATURATION,ART_HUE,ART_SPLIT,ART_MONO,ART_RESET,ART_COUNT};
static const char menu_art[ART_COUNT][FILM_UI_ICON_H][FILM_UI_ICON_W + 1] = {
    { /* redevelop */
        "...WWWWO.....",
        "..W....OO....",
        ".W.....OOO...",
        "W......OO..W.",
        "W......O...W.",
        "W..........W.",
        "W..........W.",
        ".W........W..",
        "..W......W...",
        "...WWWWWW....",
        "............." },
    { /* processing */
        "......O......",
        "...WW.O.WW...",
        "..W...O...W..",
        ".W....O....W.",
        ".W....O....W.",
        ".W.........W.",
        ".W.........W.",
        "..W.......W..",
        "...WW...WW...",
        ".....WWW.....",
        "............." },
    { /* look */
        "...WWWWWWW...",
        "...WOOOOOW...",
        "...WOOOOOW...",
        "...WOOOOOW...",
        "...WOOOOOW...",
        "...WOOOOOW...",
        "...WOOOOOW...",
        "...WOOWOOW...",
        "...WOW.WOW...",
        "...WW...WW...",
        "...W.....W..." },
    { /* lut */
        "...WW........",
        ".WWWWWW......",
        ".W....WWWWWWW",
        ".WOOO.W.g.g.W",
        ".WOOO.W.....W",
        ".WOOO.W.....W",
        ".WOOO.W.g.g.W",
        ".W....WWWWWWW",
        ".WWWWWW......",
        "...WW........",
        "............." },
    { /* sharpen */
        "......W......",
        ".....WOW.....",
        ".....WOW.....",
        "....W.OOW....",
        "....W.OOW....",
        "...W..OOOW...",
        "...W..OOOW...",
        "..W...OOOOW..",
        "..W...OOOOW..",
        ".WWWWWWWWWWW.",
        "............." },
    { /* stamp shows */
        ".............",
        ".............",
        "..O...OOOO...",
        ".OO......O...",
        "..O......O...",
        "..O...OOOO...",
        "..O...O......",
        "..O...O......",
        ".OOO..OOOO...",
        ".............",
        "............." },
    { /* stamp text */
        ".............",
        ".WWWWWWW.....",
        ".W..W..W..O..",
        "....W.....O..",
        "....W.....O..",
        "....W.....O..",
        "....W.....O..",
        "....W.....O..",
        "...WWW....O..",
        ".............",
        "............." },
    { /* save */
        "....WWWWW....",
        "..WW.....WW..",
        ".W....O....W.",
        ".W....O....W.",
        "W.....O.....W",
        "W..OOOOOOO..W",
        "W.....O.....W",
        ".W....O....W.",
        ".W....O....W.",
        "..WW.....WW..",
        "....WWWWW...." },
    { /* update */
        "......O......",
        "......O......",
        "......O......",
        "......O......",
        "...O..O..O...",
        "....O.O.O....",
        ".....OOO.....",
        "W.....O.....W",
        "W...........W",
        "WWWWWWWWWWWWW",
        "............." },
    { /* rename */
        ".........WW..",
        "........W..W.",
        ".......W..W..",
        "......W..W...",
        ".....W..W....",
        "....W..W.....",
        "...W..W......",
        "..OOOW.......",
        "..OOW........",
        "..O..........",
        ".......gggggg" },
    { /* delete */
        "....WWWWW....",
        ".WWWWWWWWWWW.",
        "..W.......W..",
        "..W.g.g.g.W..",
        "..W.g.g.g.W..",
        "..W.g.g.g.W..",
        "..W.g.g.g.W..",
        "..W.g.g.g.W..",
        "..W.......W..",
        "...WWWWWWW...",
        "............." },
    { /* settings */
        ".....WWW.....",
        "..WW.WWW.WW..",
        "..WWWWWWWWW..",
        "...WW...WW...",
        "WWWW.....WWWW",
        "WWWW..O..WWWW",
        "WWWW.....WWWW",
        "...WW...WW...",
        "..WWWWWWWWW..",
        "..WW.WWW.WW..",
        ".....WWW....." },
    { /* back */
        ".............",
        "...O.........",
        "..OO.........",
        ".OOOWWWWWW...",
        "..OO......W..",
        "...O.......W.",
        "...........W.",
        "..........W..",
        "....WWWWWW...",
        ".............",
        "............." },
    { /* indicator */
        ".............",
        ".............",
        ".WWWWWWWWWWW.",
        "W...........W",
        "W.OO.WWW.g.gW",
        "W.OO.WWW.g.gW",
        "W...........W",
        ".WWWWWWWWWWW.",
        ".............",
        ".............",
        "............." },
    { /* leds: upper orange, lower one, as beside the viewfinder */
        ".....OOO.....",
        "....OOOOO....",
        "....OOOOO....",
        "....OOOOO....",
        ".....OOO.....",
        ".............",
        ".....ggg.....",
        "....ggggg....",
        "....ggggg....",
        "....ggggg....",
        ".....ggg....." },
    { /* sbutton */
        "....WWWWW....",
        "..WW.....WW..",
        ".W...OOO...W.",
        ".W..O...O..W.",
        "W...O.......W",
        "W....OOO....W",
        "W.......O...W",
        ".W..O...O..W.",
        ".W...OOO...W.",
        "..WW.....WW..",
        "....WWWWW...." },
    { /* chdk */
        ".............",
        ".OO..........",
        ".OO.WWWWWWWW.",
        ".............",
        ".OO..........",
        ".OO.WWWWWWWW.",
        ".............",
        ".OO..........",
        ".OO.WWWWWWWW.",
        ".............",
        "............." },
    { /* engine: a little cartridge */
        "..WWWWWWWW...",
        "..W......WW..",
        "..W.OOOOO..W.",
        "..W.O...O..W.",
        "..W.O...O..W.",
        "..W.OOOOO..W.",
        "..W........W.",
        "..W.gggggg.W.",
        "..W........W.",
        "..WWWWWWWWWW.",
        "...W.W.W.W..." },
    { /* palette: four swatches, light to dark */
        ".............",
        "WWWWWWWWWWWWW",
        "W..W.gWggWOOW",
        "W..W.gWggWOOW",
        "W..W.gWggWOOW",
        "W..W.gWggWOOW",
        "W..W.gWggWOOW",
        "WWWWWWWWWWWWW",
        ".............",
        ".............",
        "............." },
    { /* frame: a border round a picture */
        "WWWWWWWWWWWWW",
        "W.W.W.W.W.W.W",
        "WW.........WW",
        "W..OOOOOOO..W",
        "WW.O.....O.WW",
        "W..O..O..O..W",
        "WW.O.OOO.O.WW",
        "W..OOOOOOO..W",
        "WW.........WW",
        "W.W.W.W.W.W.W",
        "WWWWWWWWWWWWW" },
    { /* contrast: half dark, half light */
        "....WWWWW....",
        "..WWOOOO.WW..",
        ".WOOOOOO...W.",
        ".WOOOOOO...W.",
        "WOOOOOOO....W",
        "WOOOOOOO....W",
        "WOOOOOOO....W",
        ".WOOOOOO...W.",
        ".WOOOOOO...W.",
        "..WWOOOO.WW..",
        "....WWWWW...." },
    { /* brightness: a sun */
        "......W......",
        "..W...W...W..",
        "...W.....W...",
        ".....OOO.....",
        "....OOOOO....",
        "WW..OOOOO..WW",
        "....OOOOO....",
        ".....OOO.....",
        "...W.....W...",
        "..W...W...W..",
        "......W......" },
    { /* edges: a square traced in light and dark */
        ".............",
        ".WWWWWWWWWWW.",
        ".WgggggggggW.",
        ".Wg.......gW.",
        ".Wg..OOO..gW.",
        ".Wg..OOO..gW.",
        ".Wg..OOO..gW.",
        ".Wg.......gW.",
        ".WgggggggggW.",
        ".WWWWWWWWWWW.",
        "............." },
    { /* dither: a checkerboard fading out */
        "W.W.W.W.W.W.W",
        ".W.W.W.W.W.W.",
        "W.W.W.W.W.W.W",
        ".O.O.O.O.O.O.",
        "O.O.O.O.O.O.O",
        ".O.O.O.O.O.O.",
        "g...g...g...g",
        "..g...g...g..",
        "g...g...g...g",
        ".............",
        "............." },
    { /* resolution: coarse pixels, then fine ones */
        ".............",
        "WW..WW.O.O.O.",
        "WW..WW..O.O.O",
        "..WW...O.O.O.",
        "..WW....O.O.O",
        "WW..WW.O.O.O.",
        "WW..WW..O.O.O",
        "..WW...O.O.O.",
        "..WW....O.O.O",
        ".............",
        "............." },
    { /* inks: two inks overlapping */
        ".............",
        "...WWW.......",
        ".WWWWWWW.....",
        ".WWWWWOOOO...",
        "WWWWWOOOOOO..",
        "WWWWOOOOOOOO.",
        "WWWWOOOOOOOO.",
        ".WWWOOOOOOOO.",
        "..WWWOOOOOO..",
        ".....OOOOO...",
        "............." },
    { /* separation: one tone to two inks */
        "WWWWW........",
        "WWWWW........",
        "WWWWW........",
        "WWWWW...WWOO.",
        "WWWWW..WWOOO.",
        "WWWWW.gWWOOO.",
        "WWWWW..WWOOO.",
        "WWWWW...WWOO.",
        "WWWWW........",
        "WWWWW........",
        "WWWWW........" },
    { /* screen: dots growing along a row */
        ".............",
        ".............",
        "..........OO.",
        "......OO.OOOO",
        "..O..OOO.OOOO",
        ".OOO.OOO..OO.",
        "..O...O......",
        ".............",
        "gg.gg.gg.gg.g",
        ".............",
        "............." },
    { /* noise: grain scattered */
        "W...O....W...",
        "..W....W...O.",
        ".O..W.O...W..",
        "....OO..W....",
        "W.W....OO..W.",
        "...O.W....O..",
        ".W....O.W....",
        "..OO.W...O.W.",
        "W.....W.O....",
        "..W.O....W..O",
        ".O....W...W.." },
    { /* register: two prints out of line */
        "WWWWWWWW.....",
        "W......W.....",
        "W..OOOOOOOO..",
        "W..O...W..O..",
        "W..O...W..O..",
        "W..O...W..O..",
        "WWWOWWWW..O..",
        "...O......O..",
        "...OOOOOOOO..",
        ".............",
        "............." },
    { /* print flaws: a solid with specks and a streak */
        "OOOOOOOOOOOOO",
        "OOOO.OOOgOOOO",
        "OOOOOOOOgOOOO",
        "OO..OOOOgO.OO",
        "OOOOOOOOgOOOO",
        "OOOOOO.OgOOOO",
        "O.OOOOOOgOOOO",
        "OOOOOOOOgOO.O",
        "OOOO..OOgOOOO",
        "OOOOOOOOgOOOO",
        "OOOOOOOOOOOOO" },
    { /* paper: a sheet with a folded corner */
        "..WWWWWWW....",
        "..W.....WW...",
        "..W.....WgW..",
        "..W.....WggW.",
        "..W.....WWWW.",
        "..W........W.",
        "..W.OOOO...W.",
        "..W........W.",
        "..W.OOOOOO.W.",
        "..W........W.",
        "..WWWWWWWWWW." },
    { /* live preview: a screen showing a picture */
        ".............",
        "WWWWWWWWWWWWW",
        "W...........W",
        "W.OOO.......W",
        "W.OOO...g...W",
        "W......ggg..W",
        "W.....ggggg.W",
        "W..gggggggggW",
        "WWWWWWWWWWWWW",
        ".....WWW.....",
        "...WWWWWWW..."},
    { /* style: a brush, a pen nib and a stamp */
        ".............",
        "OO.......W...",
        "OOO.....WWW..",
        ".OOO...WW.WW.",
        "..OOO..W...W.",
        "...WW..WW.WW.",
        "...WW...WWW..",
        "........gWg..",
        ".gggggg.gWg..",
        ".gWWWWg......",
        ".gggggg......"},
    { /* grade: three sliders */
        "..O..........",
        "WOOOWWWWWWWW.",
        "..O..........",
        "........O....",
        "WWWWWWWOOOWW.",
        "........O....",
        ".....O.......",
        "WWWWOOOWWWWW.",
        ".....O.......",
        ".............",
        "............."},
    { /* tone: a curve over its axes */
        "W............",
        "W.........OO.",
        "W........O...",
        "W.......O....",
        "W......O.....",
        "W.....O......",
        "W....O.......",
        "W...O........",
        "W.OO.........",
        "WWWWWWWWWWWW.",
        "............."},
    { /* colour balance: a thermometer */
        ".....WWW.....",
        ".....W.W..gg.",
        ".....WOW.....",
        ".....WOW..gg.",
        ".....WOW.....",
        ".....WOW..gg.",
        "....WOOOW....",
        "...WOOOOOW...",
        "...WOOOOOW...",
        "....WOOOW....",
        ".....WWW....."},
    { /* saturation: a drop of colour */
        "......W......",
        "......W......",
        ".....WOW.....",
        ".....WOW.....",
        "....WOOOW....",
        "...WOOOOOW...",
        "...WOOOOOW...",
        "...WOOOOOW...",
        "....WOOOW....",
        ".....WWW.....",
        "............."},
    { /* hue: a colour wheel */
        "....OOOOO....",
        "..OO.....OO..",
        ".O.........W.",
        ".O.........W.",
        "O...........W",
        "O.....g.....W",
        "O...........W",
        ".g.........W.",
        ".g.........W.",
        "..gg.....WW..",
        "....ggggg...."},
    { /* split toning: a dark half and a light half */
        "....gggOO....",
        "..gggggOOOO..",
        ".ggggggOOOOO.",
        ".ggggggOOOOO.",
        "gggggggOOOOOO",
        "gggggggOOOOOO",
        "gggggggOOOOOO",
        ".ggggggOOOOO.",
        ".ggggggOOOOO.",
        "..gggggOOOO..",
        "....gggOO...."},
    { /* black and white: a filter's orange ring, a white half and a grey half */
        "....OOOOO....",
        "..OOWWWggOO..",
        ".OWWWWWggggO.",
        ".OWWWWWggggO.",
        "OWWWWWWgggggO",
        "OWWWWWWgggggO",
        "OWWWWWWgggggO",
        ".OWWWWWggggO.",
        ".OWWWWWggggO.",
        "..OOWWWggOO..",
        "....OOOOO...."},
    { /* reset: an arrow turning back */
        "....WWWWW....",
        "..WW.....W...",
        ".W........W..",
        ".W.........O.",
        "W.........OOO",
        "W..........O.",
        "W............",
        ".W...........",
        ".W........W..",
        "..WW.....W...",
        "....WWWWW...."}
};

static void draw_art(int x, int y, int art, color white, color orange, color grey)
{
    int r, c;
    for (r = 0; r < FILM_UI_ICON_H; r++)
        for (c = 0; c < FILM_UI_ICON_W; c++) {
            char p = menu_art[art][r][c];
            if (p != '.') draw_pixel(x + c, y + r, p == 'W' ? white : p == 'O' ? orange : grey);
        }
}

/* The art for a row without an effect icon, or -1. */
static int row_art(const CMenuItem *m)
{
    static const struct { const char *(*value)(int, int); int art; } enums[] = {
        {film_lut_menu_processing, ART_PROCESSING}, {film_lut_menu_look, ART_LOOK}, {film_lut_menu_choice, ART_LUT},
        {film_lut_menu_sharpen, ART_SHARPEN}, {film_lut_menu_stamp_mode, ART_STAMP_SHOWS}, {film_lut_menu_stamp_text, ART_STAMP_TEXT},
        {film_lut_menu_indicator, ART_INDICATOR}, {film_lut_menu_leds, ART_LEDS}, {film_lut_menu_sbutton, ART_SBUTTON},
        {film_gbui_menu_engine, ART_ENGINE}, {film_gbui_menu_palette, ART_PALETTE}, {film_gbui_menu_frame, ART_FRAME},
        {film_gbui_menu_contrast, ART_CONTRAST}, {film_gbui_menu_brightness, ART_BRIGHTNESS}, {film_gbui_menu_edges, ART_EDGES},
        {film_gbui_menu_dither, ART_DITHER}, {film_gbui_menu_resolution, ART_RESOLUTION},
        {film_rsui_menu_inks, ART_INKS}, {film_rsui_menu_separation, ART_SEPARATION}, {film_rsui_menu_screen, ART_SCREEN},
        {film_rsui_menu_noise, ART_NOISE}, {film_rsui_menu_register, ART_REGISTER}, {film_rsui_menu_flaws, ART_FLAWS},
        {film_rsui_menu_paper, ART_PAPER}, {film_gbui_menu_live, ART_LIVE}, {film_rsui_menu_style, ART_STYLE},
        {film_rsui_menu_palette, ART_PALETTE},
        {film_grui_menu_slot, ART_GRADE}, {film_grui_menu_colour, ART_HUE}, {film_grui_menu_hsl, ART_HUE}};
    static const int actions[] = {ART_SAVE, ART_UPDATE, ART_RENAME, ART_DELETE};   /* FILM_LOOK_* order */
    unsigned i;
    switch (m->type & MENUITEM_MASK) {
    case MENUITEM_ENUM:
        if (m->value == (int *)film_grui_menu_field) {
            /* Grade's rows share one callback; the art follows the field. */
            switch (m->arg) {
            case FILM_GRADE_CONTRAST: return ART_CONTRAST;
            case FILM_GRADE_MIDTONES: return ART_BRIGHTNESS;
            case FILM_GRADE_SHADOWS: case FILM_GRADE_HIGHLIGHTS: case FILM_GRADE_FADE:
            case FILM_GRADE_BLACKS: case FILM_GRADE_WHITES: return ART_CURVE;
            case FILM_GRADE_MONO: return ART_MONO;
            case FILM_GRADE_TEMPERATURE: case FILM_GRADE_TINT: return ART_BALANCE;
            case FILM_GRADE_SATURATION: case FILM_GRADE_VIBRANCE: return ART_SATURATION;
            default: return ART_SPLIT;
            }
        }
        for (i = 0; i < sizeof(enums) / sizeof(enums[0]); i++)
            if (m->value == (int *)enums[i].value) return enums[i].art;
        return -1;
    case MENUITEM_PROC:
        if (look_action(m) && m->arg >= 0 && m->arg < 4) return actions[m->arg];
        if (m->value == (int *)module_run) return ART_REDEVELOP;
        if (m->value == (int *)film_open_chdk_menu) return ART_CHDK;
        if (m->value == (int *)film_grui_reset) return ART_RESET;
        return -1;
    case MENUITEM_SUBMENU:
        return m->value == (int *)&film_settings_menu ? ART_SETTINGS : -1;
    case MENUITEM_UP:
        return ART_BACK;
    }
    return -1;
}

/* The Zrno logo: a cartoon coffee bean ("zrno" is a grain, a bean), orange
 * with a bold navy S-shaped crease, tilted 45 degrees. Computed for the
 * G12's pixels (about 0.89 as wide as tall) so it sits at 45 degrees on
 * screen, and the crease is two pixels wide so it reads at this size. 'O'
 * orange, 'B' the value cards' navy. It stands left of each Zrno menu's
 * title, in the icon column. */
#define LOGO_W 14
#define LOGO_H 13
static const char logo_art[LOGO_H][LOGO_W + 1] = {
    ".......OOOOOO.",
    ".....OOOOOOOOO",
    "...OOOOOOOBOOO",
    "..OOOOOOOOBBOO",
    ".OOOOOOOOBBBOO",
    ".OOOOOOOBBBOOO",
    "OOOOBBBBBBOOOO",
    "OOOBBBOOOOOOO.",
    "OOBBBOOOOOOOO.",
    "OOBBOOOOOOOO..",
    "OOOBOOOOOOO...",
    "OOOOOOOOO.....",
    ".OOOOOO......."
};

static void draw_logo(int x, int y)
{
    int r, c;
    for (r = 0; r < LOGO_H; r++)
        for (c = 0; c < LOGO_W; c++) {
            char p = logo_art[r][c];
            if (p != '.') draw_pixel(x + c, y + r, p == 'O' ? ACCENT : SURFACE);
        }
}

/* A row's label: the look actions name the look they act on (and dim when
 * idle), Grade's HSL rows name the chosen colour, Reset its grade. */
static const char *row_label(const CMenuItem *m, int *enabled)
{
    *enabled = 1;
    if (look_action(m)) return film_lut_look_label(m->arg, enabled);
    if ((m->type & MENUITEM_MASK) == MENUITEM_ENUM && m->value == (int *)film_grui_menu_hsl) return film_grui_hsl_label(m->arg);
    if ((m->type & MENUITEM_MASK) == MENUITEM_PROC && m->value == (int *)film_grui_reset) return film_grui_reset_label(enabled);
    return lang_str(m->text);
}

/* indent: ICON_COL in a menu with icons (every label lines up after the
 * icon column), else 0. */
static void draw_row(const CMenuItem *m, int y, int rh, int h, int sel, int indent)
{
    int type = m->type & MENUITEM_MASK, ty = y + (rh - h) / 2, enabled = 1, icon = row_icon(m);
    color bg = sel ? ACCENT : BG, fg = sel ? BG : COLOR_WHITE;
    const char *label = row_label(m, &enabled);
    if (!enabled && !sel) fg = MUTED;
    film_menu_fill(LEFT, y, RIGHT, y + rh - 1, bg, BG, sel ? 6 : 4);
    if (type == MENUITEM_TEXT) {
        rbf_draw_string(LEFT + TEXT_X + indent, ty, lang_str(m->text), MAKE_COLOR(BG, MUTED));
        return;
    }
    int x2 = RIGHT - CARD_IN, x1 = x2 - CARD_W + 1;
    rbf_draw_string_len(LEFT + TEXT_X + indent, ty, (type == MENUITEM_ENUM ? x1 - 6 : RIGHT - 20) - LEFT - TEXT_X - indent, label, MAKE_COLOR(bg, fg));
    {
        /* The icon: in colour, grey while the value is Off or the action
         * has nothing to do (as the value or label), dark on the orange
         * cursor (as the row's text). */
        int art = icon < 0 ? row_art(m) : -1, dim = !enabled, ix = LEFT + TEXT_X, iy = y + (rh - FILM_UI_ICON_H) / 2;
        if (type == MENUITEM_ENUM) dim |= !strcmp(((const char *(*)(int, int))(m->value))(0, m->arg), "Off");
        if (m->value == (int *)film_lut_menu_halo_colour) dim |= !strcmp(film_lut_menu_halo(0, 0), "Off");   /* no glow to colour */
        color white = sel ? BG : dim ? MUTED : COLOR_WHITE, orange = sel ? BG : dim ? MUTED : ACCENT, grey = sel ? BG : MUTED;
        if (icon >= 0) film_ui_icon(ix, iy, icon, white, orange, grey);
        else if (art >= 0) draw_art(ix, iy, art, white, orange, grey);
    }
    if (type == MENUITEM_ENUM) {
        const char *v = ((const char *(*)(int, int))(m->value))(0, m->arg);
        color cb = sel ? BG : SURFACE, cf = sel || (strcmp(v, "Off") && !(m->value == (int *)film_lut_menu_halo_colour && !strcmp(film_lut_menu_halo(0, 0), "Off"))) ? COLOR_WHITE : MUTED;
        int w = rbf_str_width(v), cy = y + rh / 2;
        if (w > CARD_W - 28) w = CARD_W - 28;
        film_menu_fill(x1, y + 3, x2, y + rh - 4, cb, bg, 4);
        rbf_draw_string_len(x1 + (CARD_W - w) / 2, ty, w, v, MAKE_COLOR(cb, cf));
        if (sel) {
            arrow(x1 + 7, cy, -1, ACCENT);
            arrow(x2 - 7, cy, 1, ACCENT);
        }
    } else {
        arrow(RIGHT - 12, y + rh / 2, type == MENUITEM_UP ? -1 : 1, sel ? BG : MUTED);
    }
}

/* The status sits at the right of the title bar, across from the title,
 * and only when it is not Ready: grey while a change is on its way, orange
 * when something went wrong. Drawn on every pass, as it changes on its own. */
static void draw_status(const CMenu *menu, int h)
{
    const char *s = film_lut_menu_status(0, 0);
    int y = (TOP - h) / 2 - 1, x1 = LEFT + TEXT_X + ICON_COL + rbf_str_width(lang_str(menu->title)) + 12, x2 = RIGHT - CARD_IN, w;
    color c = !strcmp(s, "Updating") || !strcmp(s, "After capture") || !strcmp(s, "Starting") ? MUTED : ACCENT;
    draw_rectangle(x1, 0, SCREEN_W - 1, TOP - 3, MAKE_COLOR(BG, BG), RECT_BORDER0 | DRAW_FILLED);
    if (!strcmp(s, "Ready")) return;
    w = rbf_str_width(s);
    if (w > x2 - x1) w = x2 - x1;
    rbf_draw_string_len(x2 - w, y, w, s, MAKE_COLOR(BG, c));
}

/* Look preview. SET on the LUT row (or the Look row, once looks are saved)
 * in shooting mode hides the menu over the live view and leaves a small card
 * at the bottom, in the menu's theme: the row's value between arrows, and a
 * tab naming the row. Left/right or the back dial step the row as in the
 * menu; the change is applied as usual and film_lut.c reloads Canon's colour
 * table, so the live view shows the new colours. SET or MENU returns to the
 * menu on the same row. While the colours cannot show (Processing Off, or
 * My Colors set to something else), the tab says so in orange. The mode
 * counts as a menu mode, so CHDK's OSD stays off and the theme palette on. */
#define PV_W   220                       /* card width */
#define PV_Y   186                       /* card top: the indicator's row, clear of Canon's icons */
static int preview_dirty;
static void preview_close(void);

/* The card itself, inside a frame being composed. */
/* The card draws from one reading of preview_item: the keyboard task can
 * close the preview (and clear it) while SpyTask is drawing, and a second
 * reading then found it gone (a jump through address 0 in SpyTask, seen on
 * the camera). */
static int item_gboy(const CMenuItem *m);
static void preview_card(void)
{
    const char *v, *tab;
    const CMenuItem *m = preview_item;
    int h, x1 = (SCREEN_W - PV_W) / 2, x2 = x1 + PV_W - 1, y = PV_Y, ty = y - FILM_UI_TAB_H, w, cy, gboy;
    color tc;
    if (!m) return;
    gboy = item_gboy(m);
    v = ((const char *(*)(int, int))(m->value))(0, m->arg);
    if (!film_lut_processing()) { tab = "Processing is Off"; tc = ACCENT; }
    else if (gboy && film_route_reason() != FILM_ROUTE_READY) { tab = "Paused: see the indicator"; tc = ACCENT; }
    else if (!gboy && !film_preview_reloadable()) { tab = "My Colors is not Off"; tc = ACCENT; }
    else { int on; tab = row_label(m, &on); tc = MUTED; }
    h = rbf_film_font(FILM_MENU_FONT, 1);
    if (!gboy)                               /* over ZrnoBoy's screen, which is redrawn whole */
        film_menu_fill(x1, ty, x2 + 2, y - 1, COLOR_TRANSPARENT, COLOR_TRANSPARENT, 0);   /* the last tab's rows */
    film_menu_fill(x1, y, x2, y + h + 4, BG, COLOR_TRANSPARENT, 6);
    film_ui_tab(x1, ty, x1 + 10 + film_ui_tiny_width(tab) + 6, y, BG);
    film_ui_tiny_text(x1 + 10, ty + 3, tab, tc);
    cy = y + (h + 4) / 2;
    arrow(x1 + 9, cy, -1, ACCENT);
    arrow(x2 - 9, cy, 1, ACCENT);
    w = rbf_str_width(v);
    if (w > PV_W - 48) w = PV_W - 48;
    rbf_draw_string_len(x1 + (PV_W - w) / 2, y + 2, w, v, MAKE_COLOR(BG, COLOR_WHITE));
    rbf_film_font(0, 0);
}

static void preview_draw(int enforce)
{
    int h, x1 = (SCREEN_W - PV_W) / 2, x2 = x1 + PV_W - 1, composed;
    const CMenuItem *m = preview_item;
    static int was_live;
    if (!m) return;
    /* ZrnoBoy: the live screen is drawn on every pass, the card over it. On
     * the Look row a look can leave ZrnoBoy or Print for Film or Grade: the
     * live screen is then erased (film_gbui_live_draw returns 0) and the
     * card drawn again on its own. */
    if ((item_gboy(m) || m->value == (int *)film_lut_menu_look) && film_gbui_live_draw(preview_card)) { preview_dirty = 0; was_live = 1; return; }
    if (was_live) { was_live = 0; preview_dirty = 1; }
    if (!enforce && !preview_dirty) return;
    preview_dirty = 0;
    h = rbf_film_font(FILM_MENU_FONT, 1);
    rbf_film_font(0, 0);
    composed = film_draw_begin(x1, PV_Y - FILM_UI_TAB_H, x2 + 2, PV_Y + h + 4);
    preview_card();
    if (composed) film_draw_end();
}

static void preview_step(int direction)
{
    const CMenuItem *m = preview_item;
    if (!m) return;
    ((const char *(*)(int, int))(m->value))(direction, m->arg);
    preview_dirty = 1;
}

static int preview_kbd(void)
{
    if (!preview_item) return 0;
    switch (kbd_get_autoclicked_key() | get_jogdial_direction()) {
    case KEY_LEFT:
    /* The back dial. CHDK's G12 names its dials the other way round: its
     * JOGDIAL ("rear") codes come from the front dial by the shutter button
     * (seen on the camera). Canon does not see either here. */
    case FRONTDIAL_LEFT:
        preview_step(-1);
        break;
    case KEY_RIGHT:
    case FRONTDIAL_RIGHT:
        preview_step(1);
        break;
    case KEY_SET:
        preview_close();
        break;
    case KEY_SHOOT_HALF:
        /* A half-press leaves for the camera, as S does, from the menus too. */
        preview_item = 0;
        exit_alt();
        break;
    }
    return 0;
}

/* Rows that open the preview: the LUT and Look (once looks are saved),
 * every ZrnoBoy and Print row but Engine, and every Grade value row
 * (over Canon's own live view, which shows the grade). */
static int preview_row(const CMenuItem *m)
{
    static const char *(*const gboy[])(int, int) = {film_gbui_menu_palette, film_gbui_menu_frame, film_gbui_menu_contrast,
        film_gbui_menu_brightness, film_gbui_menu_edges, film_gbui_menu_dither, film_gbui_menu_resolution,
        film_rsui_menu_inks, film_rsui_menu_separation, film_rsui_menu_screen, film_rsui_menu_noise,
        film_rsui_menu_register, film_rsui_menu_flaws, film_rsui_menu_paper, film_rsui_menu_style, film_rsui_menu_palette,
        film_grui_menu_slot, film_grui_menu_field, film_grui_menu_colour, film_grui_menu_hsl};
    unsigned i;
    for (i = 0; i < sizeof(gboy) / sizeof(gboy[0]); i++)
        if (m->value == (int *)gboy[i]) return 1;
    return m->value == (int *)film_lut_menu_choice || (m->value == (int *)film_lut_menu_look && film_lut_looks_saved());
}
/* Rows previewed on the ZrnoBoy or Print screen: every row while either is
 * the engine (the Look row too, as a look may switch the engine). */
static int item_gboy(const CMenuItem *m)
{
    return m && film_engine_alt() && !film_grade_engine();
}

gui_handler filmPreviewGuiHandler = { GUI_MODE_MENU, preview_draw, preview_kbd, preview_close, 0, 0 };

int film_preview_open(const CMenuItem *m)
{
    if (!camera_info.state.mode_rec || (m->type & MENUITEM_MASK) != MENUITEM_ENUM) return 0;
    if (!preview_row(m)) return 0;
    preview_item = m;
    preview_dirty = 1;
    gui_set_mode(&filmPreviewGuiHandler);       /* erases the menu: the live view shows */
    return 1;
}

static void preview_close(void)
{
    preview_item = 0;
    gui_set_mode(&menuGuiHandler);              /* the menu again, on the same row */
}

/* Only while the preview is the GUI mode: leaving <ALT> mode (the S button)
 * or a script returns CHDK to the shooting screen without preview_close,
 * and a preview left set kept ZrnoBoy's live screen off until another
 * preview was opened and closed (seen on the camera). */
int film_preview_showing(void)
{
    return preview_item != 0 && camera_info.state.gui_mode == GUI_MODE_MENU;
}

int film_menu_draw(const CMenu *menu, int *top, int curr, int full)
{
    const CMenuItem *items = menu->menu;
    int h = rbf_film_font(FILM_MENU_FONT, 1), rh = h + PAD, rows = (SCREEN_H - TOP - 4) / rh, count = 0, i;
    int top0 = TOP + (SCREEN_H - TOP - 4 - rows * rh) / 2;     /* centre the rows that fit */
    int composed;
    int indent = 0;
    if ((film_gbui_engine_changed() | film_rsui_style_changed()) && menu == &film_lut_menu) {
        /* Engine or Style changed: those rows, the cursor still on the row. */
        film_menu_prepare();
        items = menu->menu;
        full = 1;
    }
    while (items[count].text) {
        if (row_icon(&items[count]) >= 0 || row_art(&items[count]) >= 0) indent = ICON_COL;
        count++;
    }
    if (rows > count) rows = count;
    if (*top > count - rows) *top = count - rows;
    if (curr >= 0 && curr < *top) *top = curr;
    if (curr >= *top + rows) *top = curr - rows + 1;
    if (*top < 0) *top = 0;
    if (curr < 0 || curr >= count || !look_action(&items[curr]) || items[curr].arg != FILM_LOOK_DELETE)
        film_lut_look_disarm();          /* deleting takes SET twice on the row */
    if (curr < 0 || curr >= count || (items[curr].type & MENUITEM_MASK) != MENUITEM_PROC || items[curr].value != (int *)film_grui_reset)
        film_grui_reset_disarm();        /* so does Reset grade */
    composed = film_draw_begin(0, 0, SCREEN_W - 1, SCREEN_H - 1);
    if (full) {
        draw_rectangle(0, 0, SCREEN_W - 1, SCREEN_H - 1, MAKE_COLOR(BG, BG), RECT_BORDER0 | DRAW_FILLED);
        draw_logo(LEFT + TEXT_X + (FILM_UI_ICON_W - LOGO_W) / 2, (TOP - LOGO_H) / 2 - 1);
        rbf_draw_string(LEFT + TEXT_X + ICON_COL, (TOP - h) / 2 - 1, lang_str(menu->title), MAKE_COLOR(BG, COLOR_WHITE));
    }
    draw_status(menu, h);
    for (i = 0; i < rows; i++)
        draw_row(&items[*top + i], top0 + i * rh, rh, h, *top + i == curr, indent);
    if (count > rows) {
        int track = rows * rh, len = track * rows / count, pos = top0 + (track - len) * *top / (count - rows);
        draw_rectangle(BAR_X, top0, BAR_X + 2, top0 + track - 1, MAKE_COLOR(BG, BG), RECT_BORDER0 | DRAW_FILLED);
        film_menu_fill(BAR_X, pos, BAR_X + 2, pos + len - 1, MUTED, BG, 2);
    }
    if (composed) film_draw_end();
    rbf_film_font(0, 0);
    return rows;
}
