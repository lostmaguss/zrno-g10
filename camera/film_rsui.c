/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Print on the camera (see film_rsui.h). Menu
 * callbacks only change the settings; SpyTask stores them and loads the
 * tables. The develop task prints a shot from a copy taken when it was armed. */
#include "camera_info.h"
#include "conf.h"
#include "clock.h"
#include "shooting.h"
#include "film_rsui.h"
#include "film_ui.h"

#define CFG_FILE "A/CHDK/FILM/PRINT.CFG"
#define CFG_TMP "A/CHDK/FILM/PRINT.TMP"
/* The file's name while the engine was called ZrnoPrint: read when
 * PRINT.CFG is missing, never written or removed (an older build on the
 * card still reads it). */
#define CFG_OLD "A/CHDK/FILM/ZRNOPRNT.CFG"
#define DATA_FILE "A/CHDK/FILM/PRINT.BIN"
/* Defaults: the picks of the prototype round (Riso, Pink + Blue, colour,
 * grain, noise 2, register 1.5 mm, heavy print flaws, off-white paper). */
static film_riso_settings set = {0, 0, 0, 2, 2, 3, 0, 0, 0};
static unsigned char variants[FILM_RISO_STYLES];      /* each style's palette, remembered */
/* Noise, Register and Print flaws are each style's own (Heavy flaws suit
 * the riso, not an engraving); these are the defaults, kept per style. */
static unsigned char style_rows[FILM_RISO_STYLES][3] = {
    {2, 2, 3},      /* Riso: the prototype picks */
    {0, 2, 0},      /* Screenprint: the key 1.5 mm out, clean ink */
    {0, 0, 0},      /* Engraving: clean lines */
    {1, 2, 1},      /* Woodcut */
    {3, 0, 2},      /* Photocopy: rough edges, streaks */
    {0, 1, 0},      /* Comic */
    {0, 0, 0}};     /* Text mode: none apply */
static void rows_save(void) { style_rows[set.style][0] = set.noise; style_rows[set.style][1] = set.reg; style_rows[set.style][2] = set.flaws; }
static void rows_load(void) { set.noise = style_rows[set.style][0]; set.reg = style_rows[set.style][1]; set.flaws = style_rows[set.style][2]; }
static int dirty, style_changed;
static unsigned dirty_tick;

/* ---- Settings file, 48 bytes: "G12ZP02" 0, inks, separation, screen,
 * noise, register, flaws (the current style's), paper, style, each style's
 * palette (7), each style's noise, register and flaws (21), 3 spare, a
 * byte sum. "G12ZP01" (16 bytes, before the styles) is read too.
 * Missing or invalid: defaults. */
static unsigned char sum(const unsigned char *p, int n) { unsigned s = 0xa5; while (n--) s += *p++; return (unsigned char)s; }
static void load_settings(void)
{
    unsigned char rec[48];
    int fd = open(CFG_FILE, O_RDONLY, 0), n, v2, i;
    if (fd < 0) fd = open(CFG_OLD, O_RDONLY, 0);
    if (fd < 0) return;
    n = read(fd, rec, 48);
    close(fd);
    v2 = n == 48 && !memcmp(rec, "G12ZP02", 8) && rec[47] == sum(rec, 47);
    if (!v2 && !(n == 16 && !memcmp(rec, "G12ZP01", 8) && rec[15] == sum(rec, 15))) return;
    if (rec[8] >= FILM_RISO_SETS || rec[9] >= FILM_RISO_SEPARATIONS || rec[10] >= FILM_RISO_SCREENS ||
        rec[11] >= FILM_RISO_NOISES || rec[12] >= FILM_RISO_REGISTERS || rec[13] >= FILM_RISO_FLAWS ||
        rec[14] >= FILM_RISO_PAPERS || (v2 && rec[15] >= FILM_RISO_STYLES))
        return;
    if (v2) for (i = 0; i < FILM_RISO_STYLES; i++)
        if ((i && rec[16 + i] >= film_riso_variants((unsigned)i)) || rec[23 + 3 * i] >= FILM_RISO_NOISES ||
            rec[24 + 3 * i] >= FILM_RISO_REGISTERS || rec[25 + 3 * i] >= FILM_RISO_FLAWS) return;
    set.inks = rec[8]; set.separation = rec[9]; set.screen = rec[10]; set.noise = rec[11];
    set.reg = rec[12]; set.flaws = rec[13]; set.paper = rec[14];
    if (v2) {
        set.style = rec[15];
        for (i = 1; i < FILM_RISO_STYLES; i++) variants[i] = rec[16 + i];
        for (i = 0; i < FILM_RISO_STYLES; i++) { style_rows[i][0] = rec[23 + 3 * i]; style_rows[i][1] = rec[24 + 3 * i]; style_rows[i][2] = rec[25 + 3 * i]; }
        set.variant = variants[set.style];
    }
    rows_save();                                       /* the current rows are the current style's */
}
static void store(void)
{
    unsigned char rec[48];
    int fd = open(CFG_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0777), w, i;
    if (fd < 0) return;
    rows_save();
    memset(rec, 0, 48);
    memcpy(rec, "G12ZP02", 8);
    rec[8] = set.inks; rec[9] = set.separation; rec[10] = set.screen; rec[11] = set.noise;
    rec[12] = set.reg; rec[13] = set.flaws; rec[14] = set.paper; rec[15] = set.style;
    for (i = 1; i < FILM_RISO_STYLES; i++) rec[16 + i] = variants[i];
    for (i = 0; i < FILM_RISO_STYLES; i++) { rec[23 + 3 * i] = style_rows[i][0]; rec[24 + 3 * i] = style_rows[i][1]; rec[25 + 3 * i] = style_rows[i][2]; }
    rec[47] = sum(rec, 47);
    w = write(fd, rec, 48);
    if (close(fd) || w != 48) { remove(CFG_TMP); return; }
    remove(CFG_FILE);
    rename(CFG_TMP, CFG_FILE);
}

/* ---- Tables and memory: PRINT.BIN (631 KB; RISO.BIN before the styles), the shot's workspace (for an L
 * frame, about 160 KB) and the live view's separation (24 KB). Loaded once
 * Print is chosen; kept after (switching back costs nothing). */
static unsigned char *data, *work, *live_sep;
static size_t work_bytes;
static int load_failed;
static void load_data(void)
{
    int fd, n;
    data = malloc(FILM_RISO_DATA_BYTES);
    work_bytes = film_riso_work_size(3648);
    work = malloc(work_bytes);
    live_sep = malloc(FILM_RISO_INKS * 8192);
    if (!data || !work || !live_sep) goto fail;
    fd = open(DATA_FILE, O_RDONLY, 0);
    if (fd < 0) goto fail;
    n = read(fd, data, FILM_RISO_DATA_BYTES);
    close(fd);
    if (n != (int)FILM_RISO_DATA_BYTES || film_riso_data_check(data, FILM_RISO_DATA_BYTES)) goto fail;
    return;
fail:
    if (data) free(data);
    if (work) free(work);
    if (live_sep) free(live_sep);
    data = work = live_sep = 0;
    load_failed = 1;
}
int film_rsui_ready(void) { return data != 0; }

void film_rsui_poll(int active)
{
    static int loaded;
    if (!loaded) { loaded = 1; load_settings(); }
    if (active && !data && !load_failed && !shooting_in_progress()) load_data();
    if (dirty && (unsigned)get_tick_count() - dirty_tick > 1000u && !shooting_in_progress() && !film_ui_busy()) {
        dirty = 0;
        store();
    }
}
static void changed(void) { dirty = 1; dirty_tick = get_tick_count(); }

/* ---- Menu rows ----------------------------------------------------------- */
static unsigned char step(unsigned char v, int change, int n) { return (unsigned char)((v + change % n + n) % n); }
#define ROW(fn, field, count, namefn) \
    const char *fn(int change, int arg) \
    { \
        (void)arg; \
        if (change) { set.field = step(set.field, change, count); changed(); } \
        return namefn(set.field); \
    }
ROW(film_rsui_menu_inks, inks, FILM_RISO_SETS, film_riso_inks_name)
ROW(film_rsui_menu_separation, separation, FILM_RISO_SEPARATIONS, film_riso_separation_name)
ROW(film_rsui_menu_screen, screen, FILM_RISO_SCREENS, film_riso_screen_name)
ROW(film_rsui_menu_register, reg, FILM_RISO_REGISTERS, film_riso_register_name)
ROW(film_rsui_menu_flaws, flaws, FILM_RISO_FLAWS, film_riso_flaws_name)
ROW(film_rsui_menu_paper, paper, FILM_RISO_PAPERS, film_riso_paper_name)
/* Style: the Riso rows, or another print style with its own palettes. The
 * rows change with it (film_menu.c asks film_rsui_style_changed). */
const char *film_rsui_menu_style(int change, int arg)
{
    (void)arg;
    if (change) {
        rows_save();
        set.style = step(set.style, change, FILM_RISO_STYLES);
        set.variant = variants[set.style];
        rows_load();
        style_changed = 1;
        changed();
    }
    return film_riso_style_name(set.style);
}
int film_rsui_style_changed(void) { int c = style_changed; style_changed = 0; return c; }
unsigned film_rsui_style(void) { return set.style; }
void film_rsui_get(film_riso_settings *s) { *s = set; }
void film_rsui_set(const film_riso_settings *s)
{
    film_riso_settings n = set;
    if (s->style < FILM_RISO_STYLES) n.style = s->style;
    if (s->inks < FILM_RISO_SETS) n.inks = s->inks;
    if (s->separation < FILM_RISO_SEPARATIONS) n.separation = s->separation;
    if (s->screen < FILM_RISO_SCREENS) n.screen = s->screen;
    if (s->noise < FILM_RISO_NOISES) n.noise = s->noise;
    if (s->reg < FILM_RISO_REGISTERS) n.reg = s->reg;
    if (s->flaws < FILM_RISO_FLAWS) n.flaws = s->flaws;
    if (s->paper < FILM_RISO_PAPERS) n.paper = s->paper;
    /* The Riso style has no palette of its own (the Inks row). */
    n.variant = !n.style ? 0 : s->variant < film_riso_variants(n.style) ? s->variant : variants[n.style];
    if (!memcmp(&n, &set, sizeof(n))) return;
    if (n.style != set.style) style_changed = 1;
    set = n;
    variants[set.style] = set.variant;
    rows_save();                                       /* the style keeps its noise, register and flaws */
    changed();
}
const char *film_rsui_menu_palette(int change, int arg)
{
    (void)arg;
    if (change && set.style) {
        set.variant = step(set.variant, change, (int)film_riso_variants(set.style));
        variants[set.style] = set.variant;
        changed();
    }
    return film_riso_variant_name(set.style, set.variant);
}
/* Noise steps stop at the ends, as ZrnoBoy's contrast does. */
const char *film_rsui_menu_noise(int change, int arg)
{
    (void)arg;
    if (change) { int c = set.noise + change; set.noise = (unsigned char)(c < 0 ? 0 : c >= FILM_RISO_NOISES ? FILM_RISO_NOISES - 1 : c); changed(); }
    return film_riso_noise_name(set.noise);
}

/* ---- The shot ------------------------------------------------------------ */
static film_riso_settings shot_set;
static unsigned shot_seed;
static volatile int shot_armed;
int film_rsui_arm(unsigned seed)
{
    shot_set = set;
    shot_seed = seed;
    shot_armed = data != 0;
    return shot_armed;
}
int film_rsui_render(unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                     void (*progress)(unsigned, unsigned))
{
    int rc;
    if (!shot_armed || !data) return -120;
    rc = film_riso_render(frame, width, height, stride, data, &shot_set, shot_seed, work, work_bytes, progress);
    shot_armed = 0;
    return rc ? -121 : 0;
}

/* ---- Live view ----------------------------------------------------------- */
static film_riso_settings live_set;
static int live_lo = 16, live_hi = 235;
void film_rsui_live_frame(const unsigned char *view, unsigned width, unsigned height, unsigned rowbytes,
                          int mirror, int fresh, unsigned char *yuv, unsigned char *canvas)
{
    if (!data) return;
    live_set = set;
    film_riso_live_sample(view, width, height, rowbytes, FILM_RISO_LIVE_W, FILM_RISO_LIVE_H, mirror, yuv);
    film_riso_live_levels(yuv, FILM_RISO_LIVE_W * FILM_RISO_LIVE_H, fresh, &live_lo, &live_hi);
    film_riso_live_render(yuv, FILM_RISO_LIVE_W, FILM_RISO_LIVE_H, data, &live_set, live_lo, live_hi, live_sep, canvas);
}
unsigned film_rsui_live_colour(unsigned m)
{
    int y, u, v;
    if (!data) return 0x03800000u;
    film_riso_colour(data, &live_set, m, &y, &u, &v);
    return 0x03000000u | (unsigned)y << 16 | (unsigned)(u & 255) << 8 | (unsigned)(v & 255);
}
const char *film_rsui_stage_text(void)
{
    static char text[40];
    unsigned n = data ? film_riso_ink_count(data, film_riso_set(&shot_set)) : 2;
    if (!shot_set.style) return n > 2 ? "Printing in 3 inks" : "Printing in 2 inks";
    if (shot_set.style == FILM_RISO_STYLE_TEXT) return "Typing it out";
    strcpy(text, "Printing: ");
    strcat(text, film_riso_style_name(shot_set.style));
    return text;
}
unsigned film_rsui_live_colours(void) { return data ? film_riso_live_colours(data, &live_set) : 4; }
