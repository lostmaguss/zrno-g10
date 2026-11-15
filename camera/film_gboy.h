/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. ZrnoBoy: the second engine, a 1998 pocket-console
 * camera. Photos become 4 shades on a coarse pixel grid, dithered by a 4x4
 * threshold matrix as the console's camera cartridge did (one analog value
 * against three thresholds per matrix cell), with the sensor's edge
 * enhancement, a 4-colour palette and an optional frame.
 *
 * Geometry. The canvas is 160 x 120 (twice that at the higher resolution,
 * everything scaled), 4:3 like the photo. With a frame the
 * picture is a 128 x 96 window at (16,12): the console's 128-pixel width and
 * 16-pixel side border, but 4:3 like the sensor, so the frame goes round the
 * whole picture and never over it. Without a frame the picture fills the
 * canvas. An L frame (3648 x 2736) is 22.8 camera pixels per canvas pixel.
 * The live view shows the whole canvas, each pixel two screen rows tall and
 * 2-3 screen pixels wide, which fills the G12's 720 x 240 overlay exactly:
 * the shooting screen shows what the photo will be.
 *
 * Kernels only: no allocation, no file I/O. Shades are 0 (lightest) to 3
 * (darkest); FILM_GBOY_PIC marks canvas pixels that show the picture. */
#ifndef FILM_GBOY_H
#define FILM_GBOY_H
#include <stddef.h>

#define FILM_GBOY_FRAMED_W 160
#define FILM_GBOY_FRAMED_H 120
#define FILM_GBOY_WIN_X 16
#define FILM_GBOY_WIN_Y 12
#define FILM_GBOY_WIN_W 128
#define FILM_GBOY_WIN_H 96
#define FILM_GBOY_FULL_W 160
#define FILM_GBOY_FULL_H 120
/* Resolution: scale 1 (128 x 96 picture, the default) or 2 (256 x 192 in
 * a 320 x 240 canvas, the frame's pixel art drawn twice the size). */
#define FILM_GBOY_RESOLUTIONS 2
#define FILM_GBOY_MAX_W (FILM_GBOY_FULL_W * FILM_GBOY_RESOLUTIONS)
#define FILM_GBOY_MAX_H (FILM_GBOY_FULL_H * FILM_GBOY_RESOLUTIONS)
#define FILM_GBOY_CANVAS_BYTES (FILM_GBOY_MAX_W * FILM_GBOY_MAX_H)
#define FILM_GBOY_PIC 4

enum {FILM_GBOY_PALETTES = 10, FILM_GBOY_FRAMES = 11};   /* frame 0 is Off */
#define FILM_GBOY_FRAME_VIEWFINDER 4                  /* the one that prints the exposure */
#define FILM_GBOY_CONTRASTS 16  /* the cartridge's 16 steps, shown 1 .. 16 (default 8) */
#define FILM_GBOY_BRIGHTNESS 7   /* -3 .. +3, stored 0 .. 6 */
#define FILM_GBOY_EDGES 4        /* Off, Soft (25%), Camera (50%, the cartridge's), Strong (100%) */

typedef struct {
    unsigned char palette, frame, contrast, brightness, edges, dither, resolution;
} film_gboy_settings;

const char *film_gboy_palette_name(unsigned palette);
const char *film_gboy_frame_name(unsigned frame);
const char *film_gboy_contrast_name(unsigned contrast);
const char *film_gboy_brightness_name(unsigned brightness);
const char *film_gboy_edges_name(unsigned edges);
const char *film_gboy_resolution_name(unsigned resolution);

/* The palette's shade as 0xRRGGBB, and as BT.601 full-range Y with signed
 * U and V (the camera's frame and the G12 overlay palette both use these). */
unsigned film_gboy_rgb(unsigned palette, unsigned shade);
void film_gboy_yuv(unsigned palette, unsigned shade, int *y, int *u, int *v);

/* Canvas and picture window for a frame (0: no frame) at a resolution
 * (0: 160 x 120 canvas, 1: 320 x 240). */
typedef struct { unsigned cw, ch, wx, wy, ww, wh; } film_gboy_geom;
void film_gboy_geometry(unsigned frame, unsigned resolution, film_gboy_geom *g);

/* The exposure the Viewfinder frame prints: APEX96 Tv and Av, market ISO
 * and exposure compensation (APEX96, 32 per third of a stop). */
typedef struct { short tv96, av96, iso, ev96; } film_gboy_exposure;
#define FILM_GBOY_TV_UNKNOWN (-32768)     /* not metered yet: printed "--" */
/* Its text as the camera shows it: "1/125" or "2\"5", "F8.0", "ISO400"
 * (nearest third stop each; "AUTO" for ISO 0). tv: 8 bytes, av: 6, iso: 9. */
void film_gboy_exposure_text(const film_gboy_exposure *e, char *tv, char *av, char *iso);

/* Draw the frame into canvas (cw * ch bytes, see film_gboy_geometry):
 * shades round the picture, which is all FILM_GBOY_PIC (no frame reaches
 * into it). seed varies the frames that differ per shot (the light leak);
 * exposure is what the Viewfinder frame prints (0: 1/125, F8.0, ISO400, 0). */
void film_gboy_frame(unsigned frame, unsigned resolution, unsigned char *canvas, unsigned seed,
                     const film_gboy_exposure *exposure);

/* Exposure levels: the picture is stretched from lo..hi to the sensor's
 * range, as the cartridge's auto-exposure kept the scene in its few levels.
 * measure: the 5% and 95% points of luma (n pixels). follow: move cur
 * toward target by at most a quarter per call (the cartridge's exposure
 * steps), for the live view. */
typedef struct { int lo, hi; } film_gboy_levels;
void film_gboy_levels_measure(const unsigned char *luma, unsigned n, film_gboy_levels *lv);
void film_gboy_levels_follow(film_gboy_levels *cur, const film_gboy_levels *target);

/* The picture: luma (ww * wh, 0..255, the window's size) to shades, written
 * where the canvas shows FILM_GBOY_PIC. The sensor's signal: stretched by
 * lv, with its column bars and noise (seed), edge enhancement
 * P + a(4P - N - S - E - W), then each pixel against the three thresholds
 * of its matrix cell. work: ww * wh * 2 bytes. */
void film_gboy_picture(const unsigned char *luma, unsigned char *work, const film_gboy_settings *s,
                       const film_gboy_geom *g, const film_gboy_levels *lv, unsigned seed,
                       unsigned char *canvas);
/* The three thresholds of matrix cell (x, y) on the sensor's scale. */
void film_gboy_thresholds(unsigned contrast, int dither, unsigned x, unsigned y, int t[3]);

/* Box-averaged luma of the picture's view of a signed UYVY422 frame (Y at
 * odd bytes): the frame's middle cropped to ww:wh, step: sample every step-th
 * pixel pair and row (1: all). */
void film_gboy_sample_uyvy(const unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                           unsigned ww, unsigned wh, unsigned step, unsigned char *luma);
/* The same for the live view's UYVYYY (U Y V Y Y Y per 4 pixels) buffer of
 * width x height pixels and rowbytes per row. */
void film_gboy_sample_live(const unsigned char *view, unsigned width, unsigned height, unsigned rowbytes,
                           unsigned ww, unsigned wh, unsigned char *luma);

/* Paint the canvas over the whole frame (signed UYVY422), each canvas pixel a
 * block of the frame scaled to fill it, in the palette's colours. row: a
 * scratch row of stride bytes. Returns 0, or -1 for bad arguments. */
int film_gboy_paint_uyvy(unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                         const unsigned char *canvas, const film_gboy_geom *g, unsigned palette,
                         unsigned char *row, void (*progress)(unsigned, unsigned));

/* Colour calibration: a chart of flat patches (COLS x ROWS) of known
 * Y and signed U, V over the whole frame, to measure what Canon's JPEG step
 * does to written colours. */
#define FILM_GBOY_CHART_COLS 18
#define FILM_GBOY_CHART_ROWS 12
#define FILM_GBOY_CHART_PATCHES (FILM_GBOY_CHART_COLS * FILM_GBOY_CHART_ROWS)
void film_gboy_chart_level(unsigned patch, int *y, int *u, int *v);
int film_gboy_paint_chart(unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                          unsigned char *row);

/* Paint the whole canvas into an overlay of 8-bit colour indexes, cols x
 * rows bytes (720 x 240 on the G12). At 160 x 120 each pixel is 2 rows and
 * 2.25 screen pixels (two bytes each) wide, square on its screen; at
 * 320 x 240, 1 row and 1.125 screen pixels. Each pixel takes whole screen
 * pixels; shade k in colour index[k]. */
void film_gboy_live_paint(const unsigned char *canvas, const film_gboy_geom *g, const unsigned char index[4],
                          unsigned char *overlay, unsigned stride, unsigned cols, unsigned rows);
#endif
