/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Grade on the camera (see film_grui.h). Menu
 * callbacks run in the keyboard task and only change the settings; SpyTask
 * builds the table from a copy (film_lut.c) and stores the file. */
#include "camera_info.h"
#include "conf.h"
#include "clock.h"
#include "shooting.h"
#include "film_grui.h"
#include "film_ui.h"

#define CFG_FILE "A/CHDK/FILM/GRADE.CFG"
#define CFG_TMP "A/CHDK/FILM/GRADE.TMP"

static film_grade_settings grades[FILM_GRUI_SLOTS];
static volatile int slot, colour;            /* the grade being edited; the HSL rows' colour */
static volatile unsigned serial = 1;
static int loaded, dirty, reset_armed;
static unsigned dirty_tick;
static unsigned *tables;                     /* two tables: Canon reads one, the next is built in the other */

static void changed(void) { serial++; dirty = 1; dirty_tick = get_tick_count(); }

/* ---- Settings file, 171 bytes: "G12GR02" 0, slot, HSL colour, the four
 * grades (40 signed bytes each, film_grade.h's field order), a byte sum.
 * "G12GR01" (160 bytes, 37 a grade: before blacks, whites and black and
 * white) is read too, those three then 0. Missing or invalid: four neutral
 * grades. */
#define OLD_FIELDS 37
#define OLD_REC 160                  /* 10 + 4 x 37, a zero byte, the sum */
#define REC (10 + FILM_GRUI_SLOTS * FILM_GRADE_FIELDS + 1)
static unsigned char sum(const unsigned char *p, int n) { unsigned s = 0x3c; while (n--) s += *p++; return (unsigned char)s; }
static void load(void)
{
    unsigned char rec[REC + 1];
    film_grade_settings g[FILM_GRUI_SLOTS];
    int fd, n, i, fields;
    for (i = 0; i < FILM_GRUI_SLOTS; i++) film_grade_neutral(&grades[i]);
    fd = open(CFG_FILE, O_RDONLY, 0);
    if (fd < 0) return;
    n = read(fd, rec, REC + 1);
    close(fd);
    if (n == REC && !memcmp(rec, "G12GR02", 8)) fields = FILM_GRADE_FIELDS;
    else if (n == OLD_REC && !memcmp(rec, "G12GR01", 8)) fields = OLD_FIELDS;
    else return;
    if (rec[n - 1] != sum(rec, n - 1) || rec[8] >= FILM_GRUI_SLOTS || rec[9] >= FILM_GRADE_HUES) return;
    for (i = 0; i < FILM_GRUI_SLOTS; i++) {
        film_grade_neutral(&g[i]);
        memcpy(g[i].v, rec + 10 + i * fields, fields);
        if (!film_grade_valid(&g[i])) return;
    }
    memcpy(grades, g, sizeof(grades));
    slot = rec[8]; colour = rec[9];
}
static void store(void)
{
    unsigned char rec[REC];
    int fd = open(CFG_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0777), w, i;
    if (fd < 0) return;
    memset(rec, 0, REC);
    memcpy(rec, "G12GR02", 8);
    rec[8] = (unsigned char)slot; rec[9] = (unsigned char)colour;
    for (i = 0; i < FILM_GRUI_SLOTS; i++) memcpy(rec + 10 + i * FILM_GRADE_FIELDS, grades[i].v, FILM_GRADE_FIELDS);
    rec[REC - 1] = sum(rec, REC - 1);
    w = write(fd, rec, REC);
    if (close(fd) || w != REC) { remove(CFG_TMP); return; }
    remove(CFG_FILE);                        /* none yet is fine */
    rename(CFG_TMP, CFG_FILE);
}
static void ensure_loaded(void) { if (!loaded) { loaded = 1; load(); } }

void film_grui_poll(void)
{
    ensure_loaded();
    if (dirty && (unsigned)get_tick_count() - dirty_tick > 1000u && !shooting_in_progress() && !film_ui_busy()) {
        dirty = 0;
        store();
    }
}

/* ---- Menu rows ---------------------------------------------------------- */
/* Values are drawn straight after each call; a few buffers in turn let two
 * rows' values stand at once. */
static const char *number(int v, int sign)
{
    static char text[6][8];
    static unsigned next;
    char *t = text[next++ % 6];
    if (!v) return "0";
    sprintf(t, sign && v > 0 ? "+%d" : "%d", v);
    return t;
}
static const char *slot_names[FILM_GRUI_SLOTS] = {"Grade 1", "Grade 2", "Grade 3", "Grade 4"};
const char *film_grui_slot_name(void) { return slot_names[slot]; }
const char *film_grui_menu_slot(int change, int arg)
{
    (void)arg;
    ensure_loaded();
    if (change) { slot = (slot + change % FILM_GRUI_SLOTS + FILM_GRUI_SLOTS) % FILM_GRUI_SLOTS; changed(); }
    return slot_names[slot];
}
void film_grui_get(int *s, film_grade_settings *g)
{
    ensure_loaded();
    *s = slot;
    *g = grades[slot];
}
void film_grui_set(int s, const film_grade_settings *g)
{
    ensure_loaded();
    if (s < 0 || s >= FILM_GRUI_SLOTS || !film_grade_valid(g)) return;
    if (s == slot && !memcmp(&grades[s], g, sizeof(*g))) return;
    slot = s;
    grades[s] = *g;
    reset_armed = 0;
    changed();
}
/* Numbers stop at the ends; the split-tone colours and the black-and-white
 * filters wrap (Off, Red ...). */
static void step(int field, int change)
{
    film_grade_settings *g = &grades[slot];
    int lo = film_grade_min(field), hi = film_grade_max(field), v = g->v[field] + change;
    if (field == FILM_GRADE_SHADOW_HUE || field == FILM_GRADE_HIGHLIGHT_HUE || field == FILM_GRADE_MONO) v = lo + ((v - lo) % (hi - lo + 1) + (hi - lo + 1)) % (hi - lo + 1);
    else v = v < lo ? lo : v > hi ? hi : v;
    if (v != g->v[field]) { g->v[field] = (signed char)v; changed(); }
}
const char *film_grui_menu_field(int change, int arg)
{
    int v;
    ensure_loaded();
    if (arg < 0 || (arg >= FILM_GRADE_HSL_HUE && arg < FILM_GRADE_BLACKS) || arg >= FILM_GRADE_FIELDS) return "?";
    if (change) step(arg, change);
    v = grades[slot].v[arg];
    switch (arg) {
    case FILM_GRADE_FADE: return v ? number(v, 0) : "Off";
    case FILM_GRADE_SHADOW_HUE: case FILM_GRADE_HIGHLIGHT_HUE: return v ? film_grade_hue_name((unsigned)v - 1) : "Off";
    case FILM_GRADE_SHADOW_AMOUNT: case FILM_GRADE_HIGHLIGHT_AMOUNT: return number(v, 0);
    case FILM_GRADE_MONO: return film_grade_mono_name((unsigned)v);
    }
    return number(v, 1);
}
const char *film_grui_menu_colour(int change, int arg)
{
    (void)arg;
    ensure_loaded();
    if (change) { colour = (colour + change % FILM_GRADE_HUES + FILM_GRADE_HUES) % FILM_GRADE_HUES; dirty = 1; dirty_tick = get_tick_count(); }
    return film_grade_hue_name((unsigned)colour);
}
const char *film_grui_menu_hsl(int change, int arg)
{
    int field, c = colour;
    ensure_loaded();
    if (arg < 0 || arg > 2) return "?";
    field = (arg == 0 ? FILM_GRADE_HSL_HUE : arg == 1 ? FILM_GRADE_HSL_SAT : FILM_GRADE_HSL_LIGHT) + c;
    if (change) step(field, change);
    return number(grades[slot].v[field], 1);
}
const char *film_grui_hsl_label(int arg)
{
    static char text[2][24];
    static unsigned next;
    char *t = text[next++ & 1];
    sprintf(t, "%s %s", film_grade_hue_name((unsigned)colour), arg == 0 ? "hue" : arg == 1 ? "saturation" : "lightness");
    return t;
}
void film_grui_reset(int arg)
{
    (void)arg;
    ensure_loaded();
    if (film_grade_is_neutral(&grades[slot])) { reset_armed = 0; return; }
    if (!reset_armed) { reset_armed = 1; return; }
    reset_armed = 0;
    film_grade_neutral(&grades[slot]);
    changed();
}
const char *film_grui_reset_label(int *enabled)
{
    ensure_loaded();
    *enabled = !film_grade_is_neutral(&grades[slot]);
    if (reset_armed && *enabled) return "SET again to reset";
    reset_armed = 0;
    return slot == 0 ? "Reset Grade 1" : slot == 1 ? "Reset Grade 2" : slot == 2 ? "Reset Grade 3" : "Reset Grade 4";
}
void film_grui_reset_disarm(void) { reset_armed = 0; }

/* ---- The table -------------------------------------------------------- */
unsigned film_grui_serial(void) { return serial; }
const unsigned *film_grui_build(const unsigned *avoid)
{
    film_grade_settings g;
    unsigned *t;
    ensure_loaded();
    if (!tables) tables = malloc(2 * 1000 * sizeof(unsigned));
    if (!tables) return 0;
    t = avoid == tables ? tables + 1000 : tables;
    g = grades[slot];                        /* a copy: the keyboard task may be editing */
    if (!film_grade_valid(&g)) film_grade_neutral(&g);
    film_grade_table(&g, t);
    return t;
}
