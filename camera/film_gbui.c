/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. ZrnoBoy on the camera (see film_gbui.h). Menu callbacks
 * only change the settings; SpyTask stores them and allocates. The develop
 * task renders a shot from a copy taken when the shot was armed. */
#include "camera_info.h"
#include "conf.h"
#include "clock.h"
#include "shooting.h"
#include "task.h"
#include "viewport.h"
#include "gui.h"
#include "gui_draw.h"
#include "film_gbui.h"
#include "film_rsui.h"
#include "film_grui.h"
#include "film_lut.h"
#include "film_menu.h"
#include "film_queue.h"
#include "film_ui.h"

#define GBOY_FILE "A/CHDK/FILM/ZRNOBOY.CFG"
#define GBOY_TMP "A/CHDK/FILM/ZRNOBOY.TMP"
/* Defaults: the console's look. */
static int engine;
static film_gboy_settings set = {0, 1, 7, 3, 2, 1, 0};
static int dirty, changed_engine;
static int live_off;                        /* Settings: Live preview Off */
/* CHDK/FILM/GBCAL.TXT: a ZrnoBoy shot records the calibration chart
 * instead of the photo (checked by SpyTask). */
#define GBOY_CAL_FILE "A/CHDK/FILM/GBCAL.TXT"
static volatile int calibrate;
static unsigned cal_tick;
static unsigned dirty_tick;

#define ENGINES FILM_ENGINES
int film_gboy_engine(void) { return engine == 1; }
int film_riso_engine(void) { return engine == 2; }
int film_grade_engine(void) { return engine == 3; }
int film_engine_alt(void) { return engine != 0; }
/* ZrnoBoy and Print draw their own picture (memory, live task); Grade
 * is only a colour table. */
static int picture_engine(void) { return engine == 1 || engine == 2; }
static const char *const engine_names[ENGINES] = {"Film", "ZrnoBoy", "Print", "Grade"};
const char *film_gbui_engine_name(void) { return engine_names[engine]; }
int film_gbui_engine_changed(void) { int c = changed_engine; changed_engine = 0; return c; }

/* ---- Settings file: "G12GB01" 0, engine, palette, frame, contrast,
 * brightness, edges, dither (bit 0), resolution (bit 1) and Live preview
 * Off (bit 2), a byte sum.
 * Missing or invalid: defaults. */
static unsigned char sum(const unsigned char *p, int n) { unsigned s = 0x5a; while (n--) s += *p++; return (unsigned char)s; }
static void encode(unsigned char *rec)
{
    memset(rec, 0, 16);
    memcpy(rec, "G12GB01", 8);
    rec[8] = (unsigned char)engine; rec[9] = set.palette; rec[10] = set.frame; rec[11] = set.contrast;
    rec[12] = set.brightness; rec[13] = set.edges; rec[14] = (unsigned char)(set.dither | set.resolution << 1 | live_off << 2);
    rec[15] = sum(rec, 15);
}
void film_gbui_init(void)
{
    unsigned char rec[16];
    int fd = open(GBOY_FILE, O_RDONLY, 0), n;
    if (fd < 0) return;
    n = read(fd, rec, 16);
    close(fd);
    if (n != 16 || memcmp(rec, "G12GB01", 8) || rec[15] != sum(rec, 15) || rec[8] >= ENGINES || rec[9] >= FILM_GBOY_PALETTES ||
        rec[10] >= FILM_GBOY_FRAMES || rec[11] >= FILM_GBOY_CONTRASTS || rec[12] >= FILM_GBOY_BRIGHTNESS ||
        rec[13] >= FILM_GBOY_EDGES || rec[14] > 7)
        return;
    engine = rec[8];
    set.palette = rec[9]; set.frame = rec[10]; set.contrast = rec[11];
    set.brightness = rec[12]; set.edges = rec[13]; set.dither = rec[14] & 1; set.resolution = (rec[14] >> 1) & 1; live_off = rec[14] >> 2;
}
static void store(void)
{
    unsigned char rec[16];
    int fd = open(GBOY_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0777), w;
    if (fd < 0) return;
    encode(rec);
    w = write(fd, rec, 16);
    if (close(fd) || w != 16) { remove(GBOY_TMP); return; }
    remove(GBOY_FILE);                      /* none yet is fine */
    rename(GBOY_TMP, GBOY_FILE);
}

/* ---- Memory: the frame's canvas (kept between live frames), three
 * working canvases (the live view paints one, the next frame fills another
 * and the newest finished one waits; a shot uses the first), the window's
 * luma and the picture's workspace. */
#define PIC_MAX (FILM_GBOY_MAX_W * FILM_GBOY_MAX_H)
#define LIVE_BUFS 3
#define WORK_BYTES ((1 + LIVE_BUFS) * FILM_GBOY_CANVAS_BYTES + 3 * PIC_MAX)
static unsigned char *work;
static int work_failed;
static unsigned char *frame_cv, *live_cv, *luma, *pic_work, *live_bufs[LIVE_BUFS];
static int frame_drawn = -1;                 /* which frame and resolution frame_cv holds */
static film_gboy_exposure drawn_ex;          /* and the exposure it prints */
static void live_task(void);
static volatile int lv_task, lv_busy;
#define LIVE_PRIORITY 0x1F                    /* ZrnoLive: below every Canon task (DryOS: higher runs later) */
/* Measured on the camera: 0x1C and 0x1A ran no faster than
 * 0x1F (5-6 fps); at 0x19, SpyTask's and the develop task's level, the
 * camera switched itself off seconds after the live view started. */

void film_gbui_poll(void)
{
    static int loaded;
    if (!loaded) { loaded = 1; film_gbui_init(); }
    film_rsui_poll(engine == 2);
    film_grui_poll();
    if (picture_engine() && !work && !work_failed) {
        work = malloc(WORK_BYTES);
        work_failed = !work;
        if (work) {
            int b;
            frame_cv = work; live_cv = frame_cv + FILM_GBOY_CANVAS_BYTES;
            for (b = 0; b < LIVE_BUFS; b++) live_bufs[b] = live_cv + b * FILM_GBOY_CANVAS_BYTES;
            luma = live_cv + LIVE_BUFS * FILM_GBOY_CANVAS_BYTES;
            pic_work = luma + PIC_MAX;
        }
    }
#ifndef FILM_GBUI_SYNC_LIVE
    if (work && !lv_task) lv_task = CreateTask("ZrnoLive", LIVE_PRIORITY, 0x2000, live_task) >= 0 ? 1 : -1;
#else
    if (work) lv_task = 1;                   /* the Mac simulator: frames made in place */
#endif
    if (picture_engine() && (unsigned)get_tick_count() - cal_tick > 3000u && !shooting_in_progress() && !film_ui_busy()) {
        cal_tick = get_tick_count();
        calibrate = !stat(GBOY_CAL_FILE, 0);
    }
    if (dirty && (unsigned)get_tick_count() - dirty_tick > 1000u && !shooting_in_progress() && !film_ui_busy()) {
        dirty = 0;
        store();
    }
}
static void changed(void) { dirty = 1; dirty_tick = get_tick_count(); }

/* ---- Menu rows ---------------------------------------------------------- */
static unsigned char step(unsigned char v, int change, int n) { return (unsigned char)((v + change % n + n) % n); }
const char *film_gbui_menu_engine(int change, int arg)
{
    (void)arg;
    if (change) { engine = (engine + change % ENGINES + ENGINES) % ENGINES; changed_engine = 1; changed(); }
    return engine_names[engine];
}
/* Looks: the engine and ZrnoBoy's settings. */
int film_gbui_engine(void) { return engine; }
void film_gbui_set_engine(int e)
{
    if (e >= 0 && e < ENGINES && e != engine) { engine = e; changed_engine = 1; changed(); }
}
void film_gbui_get(film_gboy_settings *s) { *s = set; }
void film_gbui_set(const film_gboy_settings *s)
{
    film_gboy_settings n = set;
    if (s->palette < FILM_GBOY_PALETTES) n.palette = s->palette;
    if (s->frame < FILM_GBOY_FRAMES) n.frame = s->frame;
    if (s->contrast < FILM_GBOY_CONTRASTS) n.contrast = s->contrast;
    if (s->brightness < FILM_GBOY_BRIGHTNESS) n.brightness = s->brightness;
    if (s->edges < FILM_GBOY_EDGES) n.edges = s->edges;
    if (s->dither < 2) n.dither = s->dither;
    if (s->resolution < FILM_GBOY_RESOLUTIONS) n.resolution = s->resolution;
    if (memcmp(&n, &set, sizeof(n))) { set = n; changed(); }
}
const char *film_gbui_menu_palette(int change, int arg)
{
    (void)arg;
    if (change) { set.palette = step(set.palette, change, FILM_GBOY_PALETTES); changed(); }
    return film_gboy_palette_name(set.palette);
}
const char *film_gbui_menu_frame(int change, int arg)
{
    (void)arg;
    if (change) { set.frame = step(set.frame, change, FILM_GBOY_FRAMES); changed(); }
    return film_gboy_frame_name(set.frame);
}
const char *film_gbui_menu_contrast(int change, int arg)
{
    (void)arg;
    /* The cartridge's steps stop at the ends rather than wrapping. */
    if (change) { int c = set.contrast + change; set.contrast = (unsigned char)(c < 0 ? 0 : c >= FILM_GBOY_CONTRASTS ? FILM_GBOY_CONTRASTS - 1 : c); changed(); }
    return film_gboy_contrast_name(set.contrast);
}
const char *film_gbui_menu_brightness(int change, int arg)
{
    (void)arg;
    if (change) { int c = set.brightness + change; set.brightness = (unsigned char)(c < 0 ? 0 : c >= FILM_GBOY_BRIGHTNESS ? FILM_GBOY_BRIGHTNESS - 1 : c); changed(); }
    return film_gboy_brightness_name(set.brightness);
}
const char *film_gbui_menu_edges(int change, int arg)
{
    (void)arg;
    if (change) { set.edges = step(set.edges, change, FILM_GBOY_EDGES); changed(); }
    return film_gboy_edges_name(set.edges);
}
const char *film_gbui_menu_dither(int change, int arg)
{
    (void)arg;
    if (change) { set.dither = !set.dither; changed(); }
    return set.dither ? "On" : "Off";
}
const char *film_gbui_menu_live(int change, int arg)
{
    (void)arg;
    if (change) { live_off = !live_off; changed(); }
    return live_off ? "Off" : "On";
}
const char *film_gbui_menu_resolution(int change, int arg)
{
    (void)arg;
    if (change) { set.resolution = step(set.resolution, change, FILM_GBOY_RESOLUTIONS); changed(); }
    return film_gboy_resolution_name(set.resolution);
}

/* ---- The shot ------------------------------------------------------------- */
/* The exposure the Viewfinder frame prints: the camera's current one in
 * the live view, the shot's own in the photo. */
static int shot_metered;                     /* the shot's exposure is being read */
static void read_exposure(film_gboy_exposure *e)
{
    e->tv96 = shooting_get_tv96();
    /* Canon has not metered before the first half-press: its Tv reads 0,
     * which would print 1". (A shot's own value is always metered.) */
    if (!e->tv96 && !shooting_in_progress() && !shot_metered) e->tv96 = FILM_GBOY_TV_UNKNOWN;
    e->av96 = shooting_get_av96();
    if (!e->av96) e->av96 = shooting_get_user_av96();
    e->iso = shooting_get_iso_market();
    e->ev96 = shooting_get_ev_correction1();
}
static film_gboy_settings shot_set;
static film_gboy_exposure shot_ex;
static unsigned shot_seed;
static volatile int shot_armed, shot_engine;
const char *film_gbui_stage_text(void)
{
    if (shot_engine != 2) return "Dithering in 4 shades";
    return film_rsui_stage_text();
}
int film_gbui_arm(unsigned seed)
{
    shot_engine = engine;
    shot_armed = picture_engine() && work != 0 && (engine != 2 || film_rsui_arm(seed));
    shot_set = set;
    shot_metered = 1; read_exposure(&shot_ex); shot_metered = 0;
    shot_seed = seed;
    return shot_armed;
}
/* Its own canvas and luma (the live view does not run during a shot, but
 * frame_cv may be redrawn by the menu meanwhile, so the shot uses live_cv). */
int film_gbui_render(unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                     unsigned char *row, void (*progress)(unsigned, unsigned))
{
    film_gboy_geom g;
    film_gboy_levels lv;
    unsigned step = width >= 2400 ? 2 : 1;
    if (!shot_armed || !work) return -110;
    while (lv_busy) msleep(10);              /* a live frame in progress uses the same buffers */
    if (calibrate) { shot_armed = 0; return film_gboy_paint_chart(frame, width, height, stride, row) ? -111 : 0; }
    if (shot_engine == 2) { shot_armed = 0; return film_rsui_render(frame, width, height, stride, progress); }
    film_gboy_geometry(shot_set.frame, shot_set.resolution, &g);
    film_gboy_frame(shot_set.frame, shot_set.resolution, live_cv, shot_seed, &shot_ex);
    film_gboy_sample_uyvy(frame, width, height, stride, g.ww, g.wh, step, luma);
    if (progress) progress(height / 4, height);
    film_gboy_levels_measure(luma, g.ww * g.wh, &lv);
    film_gboy_picture(luma, pic_work, &shot_set, &g, &lv, shot_seed, live_cv);
    shot_armed = 0;
    return film_gboy_paint_uyvy(frame, width, height, stride, live_cv, &g, shot_set.palette, row, progress) ? -111 : 0;
}

/* ---- Live view ------------------------------------------------------------ */
/* ZrnoBoy: at most 10 frames a second (about 7 at 256 x 192), about the
 * console camera's own rate at its default exposure. Print: up to 15.
 *
 * The work (sampling the viewport, dithering, the frame) runs in a task of
 * its own, ZrnoLive, below every Canon task. Done in SpyTask, 60-90 ms at a
 * time at 256 x 192, it held up Canon's live-image work: closing the Frame
 * preview then asserted in LiveImageTask (MotionVector.c 337, its result
 * queue empty; seen on the camera). SpyTask now only paints a
 * finished frame into the overlay.
 * ZrnoLive runs on its own while SpyTask says the live view is wanted:
 * it makes frames at the engine's rate into three canvases, never the one
 * SpyTask is painting nor the newest finished one, so neither task waits
 * for the other. (With one request per painted frame, a frame came only
 * every 246 ms on the camera although it took 150: SpyTask took a while to
 * collect each one.) The live image is read through the cache
 * after a clean and invalidate: uncached, sampling took 39 ms, cached 23.
 * film_live_frames counts painted frames and film_live_ms is the last
 * frame's time in ZrnoLive (both read over USB to measure the rate). */
#define LIVE_MS 100u
#define LIVE_MS_HIGH 150u
#define LIVE_MS_PRINT 66u
volatile unsigned film_live_frames, film_live_ms;
static int live_drawn;
static unsigned live_tick;
static film_gboy_levels live_lv = {16, 235};
static unsigned live_seed = 1;
/* What SpyTask wants (written by it, read by ZrnoLive at a frame's start). */
static volatile unsigned want_tick;
static volatile int want_engine, want_mirror, want_fresh;
static film_gboy_settings want_set;
static film_gboy_exposure want_ex;
/* The canvases: the newest finished frame (-1: none), the one SpyTask is
 * painting (-1: none); each canvas's engine and geometry. */
static volatile int ready_buf = -1, paint_buf = -1;
static int buf_engine[LIVE_BUFS], shown_engine;
static film_gboy_geom buf_g[LIVE_BUFS];
/* ZrnoLive's copy for the frame it makes. */
static film_gboy_settings lv_set;
static film_gboy_exposure lv_ex;
static int lv_mirror, lv_engine, lv_fresh;
static film_gboy_geom lv_g;

/* Clean and invalidate the whole D-cache by index (as film_stage.c does
 * before reading a frame through the cache). */
#if defined(__arm__)
static void __attribute__((target("arm"),noinline)) live_cache_flush(void)
{
    unsigned config, size, seg, i;
    asm volatile("mrc p15, 0, %0, c0, c0, 1" : "=r"(config));
    size = (config >> 18) & 0xFu;
    if (!size) return;
    size = 0x200u << size;
    for (seg = 0; seg < 4; seg++)
        for (i = 0; i < size / 4; i += 32)
            asm volatile("mcr p15, 0, %0, c7, c14, 2" :: "r"((seg << 30) | i) : "memory");
    asm volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory");
}
#else
static void live_cache_flush(void) {}
#endif

/* ZrnoLive: one frame into canvas cv. */
static void live_frame(unsigned char *cv)
{
    film_gboy_geom g;
    film_gboy_levels target;
    unsigned char *view = vid_get_viewport_live_fb();
    if (!view) return;
    /* The viewport is uncached memory: read it through the cache instead,
     * fresh after the clean and invalidate (Canon only writes it, by DMA). */
    live_cache_flush();
    view = (unsigned char *)((unsigned long)view & ~(unsigned long)(unsigned)camera_info.cam_uncached_bit);
    if (lv_engine == 2) {
        /* Print: the viewport's samples fill luma and pic_work (three
         * planes, contiguous), the ink masks fill cv. */
        film_rsui_live_frame(view + vid_get_viewport_image_offset(), vid_get_viewport_width_proper(),
                             vid_get_viewport_height_proper(), vid_get_viewport_byte_width(), lv_mirror, lv_fresh, luma, cv);
        return;
    }
    film_gboy_geometry(lv_set.frame, lv_set.resolution, &g);
    if (frame_drawn != lv_set.frame + 256 * lv_set.resolution ||
        (lv_set.frame == FILM_GBOY_FRAME_VIEWFINDER && memcmp(&lv_ex, &drawn_ex, sizeof(lv_ex)))) {
        film_gboy_frame(lv_set.frame, lv_set.resolution, frame_cv, 0, &lv_ex);
        frame_drawn = lv_set.frame + 256 * lv_set.resolution;
        drawn_ex = lv_ex;
    }
    memcpy(cv, frame_cv, g.cw * g.ch);
    film_gboy_sample_live(view + vid_get_viewport_image_offset(), vid_get_viewport_width_proper(),
                          vid_get_viewport_height_proper(), vid_get_viewport_byte_width(), g.ww, g.wh, luma);
    if (lv_mirror) {
        /* Swung round to face the lens, the panel shows the live view as a
         * mirror; the overlay is written pre-mirrored so the frame reads the
         * right way round, so the picture is flipped to match the mirror. */
        unsigned x, y;
        for (y = 0; y < g.wh; y++)
            for (x = 0; x < g.ww / 2; x++) {
                unsigned char *r = luma + y * g.ww, t = r[x];
                r[x] = r[g.ww - 1 - x]; r[g.ww - 1 - x] = t;
            }
    }
    film_gboy_levels_measure(luma, g.ww * g.wh, &target);
    if (lv_fresh) live_lv = target; else film_gboy_levels_follow(&live_lv, &target);
    live_seed = live_seed * 1103515245u + 12345u;
    film_gboy_picture(luma, pic_work, &lv_set, &g, &live_lv, live_seed, cv);
    lv_g = g;
}
/* ZrnoLive's frame: the wanted settings, a free canvas, the frame, then
 * hand it over as the newest. A free canvas is neither the newest finished
 * one nor the one being painted: SpyTask marks paint_buf before it clears
 * ready_buf, and this reads ready_buf first, so it never picks either. */
static void live_make(void)
{
    unsigned t = get_tick_count();
    int r = ready_buf, p = paint_buf, b = 0;
    while (b == r || b == p) b++;
    lv_set = want_set; lv_ex = want_ex; lv_engine = want_engine; lv_mirror = want_mirror;
    lv_fresh = want_fresh; want_fresh = 0;
    live_frame(live_bufs[b]);
    buf_engine[b] = lv_engine; buf_g[b] = lv_g;
    film_live_ms = (unsigned)get_tick_count() - t;
    ready_buf = b;                           /* last: the frame and its facts are set */
}
static unsigned live_cap(int eng, const film_gboy_settings *st) { return eng == 2 ? LIVE_MS_PRINT : st->resolution ? LIVE_MS_HIGH : LIVE_MS; }
static void __attribute__((unused)) live_task(void)
{
    unsigned started = 0;
    for (;;) {
        unsigned now = get_tick_count();
        /* Only while SpyTask wants the live view (it says so on every pass),
         * at the engine's rate, and never while a shot is armed: it uses
         * the same buffers. */
        if (shot_armed || now - want_tick > 300u || now - started < live_cap(want_engine, &want_set)) { msleep(10); continue; }
        started = now;
        lv_busy = 1;
        if (!shot_armed) live_make();
        lv_busy = 0;
    }
}

static int live_wanted(void)
{
    if (!picture_engine() || !work || lv_task != 1 || !film_lut_processing()) return 0;
    if (engine == 2 && !film_rsui_ready()) return 0;
    if (!camera_info.state.mode_rec || conf.rotate_osd) return 0;
    if (!camera_info.state.gui_mode_none && !film_preview_showing()) return 0;
    if (live_off && !film_preview_showing()) return 0;      /* Settings: Live preview Off */
    if (film_ui_busy() || film_ui_themed()) return 0;
    return film_route_reason() == FILM_ROUTE_READY;
}
/* Erase the screen we drew, once, when the live view stops (not under a
 * menu, which paints the whole screen itself; under a row's preview, which
 * leaves the live view showing, so a look that leaves ZrnoBoy or Print shows
 * Canon's live view again). */
static void live_erase(void)
{
    ready_buf = -1;
    if (!live_drawn) return;
    live_drawn = 0;
    if ((camera_info.state.gui_mode_none || film_preview_showing()) && film_draw_begin(0, 0, camera_screen.width - 1, camera_screen.height - 1)) {
        if (!film_draw_clear(COLOR_TRANSPARENT)) draw_rectangle(0, 0, camera_screen.width - 1, camera_screen.height - 1, MAKE_COLOR(COLOR_TRANSPARENT, COLOR_TRANSPARENT), RECT_BORDER0 | DRAW_FILLED);
        film_draw_end();
    }
}
int film_gbui_live_shown(void) { return live_drawn && (unsigned)get_tick_count() - live_tick < 1000u; }
unsigned film_gbui_palette_slot(int slot, unsigned deflt)
{
    int k = slot == FILM_GBUI_SLOT0 ? 0 : slot == FILM_GBUI_SLOT1 ? 1 : slot == FILM_GBUI_SLOT2 ? 2 : slot == FILM_GBUI_SLOT3 ? 3 :
            slot == FILM_GBUI_SLOT4 ? 4 : slot == FILM_GBUI_SLOT5 ? 5 : slot == FILM_GBUI_SLOT6 ? 6 : -1, y, u, v;
    if (k < 0 || !film_gbui_live_shown()) return deflt;
    /* Print: ink mask k (two inks use the first four slots; with three,
     * the all-ink mask is drawn in black). */
    if (shown_engine == 2) return (unsigned)k < film_rsui_live_colours() ? film_rsui_live_colour((unsigned)k) : deflt;
    if (k > 3) return deflt;
    film_gboy_yuv(set.palette, (unsigned)k, &y, &u, &v);
    return 0x03000000u | (unsigned)y << 16 | (unsigned)(u & 255) << 8 | (unsigned)(v & 255);
}
int film_gbui_live_draw(void (*card)(void))
{
    unsigned char *shadow, index[8];
    unsigned now = get_tick_count(), stride;
    int p;
    if (!live_wanted()) { live_erase(); return 0; }
    /* Tell ZrnoLive what is wanted now (settings, exposure, orientation). */
    want_set = set; want_engine = engine;
    read_exposure(&want_ex);
    want_mirror = film_screen_facing_lens();
    if (!live_drawn && !want_fresh) want_fresh = 1;
    want_tick = now;
#ifdef FILM_GBUI_SYNC_LIVE
    live_make();                             /* the Mac simulator: made in place */
#endif
    /* Paint the newest finished frame, if there is a new one. */
    p = ready_buf;
    if (p < 0) return live_drawn || lv_busy || now - live_tick < 1000u;   /* the last frame stays up meanwhile */
    paint_buf = p;                           /* first: ZrnoLive must not pick it */
    ready_buf = -1;
    if (!film_draw_begin_cover(0, 0, camera_screen.width - 1, camera_screen.height - 1)) { paint_buf = -1; if (ready_buf < 0) ready_buf = p; return 1; }
    shadow = film_draw_shadow(&stride);
    if (!shadow) { film_draw_end(); paint_buf = -1; return 0; }   /* no memory for the shadow: no live view */
    index[0] = chdk_colors[IDX_COLOR_GREEN]; index[1] = chdk_colors[IDX_COLOR_YELLOW];
    index[2] = chdk_colors[IDX_COLOR_YELLOW_DK]; index[3] = chdk_colors[IDX_COLOR_YELLOW_LT];
    index[4] = chdk_colors[IDX_COLOR_BLUE]; index[5] = chdk_colors[IDX_COLOR_BLUE_LT];
    index[6] = chdk_colors[IDX_COLOR_MAGENTA]; index[7] = COLOR_BLACK;
    if (buf_engine[p] == 2)
        film_riso_live_paint(live_bufs[p], FILM_RISO_LIVE_W, FILM_RISO_LIVE_H, index, shadow, stride, camera_screen.physical_width, camera_screen.height);
    else
        film_gboy_live_paint(live_bufs[p], &buf_g[p], index, shadow, stride, camera_screen.physical_width, camera_screen.height);
    shown_engine = buf_engine[p];
    if (card) card();
    film_draw_end();
    paint_buf = -1;
    live_drawn = 1; live_tick = now;
    film_live_frames++;
    return 1;
}
