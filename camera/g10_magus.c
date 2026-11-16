// SPDX-License-Identifier: GPL-2.0-or-later
/* Magus: a port of Zrno (github.com/suzvuk/zrno, by suzvuk) to the Canon
 * PowerShot G10 1.03B (docs/PORT-G10.md): film looks in the DIGIC colour
 * table, and halation, Bloom and grain on the developed L frame.
 *
 * Tasks:
 *   - SpyTask (g10_magus_poll, every CHDK loop): loads settings, allocates the
 *     workspaces, and installs the selected look into Canon's "My Colors
 *     Off" table (in shooting mode with My Colors Off), then reloads it.
 *   - the menu (CHDK's GUI): changes settings, saves them.
 *   - Canon's develop task, through its relocated copy (film_reloc.c): the
 *     hooks below. After the RAW -> YC message has developed an L frame the
 *     effects run on it, before Canon encodes the JPEG with the look's table.
 * The develop task reads settings once per shot, into locals.
 */
#include "film_reloc.h"
#include "film_stage.h"
#include "film_colour.h"
#include "film_bloom.h"
#include "film_grain.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"
#include "camera_info.h"
#include "properties.h"
#include "gui.h"
#include "gui_menu.h"
#include "gui_lang.h"

extern long get_tick_count(void);
#define G10_PROP_MY_COLORS 184          /* propset 2; core library code cannot include propset2.h */
extern void msleep(long);

/* ---- Develop log (G10OBS.LUA reads it by its magic) ---------------------- */
#define G10_MAGIC   0x3031474du          /* "MG10" */
#define G10_RECORDS 96
#define G10_WORDS   16
static const unsigned watch[G10_WORDS] = {
    0x210ac, 0x210b0, 0x210b4, 0x210b8, 0x210bc, 0x210c0, 0x210c4, 0x210d8,
    0x210dc, 0x210e0, 0x21074, 0x21078, 0x6ee8, 0x6eec, 0x40428, 0x40428 + 4 * 364,
};
typedef struct { unsigned tick, phase, type, seq; unsigned w[G10_WORDS]; } g10_record;
struct {
    unsigned magic, version, count, reloc_status, records, words;
    unsigned watch[G10_WORDS];
    g10_record rec[G10_RECORDS];
} magus_g10_log = { G10_MAGIC, 2, 0, 0, G10_RECORDS, G10_WORDS, {0}, {{0}} };

static g10_record *add(unsigned phase, const unsigned *msg)
{
    unsigned n = magus_g10_log.count, i;
    g10_record *r = &magus_g10_log.rec[n % G10_RECORDS];
    r->tick = (unsigned)get_tick_count();
    r->phase = phase;
    r->type = msg ? msg[0] : 0;
    r->seq = n;
    for (i = 0; i < G10_WORDS; i++) r->w[i] = msg ? msg[i] : *(volatile unsigned *)watch[i];
    magus_g10_log.reloc_status = (unsigned)film_reloc_status;
    magus_g10_log.count = n + 1;
    return r;
}

/* ---- Settings ------------------------------------------------------------ */
#define MAX_LOOKS 16
#define DIR "A/CHDK/MAGUS/"
#define CFG "A/CHDK/MAGUS/MAGUS.CFG"
static volatile int set_on = 1, set_look, set_halo, set_amber, set_bloom, set_grain;
static char look_stem[MAX_LOOKS][9], look_name[MAX_LOOKS + 1][21];
static int look_count;

static void save_settings(void)
{
    char s[64];
    int fd = open(CFG, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) return;
    sprintf(s, "magus-g10 1 %d %d %d %d %d %d\n", set_on, set_look, set_halo, set_amber, set_bloom, set_grain);
    write(fd, s, strlen(s));
    close(fd);
}

static int read_file(const char *path, void *buf, int size)
{
    int fd = open(path, O_RDONLY, 0), n;
    if (fd < 0) return -1;
    n = read(fd, buf, size);
    close(fd);
    return n;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void load_settings(void)
{
    static char text[64 * MAX_LOOKS + 64];
    char *p, *line;
    int n = read_file(DIR "LOOKS.TXT", text, sizeof text - 1), v[6];
    strcpy(look_name[0], "Canon");
    look_count = 0;
    if (n > 0) {
        text[n] = 0;
        for (line = text; line && *line && look_count < MAX_LOOKS; line = p) {
            char *bar = strchr(line, '|');
            p = strchr(line, '\n');
            if (p) *p++ = 0;
            if (!bar || bar - line > 8 || bar == line) continue;
            *bar = 0;
            strcpy(look_stem[look_count], line);
            strncpy(look_name[look_count + 1], bar + 1, 20);
            look_name[look_count + 1][20] = 0;
            if ((bar = strchr(look_name[look_count + 1], '\r'))) *bar = 0;
            look_count++;
        }
    }
    n = read_file(CFG, text, 63);
    if (n > 12 && !memcmp(text, "magus-g10 1 ", 12)) {
        int k;
        text[n] = 0;
        p = text + 12;
        for (k = 0; k < 6; k++) {
            char *end;
            v[k] = (int)strtol(p, &end, 10);
            if (end == p) break;
            p = end;
        }
        if (k == 6) {
            set_on = v[0] != 0; set_look = clamp(v[1], 0, look_count);
            set_halo = clamp(v[2], 0, 3); set_amber = v[3] != 0;
            set_bloom = clamp(v[4], 0, 3); set_grain = clamp(v[5], 0, 3);
        }
    }
}

/* ---- Looks in Canon's table ---------------------------------------------- */
#define NODES       729
#define TABLE_PTR   (*(volatile unsigned *)0x75c8)    /* My Colors tables, table 0 = Off */
#define ACTIVE      ((volatile unsigned *)0x40428)     /* the table Canon copied for this shot */
#define RELOAD      ((void (*)(int))0xff83da04)       /* My Colors (re)load, mode 0 */
static unsigned canon[NODES], look[NODES];             /* Canon's Off table; the table installed */
static int have_canon, loaded = -1;                    /* loaded: look index installed, -1 none */
static film_halo_tint tint;
static unsigned char tint_highlight[256];
static int look_mono, look_error;
static const char *look_status = "starting";

static int rom_ok(void)
{
    return film_reloc_status == 1
        && *(volatile unsigned *)0xff83da04 == 0xe92d4038u
        && *(volatile unsigned *)0xff8d2c38 == 0xe92d4010u
        && *(volatile unsigned *)0xff8fa698 == 0xe92d4070u;
}

static int is_identity(const volatile unsigned *t)
{
    int k;
    for (k = 0; k < 9; k++) if (t[91 * k] != (unsigned)(80 * k) << 20) return 0;
    return (t[1] & 1023) != 0;
}

static unsigned u16le(const unsigned char *p) { return p[0] | p[1] << 8; }

/* The halation tint and highlight weights of look i (1..), from its .HWT
 * and .HWA (Zrno's files, fitted to the look); Canon: Zrno's default. */
static void load_tint(int i, int amber)
{
    static unsigned char hwt[4272];
    unsigned char hwa[16];
    char path[40];
    tint.red = 256; tint.green = amber ? 128 : 51; tint.blue = 0; tint.highlight = 0;
    if (i <= 0) return;
    sprintf(path, DIR "%s.HWT", look_stem[i - 1]);
    if (read_file(path, hwt, sizeof hwt) != (int)sizeof hwt || memcmp(hwt + 4000, "HWTINT1", 8)) return;
    memcpy(tint_highlight, hwt + 4016, 256);
    tint.highlight = tint_highlight;
    tint.red = u16le(hwt + 4008); tint.green = u16le(hwt + 4010); tint.blue = u16le(hwt + 4012);
    if (amber) {
        sprintf(path, DIR "%s.HWA", look_stem[i - 1]);
        if (read_file(path, hwa, 16) == 16 && !memcmp(hwa, "HWAMBR1", 8) &&
            u16le(hwa + 8) <= 1024 && u16le(hwa + 10) <= 1024 && u16le(hwa + 12) <= 1024) {
            tint.red = u16le(hwa + 8); tint.green = u16le(hwa + 10); tint.blue = u16le(hwa + 12);
        }
    }
}

/* SpyTask, shooting mode: installs the wanted look into table 0. */
static void install_look(void)
{
    int want = set_on ? set_look : 0, i, mono = 1;
    unsigned base = TABLE_PTR;
    char path[40];
    static int tint_for = -2;
    if (!rom_ok()) { look_status = "unknown firmware"; return; }
    if (base < 0x100000 || base > 0x2000000 || (base & 3)) { look_status = "no colour table"; return; }
    if (!have_canon) {
        if (!is_identity((volatile unsigned *)base)) { look_status = "table changed: restart"; return; }
        for (i = 0; i < NODES; i++) canon[i] = ((volatile unsigned *)base)[i];
        have_canon = 1;
    }
    if (want == loaded && tint_for == (want << 1 | set_amber)) return;
    if (want != loaded) {
        if (want == 0) memcpy(look, canon, sizeof look);
        else {
            sprintf(path, DIR "%s.G10", look_stem[want - 1]);
            if (read_file(path, look, sizeof look) != (int)sizeof look) {
                look_status = "look file missing"; look_error = 1; set_look = 0; return;
            }
        }
        for (i = 0; i < NODES; i++) if (look[i] & 0xfffffu) { mono = 0; break; }
        look_mono = mono;
        for (i = 0; i < NODES; i++) ((volatile unsigned *)base)[i] = look[i];
        RELOAD(0);
        loaded = want;
    }
    load_tint(want, set_amber);
    tint_for = want << 1 | set_amber;
    look_error = 0;
    look_status = "ready";
}

/* ---- Workspaces ---------------------------------------------------------- */
#define G10_W     4416u
#define G10_H     3312u
#define ROW_BYTES 8832u
static void *halo_work, *bloom_work;
static unsigned char *row_scratch, *fade_table;
static size_t halo_size, bloom_size;
static int alloc_done;

/* CHDK shares Canon's heap on the G10 (not in EXMEM): leave Canon this much. */
#define CANON_RESERVE (512 * 1024)
extern int core_get_free_memory(void);
static int free_at_start;

static void allocate(void)
{
    size_t need;
    alloc_done = 1;
    halo_size = film_halo_work_size(G10_W);
    bloom_size = film_bloom_work_size(G10_W);
    need = halo_size + bloom_size + ROW_BYTES + 256;
    free_at_start = core_get_free_memory();
    if (free_at_start < (int)need + CANON_RESERVE) return;     /* effects stay off: "no memory" */
    halo_work = malloc(halo_size + 4);
    bloom_work = malloc(bloom_size + 4);
    row_scratch = malloc(ROW_BYTES + 64);
    if (core_get_free_memory() > (int)film_bloom_fade_table_size() + CANON_RESERVE)
        fade_table = malloc(film_bloom_fade_table_size());     /* optional: faster Bloom */
    if (halo_work) halo_work = (void *)(((unsigned)halo_work + 3) & ~3u);
    if (bloom_work) bloom_work = (void *)(((unsigned)bloom_work + 3) & ~3u);
    if (fade_table) film_bloom_fade_table(fade_table);
}

/* ---- SpyTask ------------------------------------------------------------- */
void g10_magus_poll(void)
{
    static int started;
    if (!started) {
        started = 1;
        mkdir_if_not_exist("A/CHDK/MAGUS");
        load_settings();
        allocate();
    }
    if (camera_info.state.mode_rec && !camera_info.state.mode_video &&
        shooting_get_prop(G10_PROP_MY_COLORS) == 0)
        install_look();
}

/* ---- Develop task hooks -------------------------------------------------- */
static unsigned last_type, last_yield, sleep_ms, sleeps;
static unsigned last_ms;
static int last_rc = 1;                 /* 1: no shot yet */

void film_develop_trace(const unsigned *msg)
{
    if (!magus_g10_log.watch[0]) {
        unsigned i;
        for (i = 0; i < G10_WORDS; i++) magus_g10_log.watch[i] = watch[i];
    }
    last_type = msg[0];
    add(0, msg);
}

static unsigned l_frame(unsigned src, unsigned dst)
{
    if (src == 0x43164d80u && dst == 0x42902100u) return dst;
    if (src == 0x413245a0u && dst == 0x41516720u) return dst;
    return 0;
}

static void __attribute__((target("arm"), noinline)) dcache_clean_flush_all(void)
{
    unsigned config, size, seg, i;
    asm volatile("mrc p15, 0, %0, c0, c0, 1" : "=r"(config));
    size = (config >> 18) & 0xfu;
    if (!size) return;
    size = 0x200u << size;
    for (seg = 0; seg < 4; seg++)
        for (i = 0; i < size / 4; i += 32)
            asm volatile("mcr p15, 0, %0, c7, c14, 2" :: "r"((seg << 30) | i) : "memory");
    asm volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory");
}

static void progress(unsigned row, unsigned rows)
{
    (void)row; (void)rows;
    if ((unsigned)get_tick_count() - last_yield >= 250) {
        unsigned t = (unsigned)get_tick_count();
        msleep(10);
        last_yield = (unsigned)get_tick_count();
        sleep_ms += last_yield - t;
        sleeps++;
    }
}

/* Canon used this look for the shot: its copy of table 0 is ours. */
static int look_used(void)
{
    int k;
    if (loaded < 0) return 0;
    for (k = 0; k < NODES; k += 13) if (ACTIVE[k] != look[k]) return 0;
    return ACTIVE[NODES - 1] == look[NODES - 1];
}

/* Return codes (shown in the menu): 0 done, -2 look not used by Canon
 * (My Colors not Off), -3 no memory, other: the kernel's. */
static void effects(void)
{
    unsigned frame = l_frame(*(volatile unsigned *)0x210b4, *(volatile unsigned *)0x210d8);
    int halo = set_halo, bloom = set_bloom, grain_level = set_grain, rc = 0;
    unsigned t0, t1 = 0, t2 = 0, seed;
    unsigned char *image;
    size_t bytes = (size_t)ROW_BYTES * G10_H;
    g10_record *r;
    if (!set_on || !frame || (!halo && !bloom && !grain_level)) return;
    t0 = last_yield = (unsigned)get_tick_count();
    sleep_ms = sleeps = 0;
    seed = t0 * 0x9e3779b9u ^ magus_g10_log.count;
    if (!look_used()) rc = -2;
    else if ((halo && !halo_work) || (bloom && !bloom_work) || !row_scratch) rc = -3;
    if (!rc) {
        film_halo_tint t = tint;
        dcache_clean_flush_all();                     /* no stale line of the frame */
        image = (unsigned char *)(frame & ~0x40000000u);
        film_colour_rows_in_place(1);
        film_halo_coarse(1);                          /* 4x4 mask cells: same glow, ~1/3 less time */
        film_bloom_fast(1);                           /* within 2 levels of Bloom, 12-14 % less time */
        t1 = (unsigned)get_tick_count();
        if (halo)
            rc = film_uyvy_graded_retro(image, bytes, G10_W, G10_H, ROW_BYTES, look_mono, (unsigned)halo, &t,
                                        halo_work, halo_size, row_scratch, ROW_BYTES, progress,
                                        0, 0, seed ^ 0xa3b195f1u, 0, 0, 0, 0);
        t2 = (unsigned)get_tick_count();
        if (!rc && bloom && grain_level) {             /* one pass, as the G12's fused Bloom + grain */
            static film_grain_rows g;                 /* 540 bytes: not on the develop task's 4 KB stack */
            static unsigned char grain_work[(G10_W / 2 + 1) * 2];
            rc = film_grain_begin_rows(&g, G10_W, G10_H, (unsigned)grain_level, seed, grain_work, sizeof grain_work);
            if (!rc) {
                film_bloom_row_pass pass = { row_scratch, ROW_BYTES, film_grain_finish_row, &g };
                rc = film_uyvy_bloom_rows(image, bytes, G10_W, G10_H, ROW_BYTES, (unsigned)bloom,
                                          bloom_work, bloom_size, &pass, progress);
            }
        } else {
            if (!rc && bloom)
                rc = film_uyvy_bloom(image, bytes, G10_W, G10_H, ROW_BYTES, (unsigned)bloom, bloom_work, bloom_size, progress);
            if (!rc && grain_level) {
                static unsigned char grain_work[(G10_W / 2 + 1) * 2];
                rc = film_uyvy_grain(image, bytes, G10_W, G10_H, ROW_BYTES, (unsigned)grain_level, seed,
                                     grain_work, sizeof grain_work, progress);
            }
        }
        film_colour_rows_in_place(0);
        dcache_clean_flush_all();                     /* every write in RAM before the JPEG engine */
    }
    last_ms = (unsigned)get_tick_count() - t0;
    last_rc = rc;
    r = add(6, 0);
    r->type = ((unsigned)(rc & 0xff) << 24) | (last_ms & 0xffffffu);
    /* phase 6: what the time went to (G10OBS.LUA prints these words) */
    memset(r->w, 0, sizeof r->w);
    r->w[0] = t1 ? t1 - t0 : 0;                       /* cache clean before */
    r->w[1] = t2 ? t2 - t1 : 0;                       /* halation */
    r->w[2] = t2 ? (unsigned)get_tick_count() - t2 : 0;   /* Bloom and grain, cache clean after */
    r->w[3] = sleep_ms; r->w[4] = sleeps;
    r->w[5] = (unsigned)free_at_start;
    r->w[6] = (unsigned)(halo | bloom << 4 | grain_level << 8 | set_look << 12 | look_mono << 20);
    r->w[7] = frame; r->w[8] = (unsigned)halo_work; r->w[9] = (unsigned)bloom_work; r->w[10] = (unsigned)fade_table;
}

void film_stage_observe(unsigned phase)
{
    add(phase, 0);
    if (phase == 2 && last_type == 1) effects();
}

/* ---- Menu ---------------------------------------------------------------- */
static const char *levels[4] = { "Off", "Low", "Mid", "High" };
static const char *step(volatile int *v, int change, int n)
{
    if (change) { *v = (*v + change % n + n) % n; save_settings(); }
    return 0;
}
static const char *m_on(int c, int a) { (void)a; step(&set_on, c, 2); return set_on ? "On" : "Off"; }
static const char *m_look(int c, int a)
{
    (void)a;
    step(&set_look, c, look_count + 1);
    return look_name[set_look];
}
static const char *m_halo(int c, int a) { (void)a; step(&set_halo, c, 4); return levels[set_halo]; }
static const char *m_amber(int c, int a) { (void)a; step(&set_amber, c, 2); return set_amber ? "Amber" : "Red"; }
static const char *m_bloom(int c, int a) { (void)a; step(&set_bloom, c, 4); return levels[set_bloom]; }
static const char *m_grain(int c, int a) { (void)a; step(&set_grain, c, 4); return levels[set_grain]; }
static const char *m_status(int c, int a)
{
    static char s[40];
    (void)c; (void)a;
    if (film_reloc_status != 1) return "develop hook off";
    if (look_error || loaded < 0) return look_status;
    if (last_rc == 1) return look_status;
    if (last_rc == -2) return "My Colors not Off";
    if (last_rc == -3) { sprintf(s, "no memory (%d KB)", free_at_start / 1024); return s; }
    if (last_rc) { sprintf(s, "error %d", last_rc); return s; }
    sprintf(s, "%u.%u s", last_ms / 1000, last_ms / 100 % 10);
    return s;
}

static CMenuItem magus_items[] = {
    MENU_ITEM(0x5c, "Processing",      MENUITEM_ENUM, m_on,     0),
    MENU_ITEM(0x5c, "Film look",       MENUITEM_ENUM, m_look,   0),
    MENU_ITEM(0x5c, "Halation",        MENUITEM_ENUM, m_halo,   0),
    MENU_ITEM(0x5c, "Halation colour", MENUITEM_ENUM, m_amber,  0),
    MENU_ITEM(0x5c, "Bloom",           MENUITEM_ENUM, m_bloom,  0),
    MENU_ITEM(0x5c, "Grain",           MENUITEM_ENUM, m_grain,  0),
    MENU_ITEM(0x5c, "Last shot",       MENUITEM_ENUM, m_status, 0),
    MENU_ITEM(0x51, LANG_MENU_BACK,    MENUITEM_UP,   0,        0),
    {0}
};
CMenu magus_g10_menu = { 0x5c, (int)"Magus", magus_items };
