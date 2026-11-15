/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Mac menu simulator, CHDK side: the camera's own gui_menu.c, gui_draw.c,
 * rbf_font.c and film_menu.c run against this stand-in for the rest of
 * CHDK (screen, palette, keys, GUI modes, configuration). Compiled with the
 * CHDK headers; host I/O lives in sim_io.c. */
#include "camera_info.h"
#include "conf.h"
#include "keyboard.h"
#include "font.h"
#include "lang.h"
#include "gui.h"
#include "gui_draw.h"
#include "gui_menu.h"
#include "gui_lang.h"
#include "gui_palette.h"
#include "modes.h"
#include "film_menu.h"
#include "film_ui.h"
#include "film_queue.h"
#include "film_gbui.h"
#include "platform_palette.h"
extern void set_palette(void);

#define SIM_W 360
#define SIM_H 240
#define BUF_W 960
#define BUF_H 270

/* Screen: G12's 960 x 270 bitmap; CHDK's 360 x 240 view doubles each x. */
static char screen0[BUF_W * BUF_H], screen1[BUF_W * BUF_H];
char *bitmap_buffer[2] = {screen0, screen1};
int active_bitmap_buffer = 0;
_cam_screen camera_screen;
_cam_info camera_info;
int gui_user_menu_flag;
char palette_control;

/* Palette loaded by CHDK into Canon's (lib.c values), the film menu's set
 * while it is up, and Canon's own white, greys and black (approximate). */
static const unsigned chdk_pal[14] = {
    0x33ADF62, 0x326EA40, 0x34CD57F, 0x373BFAE, 0x34BD6CA, 0x395AB95, 0x34766F0,
    0x31250F3, 0x37F408F, 0x3512D5B, 0x3A9A917, 0x3819137, 0x3DED115, 0x0090000};

static void yuv_rgb(unsigned e, unsigned char *rgb, unsigned char *alpha)
{
    int y = (e >> 16) & 255, u = (signed char)(e >> 8), v = (signed char)e, c[3], i;
    c[0] = y + (1402 * v) / 1000;
    c[1] = y - (344 * u + 714 * v) / 1000;
    c[2] = y + (1772 * u) / 1000;
    for (i = 0; i < 3; i++) rgb[i] = c[i] < 0 ? 0 : c[i] > 255 ? 255 : c[i];
    *alpha = (e >> 24) ? 255 : 150;
}

/* RGBA for each palette index (alpha 0: the scene shows through). */
void sim_palette(unsigned char *rgba)
{
    int i, film = film_menu_palette();
    for (i = 0; i < 256; i++) {
        unsigned char *p = rgba + 4 * i;
        p[0] = 255; p[1] = 0; p[2] = 255; p[3] = 255;          /* unknown: magenta */
        if (i == 0) p[3] = 0;
        else if (i == 0xFF) p[0] = p[1] = p[2] = 0;
        else if (i == 0x01) p[0] = p[1] = p[2] = 255;
        else if (i == 0x16) p[0] = p[1] = p[2] = 128;
        else if (i == 0x1a) p[0] = p[1] = p[2] = 64;
        else if (i == 0x0E) p[0] = p[1] = p[2] = 190;
        else if (i >= CHDK_COLOR_BASE && i < CHDK_COLOR_BASE + 14) {
            int k = i - CHDK_COLOR_BASE;
            unsigned e = chdk_pal[k];
            if (film && k == FILM_PAL_SLOT_ICON) e = FILM_PAL_ACCENT;
            e = film_gbui_palette_slot(k, e);                   /* ZrnoBoy's shades while its live view shows */
            if (film == 2) {
                if (k == FILM_PAL_SLOT_BG) e = FILM_PAL_BG;
                if (k == FILM_PAL_SLOT_SURFACE) e = FILM_PAL_SURFACE;
                if (k == FILM_PAL_SLOT_ACCENT) e = FILM_PAL_ACCENT;
                if (k == FILM_PAL_SLOT_MUTED) e = FILM_PAL_MUTED;
            }
            yuv_rgb(e, p, p + 3);
        }
    }
}

/* The screen swung round to face the lens: Canon mirrors its drawing
 * across the 960-byte row and the panel shows it the right way round. */
#define SIM_FLIPPED (MODE_SCREEN_OPENED | MODE_SCREEN_ROTATED)
static int flipped(void) { return camera_info.state.mode_rec && (camera_info.state.mode & SIM_FLIPPED) == SIM_FLIPPED; }

/* The 360 x 240 view as palette indexes, as the panel shows it. */
void sim_indexes(unsigned char *out)
{
    int x, y;
    for (y = 0; y < SIM_H; y++)
        for (x = 0; x < SIM_W; x++)
            out[y * SIM_W + x] = (unsigned char)bitmap_buffer[active_bitmap_buffer][y * BUF_W + (flipped() ? BUF_W - 1 - 2 * x : 2 * x)];
}

/* --- Configuration, language, colours ---------------------------------- */
static const char *lang_strings[] = {[LANG_MENU_BACK] = "Back"};
char *lang_str(long str)
{
    if ((unsigned long)str < sizeof(lang_strings) / sizeof(lang_strings[0]))
        return (char *)(lang_strings[str] ? lang_strings[str] : "?");
    return (char *)str;
}
unsigned int lang_strhash31(int langid) { return langid; }
void conf_save(void) {}
extern void conf_load_defaults(void);
extern void sim_osd_init(void);
void set_tv_override_menu(CMenu *menu) { (void)menu; }
const char *gui_change_enum2(const CMenuItem *menu_item, int change) { (void)menu_item; (void)change; return ""; }
void gui_load_user_menu_script(const char *fn) { (void)fn; }
libpalette_sym *libpalette;
void vid_bitmap_refresh(void) {}
void *umalloc(long size);
void *sim_malloc(long size);
void *umalloc(long size) { return sim_malloc(size); }
void ufree(void *p);
long get_tick_count(void);

/* --- Keys -------------------------------------------------------------- */
static int pending_key;
long kbd_get_autoclicked_key(void) { int k = pending_key; pending_key = 0; return k; }
long kbd_is_key_pressed(long key) { (void)key; return 0; }
static int pending_dial;
long get_jogdial_direction(void) { int d = pending_dial; pending_dial = 0; return d; }

/* --- GUI modes --------------------------------------------------------- */
static gui_handler *gui_mode;
static int need_restore;
static void alt_draw(int enforce) { (void)enforce; }
gui_handler altGuiHandler = {GUI_MODE_ALT, alt_draw, 0, 0, 0, 0};
/* A redraw repaints the current mode without erasing the screen first. */
static int need_redraw;
void gui_set_need_restore(void) { need_restore = 1; }
void gui_set_need_redraw(void) { need_redraw = 1; }
void gui_cancel_need_restore(void) { need_restore = 0; }
gui_handler *gui_set_mode(gui_handler *mode)
{
    gui_handler *old = gui_mode;
    camera_info.state.gui_mode = mode->mode;
    camera_info.state.gui_mode_none = mode->mode == GUI_MODE_NONE;
    camera_info.state.gui_mode_alt = mode->mode == GUI_MODE_ALT;
    if (old != mode) {
        gui_mode = mode;
        need_restore = 1;
    }
    return old;
}

/* A short stand-in for CHDK's main menu, to try "CHDK menu" and MENU. */
static CMenuItem root_items[] = {
    MENU_ITEM(0x28, "Zrno menu", MENUITEM_SUBMENU, &film_lut_menu, 0),
    MENU_ITEM(0x20, "Enhanced Photo Operations", MENUITEM_SUBMENU, &film_lut_menu, 0),
    MENU_ITEM(0x21, "Video Parameters", MENUITEM_SUBMENU, &film_lut_menu, 0),
    MENU_ITEM(0x22, "RAW Parameters", MENUITEM_SUBMENU, &film_lut_menu, 0),
    MENU_ITEM(0x23, "Edge Overlay", MENUITEM_SUBMENU, &film_lut_menu, 0),
    MENU_ITEM(0x24, "Custom Curves", MENUITEM_SUBMENU, &film_lut_menu, 0),
    MENU_ITEM(0x25, "Histogram Parameters", MENUITEM_SUBMENU, &film_lut_menu, 0),
    MENU_ITEM(0x26, "OSD Parameters", MENUITEM_SUBMENU, &film_lut_menu, 0),
    MENU_ITEM(0x27, "Miscellaneous Stuff", MENUITEM_SUBMENU, &film_lut_menu, 0),
    MENU_ITEM(0x51, LANG_MENU_BACK, MENUITEM_UP, 0, 0),
    {0}};
CMenu root_menu = {0x20, (long)"Main Menu", root_items};

/* --- Live view: a 720 x 480 UYVYYY viewport ---------------------------- */
/* A/CHDK/FILM/SIMVIEW.RAW (1080 x 480 bytes, written by menu_sim.py from a
 * photo) when there is one, else a synthetic scene: a sky gradient, a sun
 * and a dark hill. */
static unsigned char viewport[1080 * 480];
static int viewport_ready;
void *vid_get_viewport_live_fb(void)
{
    if (!viewport_ready) {
        FILE *f = fopen("A/CHDK/FILM/SIMVIEW.RAW", "rb");
        viewport_ready = 1;
        if (!f || fread(viewport, 1, sizeof(viewport), f) != sizeof(viewport)) {
            static const unsigned char yo[4] = {1, 3, 4, 5};
            int x, y;
            for (y = 0; y < 480; y++)
                for (x = 0; x < 720; x++) {
                    int v = 230 - y * 160 / 480, dx = x - 520, dy = y - 120;
                    if (dx * dx + dy * dy < 60 * 60) v = 250;
                    if (y > 330 + (x % 200 < 100 ? x % 200 : 200 - x % 200) / 3) v = 40 + (x * 7 + y * 3) % 30;
                    viewport[y * 1080 + 6 * (x / 4) + yo[x % 4]] = (unsigned char)v;
                }
        }
        if (f) fclose(f);
    }
    return viewport;
}
int vid_get_viewport_image_offset(void) { return 0; }
/* The exposure the Viewfinder frame prints: 1/250, F4.0, ISO 200, +1/3. */
void msleep(long ms) { (void)ms; }                  /* single-threaded: nothing to wait for */
short shooting_get_tv96(void) { return 768; }
short shooting_get_av96(void) { return 384; }
short shooting_get_user_av96(void) { return 384; }
short shooting_get_iso_market(void) { return 200; }
short shooting_get_ev_correction1(void) { return 32; }
int vid_get_viewport_width_proper(void) { return 720; }
int vid_get_viewport_height_proper(void) { return 480; }
int vid_get_viewport_byte_width(void) { return 1080; }

/* --- Developing overlay (camera/film_ui.c) ----------------------------- */
void camera_set_led(int led, int state, int bright) { (void)led; (void)state; (void)bright; }
static int route = FILM_ROUTE_READY;
int film_route_reason(void) { return route; }
/* Shooting screen: CHDK's own (gui_osd.c), whose OSD pass draws the
 * indicator after CHDK's elements. */
extern gui_handler defaultGuiHandler;
static void none_draw(int enforce) { defaultGuiHandler.redraw(enforce); }
static gui_handler noneGuiHandler = {GUI_MODE_NONE, none_draw, 0, 0, 0, 0};

/* Each "develop" press moves one step through a shot: sensor backup, Canon's
 * develop, LUT with halation, soft focus, bloom + grain, Canon's JPEG, then
 * SAVED. */
static void develop(void)
{
    static const int steps[][2] = {{1, 0}, {1, 60}, {3, 0}, {4, 35}, {4, 80}, {FILM_UI_STAGE_SOFT, 50}, {FILM_UI_STAGE_BLOOM_GRAIN, 60}, {3, 0}};
    static int step = -1;
    if (gui_mode != &noneGuiHandler) gui_set_mode(&noneGuiHandler);
    if (!film_ui_busy() || step < 0) {
        film_ui_set_halation(2); film_ui_set_soft(1); film_ui_set_bloom(1); film_ui_set_grain(2); film_ui_set_fused(1);
        film_ui_raw_begin();
        film_ui_raw_filename("A/DCIM/117___01/IMG_0612.JPG");
        step = 0;
    } else if (++step >= (int)(sizeof(steps) / sizeof(steps[0]))) {
        film_ui_result(FILM_UI_SAVED, 0);
        step = -1;
        return;
    }
    film_ui_raw_stage(steps[step][0]);
    film_ui_progress(steps[step][1], 100);
}

/* "Re-develop photo" runs the redev.flt module on the camera. */
int module_run(char *name) { (void)name; return 0; }

/* --- Simulation -------------------------------------------------------- */
extern void draw_init(void);
extern void film_sim_lut_init(void);

/* S button: the patched ALT entry opens the film menu (MENU then goes on
 * to CHDK's main menu). */
/* Leaving <ALT> mode (the S button on the shooting side, or a half-press
 * in the Zrno menus): CHDK's GUI returns to the shooting screen. */
void exit_alt() { gui_set_mode(&noneGuiHandler); }

static void press_s(void)
{
    gui_user_menu_flag = 0;
    gui_menu_init(&film_lut_menu);
    gui_set_mode(&menuGuiHandler);
    gui_user_menu_flag = 1;
}

/* As CHDK's main loop and gui_redraw: tick, erase if asked, draw the mode,
 * then the developing overlay. */
static void redraw(void)
{
    film_ui_tick();
    if (need_restore) {
        memset(bitmap_buffer[0], 0, BUF_W * BUF_H);
        need_restore = need_redraw = 0;
        gui_mode->redraw(1);
    } else if (need_redraw) {
        need_redraw = 0;
        gui_mode->redraw(1);
    } else {
        gui_mode->redraw(0);
    }
    film_ui_draw();
}

void sim_init(int show_root)
{
    conf_load_defaults();                                   /* CHDK's, with Zrno's OSD defaults */
    sim_osd_init();
    camera_screen.width = SIM_W;
    camera_screen.height = SIM_H;
    camera_screen.size = SIM_W * SIM_H;
    camera_screen.physical_width = 720;
    camera_screen.buffer_width = BUF_W;
    camera_screen.buffer_height = BUF_H;
    camera_screen.buffer_size = BUF_W * BUF_H;
    camera_screen.disp_left = 0;
    camera_screen.disp_right = SIM_W - 1;
    camera_screen.disp_width = SIM_W;
    camera_screen.menu_border_width = 10;
    camera_screen.ts_menu_border = 0;
    camera_info.state.mode_rec = 1;
    /* CHDK's default menu colours (fg, bg). */
    conf.menu_color = (confColor){{IDX_COLOR_WHITE, 1}, {IDX_COLOR_GREY_DK, 1}};
    conf.menu_title_color = (confColor){{IDX_COLOR_BLACK, 1}, {IDX_COLOR_WHITE, 1}};
    conf.menu_cursor_color = (confColor){{IDX_COLOR_YELLOW, 1}, {IDX_COLOR_BLUE, 1}};
    conf.menu_symbol_color = (confColor){{IDX_COLOR_WHITE, 1}, {IDX_COLOR_GREY_DK, 1}};
    conf.menu_select_first_entry = 1;
    conf.menu_center = 1;
    conf.menu_symbol_enable = 0;
    draw_init();
    set_palette();
    rbf_load_from_file("", FONT_CP_WIN);                    /* CHDK's default: built-in 8x16 */
    rbf_load_symbol("A/CHDK/SYMBOLS/icon_10.rbf");
    film_sim_lut_init();
    gui_mode = &altGuiHandler;
    if (show_root) {
        gui_menu_init(&root_menu);
        gui_set_mode(&menuGuiHandler);
    } else {
        press_s();
    }
    redraw();
}

/* One button press, then a redraw as CHDK's GUI loop would. Returns 0 for
 * an unknown name. */
int sim_key(const char *name)
{
    static const struct { const char *name; int key; } keys[] = {
        {"up", KEY_UP}, {"down", KEY_DOWN}, {"left", KEY_LEFT}, {"right", KEY_RIGHT},
        {"set", KEY_SET}, {"half", KEY_SHOOT_HALF}, {"erase", KEY_ERASE}, {"display", KEY_DISPLAY}};
    unsigned i;
    if (!strcmp(name, "dial-left") || !strcmp(name, "dial-right")) {   /* the back dial (CHDK's FRONTDIAL codes on the G12) */
        pending_dial = name[5] == 'l' ? FRONTDIAL_LEFT : FRONTDIAL_RIGHT;
        if (gui_mode->kbd_process) gui_mode->kbd_process();
        redraw();
        return 1;
    }
    if (!strcmp(name, "menu")) {
        if (gui_mode->kbd_process_menu_btn) gui_mode->kbd_process_menu_btn();
        redraw();
        return 1;
    }
    if (!strcmp(name, "s")) {
        if (gui_mode == &altGuiHandler || gui_mode == &noneGuiHandler) press_s();
        else gui_set_mode(&altGuiHandler);
        redraw();
        return 1;
    }
    if (!strcmp(name, "gallery") || !strcmp(name, "record")) {   /* Canon's playback / shooting mode */
        camera_info.state.mode_play = name[0] == 'g';
        camera_info.state.mode_rec = !camera_info.state.mode_play;
        press_s();
        redraw();
        return 1;
    }
    if (!strcmp(name, "flip")) {                        /* swing the screen to face the lens, or back */
        camera_info.state.mode ^= SIM_FLIPPED;
        memset(bitmap_buffer[0], 0, BUF_W * BUF_H);      /* Canon redraws its screen on a swing */
        memset(bitmap_buffer[1], 0, BUF_W * BUF_H);
        need_restore = 1;                                /* CHDK sees its guard pixel gone */
        redraw();
        return 1;
    }
    if (!strcmp(name, "shoot")) {                       /* leave to the shooting screen */
        gui_set_mode(&noneGuiHandler);
        redraw();
        return 1;
    }
    if (!strcmp(name, "pause")) {                       /* cycle the route: ready, then each skip reason */
        route = route == FILM_ROUTE_READY ? FILM_ROUTE_MODE : route == FILM_ROUTE_SIZE ? FILM_ROUTE_READY : route + 1;
        need_restore = 1;
        redraw();
        return 1;
    }
    if (!strcmp(name, "develop") || !strcmp(name, "fail")) {
        if (name[0] == 'f') {
            if (gui_mode != &noneGuiHandler) gui_set_mode(&noneGuiHandler);
            film_ui_raw_begin();
            film_ui_raw_filename("A/DCIM/117___01/IMG_0612.JPG");
            film_ui_result(FILM_UI_FAILED, -4);
        } else {
            develop();
        }
        redraw();
        return 1;
    }
    if (!strcmp(name, "chdk-osd")) {                    /* CHDK r6355's own OSD defaults, or Zrno's again */
        static int stock;
        stock = !stock;
        if (stock) { SIM_OSD_STOCK; } else { SIM_OSD_ZRNO; }
        need_restore = 1;
        redraw();
        return 1;
    }
    if (!strcmp(name, "redraw")) {
        need_restore = 1;
        redraw();
        return 1;
    }
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if (!strcmp(name, keys[i].name)) {
            pending_key = keys[i].key;
            if (gui_mode->kbd_process) gui_mode->kbd_process();
            pending_key = 0;
            redraw();
            return 1;
        }
    return 0;
}

/* Describe where things stand (for the page's status line). */
const char *sim_state(void)
{
    if (gui_mode == &altGuiHandler) return "<ALT> mode (S opens the film menu)";
    if (gui_mode == &noneGuiHandler) return film_ui_busy() ? "Shooting screen: developing (press Develop to step)" : "Shooting screen";
    if (gui_mode == &filmPreviewGuiHandler) return "Zrno preview";
    if (get_curr_menu() == &film_settings_menu) return "Zrno settings";
    if (get_curr_menu() != &film_lut_menu) return "CHDK menu";
    return camera_info.state.mode_play ? "Zrno menu (gallery)" : "Zrno menu";
}
