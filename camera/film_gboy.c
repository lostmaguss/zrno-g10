/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. ZrnoBoy engine kernels (see film_gboy.h). The frames are
 * original designs drawn procedurally here; none copies a console frame. */
#include "film_gboy.h"
#include "film_font.h"

/* ---- Names and palettes --------------------------------------------- */

static const char *const palette_names[FILM_GBOY_PALETTES] = {
    "Pea green", "Pocket", "Printer", "Backlight", "Zrno", "Sunset", "Ocean", "Candy", "Red visor", "Sepia"};
/* Lightest to darkest. Pea green is the green-on-green of the first
 * handheld's screen as emulators show it (with the lighter mid-green some
 * palette collections give, so the two light shades stay apart); Pocket its grey successor; Printer
 * thermal paper; Backlight the later model's blue-green glow; Zrno the
 * menu's own orange and navy; the rest are new. */
static const unsigned palettes[FILM_GBOY_PALETTES][4] = {
    {0x9BBC0F, 0x77A112, 0x306230, 0x0F380F},
    {0xC5CAA4, 0x8C926B, 0x4A5138, 0x181818},
    {0xF4F1E8, 0xB4B2AA, 0x67666A, 0x1E1E26},
    {0x7BE8D0, 0x3FB59A, 0x1A6E5E, 0x0A2E2A},
    {0xFFE8CF, 0xF95F01, 0x03246B, 0x0B0F1A},
    {0xFFD48A, 0xE8846B, 0x8C3B6E, 0x2B1640},
    {0xE2F6F2, 0x80C4CC, 0x2F6E9C, 0x102841},
    {0xFFF0F6, 0xF5A2C8, 0x9E52A3, 0x301238},
    {0xFF3B3B, 0xB00C0C, 0x5C0404, 0x120000},
    {0xF2E3C2, 0xC49562, 0x6E4A2C, 0x22140A}};
static const char *const frame_names[FILM_GBOY_FRAMES] = {
    "Off", "Classic", "Film strip", "Cicmany", "Viewfinder", "Stamp", "Tatras", "Wheat", "Window",
    "TV set", "Light leak"};
static const char *const contrast_names[FILM_GBOY_CONTRASTS] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
static const char *const brightness_names[FILM_GBOY_BRIGHTNESS] = {"-3", "-2", "-1", "0", "+1", "+2", "+3"};
static const char *const edges_names[FILM_GBOY_EDGES] = {"Off", "Soft", "Camera", "Strong"};
static const char *const resolution_names[FILM_GBOY_RESOLUTIONS] = {"128x96", "256x192"};

#define NAME(list, n, i) ((i) < (n) ? list[i] : "?")
const char *film_gboy_palette_name(unsigned i) { return NAME(palette_names, FILM_GBOY_PALETTES, i); }
const char *film_gboy_frame_name(unsigned i) { return NAME(frame_names, FILM_GBOY_FRAMES, i); }
const char *film_gboy_contrast_name(unsigned i) { return NAME(contrast_names, FILM_GBOY_CONTRASTS, i); }
const char *film_gboy_brightness_name(unsigned i) { return NAME(brightness_names, FILM_GBOY_BRIGHTNESS, i); }
const char *film_gboy_edges_name(unsigned i) { return NAME(edges_names, FILM_GBOY_EDGES, i); }
const char *film_gboy_resolution_name(unsigned i) { return NAME(resolution_names, FILM_GBOY_RESOLUTIONS, i); }

unsigned film_gboy_rgb(unsigned palette, unsigned shade)
{
    return palettes[palette < FILM_GBOY_PALETTES ? palette : 0][shade & 3];
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int div_round(int a, int b) { return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b); }

void film_gboy_yuv(unsigned palette, unsigned shade, int *y, int *u, int *v)
{
    unsigned c = film_gboy_rgb(palette, shade);
    int r = c >> 16, g = (c >> 8) & 255, b = c & 255;
    /* BT.601 full range, x1000. */
    *y = clamp(div_round(299 * r + 587 * g + 114 * b, 1000), 0, 255);
    *u = clamp(div_round(-169 * r - 331 * g + 500 * b, 1000), -128, 127);
    *v = clamp(div_round(500 * r - 419 * g - 81 * b, 1000), -128, 127);
}

void film_gboy_geometry(unsigned frame, unsigned resolution, film_gboy_geom *g)
{
    unsigned s = resolution < FILM_GBOY_RESOLUTIONS ? resolution + 1 : 1;
    if (frame && frame < FILM_GBOY_FRAMES) {
        g->cw = FILM_GBOY_FRAMED_W * s; g->ch = FILM_GBOY_FRAMED_H * s;
        g->wx = FILM_GBOY_WIN_X * s; g->wy = FILM_GBOY_WIN_Y * s; g->ww = FILM_GBOY_WIN_W * s; g->wh = FILM_GBOY_WIN_H * s;
    } else {
        g->cw = g->ww = FILM_GBOY_FULL_W * s; g->ch = g->wh = FILM_GBOY_FULL_H * s;
        g->wx = g->wy = 0;
    }
}

/* ---- Drawing on the canvas ------------------------------------------ */

#define CW FILM_GBOY_FRAMED_W
#define CH FILM_GBOY_FRAMED_H
#define WX0 FILM_GBOY_WIN_X
#define WY0 FILM_GBOY_WIN_Y
#define WX1 (FILM_GBOY_WIN_X + FILM_GBOY_WIN_W - 1)
#define WY1 (FILM_GBOY_WIN_Y + FILM_GBOY_WIN_H - 1)
#define PIC FILM_GBOY_PIC

static unsigned char *cv;
static const film_gboy_exposure *exposure;      /* for the Viewfinder frame */
static const unsigned char bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static void px(int x, int y, int s)
{
    if (x >= 0 && y >= 0 && x < CW && y < CH) cv[y * CW + x] = (unsigned char)s;
}
static int get(int x, int y) { return x >= 0 && y >= 0 && x < CW && y < CH ? cv[y * CW + x] : 0; }
static void rect(int x1, int y1, int x2, int y2, int s)
{
    int x, y;
    for (y = y1; y <= y2; y++) for (x = x1; x <= x2; x++) px(x, y, s);
}
static void box(int x1, int y1, int x2, int y2, int s)
{
    rect(x1, y1, x2, y1, s); rect(x1, y2, x2, y2, s);
    rect(x1, y1, x1, y2, s); rect(x2, y1, x2, y2, s);
}
/* Shade a or b by the 4x4 matrix: level 0 all b, 16 all a. */
static int mix(int x, int y, int level, int a, int b) { return bayer4[(y & 3) * 4 + (x & 3)] < level ? a : b; }
static void disc(int cx, int cy, int r, int s)
{
    int x, y;
    for (y = -r; y <= r; y++) for (x = -r; x <= r; x++)
        if (x * x + y * y <= r * r + r) px(cx + x, cy + y, s);
}
static void ring(int cx, int cy, int r, int s)
{
    int x, y;
    for (y = -r; y <= r; y++) for (x = -r; x <= r; x++) {
        int d = x * x + y * y;
        if (d <= r * r + r && d > (r - 1) * (r - 1) + (r - 1)) px(cx + x, cy + y, s);
    }
}
static int text_width(const char *t) { int n = 0; while (t[n]) n++; return n ? n * 6 - 1 : 0; }
static void text(int x, int y, const char *t, int s)
{
    for (; *t; t++, x += 6) {
        int c = *t, r, k;
        if (c < 32 || c > 126) c = '?';
        for (r = 0; r < 7; r++)
            for (k = 0; k < 5; k++)
                if (film_font5x7[c - 32][r] & (0x10 >> k)) px(x + k, y + r, s);
    }
}
/* Pixel art: digits are shades, '.' leaves the canvas as it is. */
static void art(int x, int y, const char *const *rows, int n)
{
    int r, c;
    for (r = 0; r < n; r++)
        for (c = 0; rows[r][c]; c++)
            if (rows[r][c] >= '0' && rows[r][c] <= '3') px(x + c, y + r, rows[r][c] - '0');
}
static unsigned rnd(unsigned *s)
{
    unsigned x = *s ? *s : 0x2545f491u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static void window_open(void) { rect(WX0, WY0, WX1, WY1, PIC); }
static void keyline(int s) { box(WX0 - 1, WY0 - 1, WX1 + 1, WY1 + 1, s); }
/* The Zrno bean (the menu's logo), small, in shades. */
static const char *const bean[7] = {
    "....2222.", "..2222232", ".22222322", "222333222", "2232222..", "232222...", ".2222...."};

/* Every frame below is drawn round the 128 x 96 picture at (16,12) on the
 * 160 x 120 canvas: a 16-pixel band each side, as the console's own frames,
 * and 12 rows above and below. */

/* 1 Classic: a light mat with a dark keyline, a soft drop shadow and the
 * bean in the bottom band. */
static void frame_classic(void)
{
    int x, y;
    rect(0, 0, CW - 1, CH - 1, 1);
    for (y = 1; y < CH; y += 4) for (x = 1 + ((y >> 2) & 1) * 2; x < CW; x += 4)
        if (x < WX0 - 4 || x > WX1 + 4 || y < WY0 - 4 || y > WY1 + 4) px(x, y, 2);
    box(WX0 - 4, WY0 - 4, WX1 + 4, WY1 + 4, 2);
    rect(WX0 - 3, WY0 - 3, WX1 + 3, WY1 + 3, 0);
    rect(WX0 + 1, WY1 + 2, WX1 + 2, WY1 + 2, 2);           /* shadow */
    rect(WX1 + 2, WY0 + 1, WX1 + 2, WY1 + 2, 2);
    keyline(3);
    rect(WX1 - 10, WY1 + 5, WX1, WY1 + 11, 1);
    art(WX1 - 9, WY1 + 5, bean, 7);
    for (x = 0; x < 4; x++) { rect(WX0 - 9, WY0 + 8 + x * 6, WX0 - 7, WY0 + 10 + x * 6, 3); rect(WX1 + 7, WY1 - 10 - x * 6, WX1 + 9, WY1 - 8 - x * 6, 3); }
}

/* 2 Film strip: the picture on a strip of film running down the frame,
 * sprocket holes either side and a frame mark in the rebate. */
static void frame_strip(void)
{
    int y;
    rect(0, 0, CW - 1, CH - 1, 3);
    for (y = -2; y < CH; y += 10) {
        rect(WX0 - 12, y, WX0 - 5, y + 5, 0); px(WX0 - 12, y, 3); px(WX0 - 5, y, 3); px(WX0 - 12, y + 5, 3); px(WX0 - 5, y + 5, 3);
        rect(WX1 + 5, y, WX1 + 12, y + 5, 0); px(WX1 + 5, y, 3); px(WX1 + 12, y, 3); px(WX1 + 5, y + 5, 3); px(WX1 + 12, y + 5, 3);
    }
    rect(0, 0, 2, CH - 1, 2); rect(CW - 3, 0, CW - 1, CH - 1, 2);   /* the strip's edges */
    keyline(2);
    { static const char *const tri[5] = {"1....", "111..", "11111", "111..", "1...."}; art(WX1 - 5, WY1 + 4, tri, 5); }
}

/* 3 Cicmany: white geometric motifs on dark logs, after the painted houses
 * of the Slovak village (a traditional, public-domain pattern). */
static void motif(int cx, int cy)
{
    int k;
    for (k = -3; k <= 3; k++) { px(cx + k, cy - 3 + (k < 0 ? -k : k), 0); px(cx + k, cy + 3 - (k < 0 ? -k : k), 0); }
    px(cx, cy, 0); px(cx, cy - 5, 0); px(cx, cy + 5, 0); px(cx - 5, cy, 0); px(cx + 5, cy, 0);
}
static void frame_cicmany(void)
{
    int x, y;
    rect(0, 0, CW - 1, CH - 1, 3);
    for (y = 6; y < CH; y += 8) for (x = 0; x < CW; x++) if ((x * 7 + y * 3) % 23) px(x, y, 2);   /* log seams */
    rect(WX0 - 7, WY0 - 7, WX1 + 7, WY1 + 7, 3);
    /* A zigzag band round the picture. */
    for (x = WX0 - 5; x <= WX1 + 5; x++) {
        int z = (x & 3) < 2 ? (x & 3) : 4 - (x & 3);
        px(x, WY0 - 5 + z, 0); px(x, WY1 + 5 - z, 0);
    }
    for (y = WY0 - 5; y <= WY1 + 5; y++) {
        int z = (y & 3) < 2 ? (y & 3) : 4 - (y & 3);
        px(WX0 - 5 + z, y, 0); px(WX1 + 5 - z, y, 0);
    }
    for (y = 20; y < CH - 12; y += 20) { motif(4, y); motif(CW - 5, y); }
    for (x = WX0 + 8; x < WX1; x += 16) { px(x, 1, 0); px(x - 1, 2, 0); px(x + 1, 2, 0); px(x, 3, 0); px(x, CH - 2, 0); px(x - 1, CH - 3, 0); px(x + 1, CH - 3, 0); px(x, CH - 4, 0); }
    motif(4, 4); motif(CW - 5, 4); motif(4, CH - 5); motif(CW - 5, CH - 5);
    keyline(0);
}

/* 4 Viewfinder: a rangefinder's bright-line corners round the picture, a
 * focus scale either side and the exposure readout below. */
static void frame_viewfinder(void)
{
    int k, y;
    rect(0, 0, CW - 1, CH - 1, 3);
    for (k = 0; k < 16; k++) {
        px(WX0 - 3 + k, WY0 - 3, 0); px(WX0 - 3, WY0 - 3 + k, 0);
        px(WX1 + 3 - k, WY0 - 3, 0); px(WX1 + 3, WY0 - 3 + k, 0);
        px(WX0 - 3 + k, WY1 + 3, 0); px(WX0 - 3, WY1 + 3 - k, 0);
        px(WX1 + 3 - k, WY1 + 3, 0); px(WX1 + 3, WY1 + 3 - k, 0);
    }
    keyline(2);
    for (y = WY0 + 16; y <= WY1 - 16; y += 4) {
        int len = (y - WY0) % 16 ? 3 : 6;
        rect(WX0 - 4 - len, y, WX0 - 5, y, (y - WY0) % 16 ? 2 : 1);
        rect(WX1 + 5, y, WX1 + 4 + len, y, (y - WY0) % 16 ? 2 : 1);
    }
    {
        /* The shot's exposure: shutter, aperture, a meter of five marks
         * (-2 .. +2 EV) with the compensation lit, and ISO. */
        static const film_gboy_exposure standard = {672, 576, 400, 0};
        const film_gboy_exposure *e = exposure ? exposure : &standard;
        char tv[8], av[6], iso[9];
        int ev = e->ev96 >= 0 ? (e->ev96 + 48) / 96 : -((-e->ev96 + 48) / 96);
        ev = clamp(ev, -2, 2) + 2;
        film_gboy_exposure_text(e, tv, av, iso);
        text(WX0 + 2, WY1 + 4, tv, 0);
        text(WX0 + 41, WY1 + 4, av, 0);
        text(WX1 - text_width(iso) - 1, WY1 + 4, iso, 1);
        for (k = 0; k < 5; k++) rect(WX0 + 66 + k * 4, WY1 + 7 - (k == ev), WX0 + 67 + k * 4, WY1 + 8 + (k == ev), k == ev ? 0 : 2);
    }
}

/* 5 Stamp: the picture is a postage stamp with perforated edges on an
 * envelope and a postmark tucked under a corner. */
static void frame_stamp(void)
{
    int x, y, k;
    for (y = 0; y < CH; y++) for (x = 0; x < CW; x++) px(x, y, mix(x, y, 3, 2, 1));
    rect(WX0 - 10, WY0 - 9, WX1 + 10, WY1 + 9, 0);
    for (x = WX0 - 7; x < WX1 + 10; x += 6) { disc(x, WY0 - 9, 2, mix(x, WY0 - 9, 3, 2, 1)); disc(x, WY1 + 9, 2, mix(x, WY1 + 9, 3, 2, 1)); }
    for (y = WY0 - 6; y < WY1 + 9; y += 6) { disc(WX0 - 10, y, 2, mix(WX0 - 10, y, 3, 2, 1)); disc(WX1 + 10, y, 2, mix(WX1 + 10, y, 3, 2, 1)); }
    keyline(3);
    {
        int cx = WX0 + 3, cy = WY1 + 5, a, b;
        for (b = -8; b <= 8; b++) for (a = -8; a <= 8; a++) {
            int d = a * a + b * b;
            if ((d <= 8 * 8 && d > 7 * 7) || (d <= 5 * 5 && d > 4 * 4)) px(cx + a, cy + b, 3);
        }
        for (k = 0; k < 2; k++)
            for (x = cx + 11; x < WX0 + 70; x++) {
                int t = x & 7, w = t < 4 ? t / 2 : (7 - t) / 2;
                px(x, WY1 + 3 + k * 4 + w, 3);
            }
    }
}

/* 6 Tatras: a dithered sky, a low sun and two mountain ridges rising
 * behind the picture. */
static void frame_tatras(void)
{
    int x, y;
    unsigned s = 0x7a7a5u;
    static int far[CW], near_[CW];             /* static: the shot draws this in Canon's develop task */
    for (y = 0; y < CH; y++) for (x = 0; x < CW; x++) px(x, y, mix(x, y, clamp(y * 16 / 60, 0, 16), 1, 0));
    disc(CW - 9, 5, 4, 0); ring(CW - 9, 5, 4, 1);
    {
        static const int fp[][2] = {{0, 40}, {8, 28}, {14, 44}, {40, 70}, {80, 96}, {120, 74}, {146, 40}, {152, 26}, {159, 36}};
        static const int np[][2] = {{0, 70}, {10, 86}, {16, 100}, {60, 112}, {100, 110}, {144, 96}, {150, 78}, {159, 88}};
        int i;
        for (i = 0; i + 1 < (int)(sizeof(fp) / sizeof(fp[0])); i++)
            for (x = fp[i][0]; x <= fp[i + 1][0]; x++)
                far[x] = fp[i][1] + (fp[i + 1][1] - fp[i][1]) * (x - fp[i][0]) / (fp[i + 1][0] - fp[i][0]) + (int)(rnd(&s) % 3) - 1;
        for (i = 0; i + 1 < (int)(sizeof(np) / sizeof(np[0])); i++)
            for (x = np[i][0]; x <= np[i + 1][0]; x++)
                near_[x] = np[i][1] + (np[i + 1][1] - np[i][1]) * (x - np[i][0]) / (np[i + 1][0] - np[i][0]) + (int)(rnd(&s) % 3) - 1;
    }
    for (x = 0; x < CW; x++) {
        for (y = far[x]; y < CH; y++) px(x, y, y < far[x] + 3 && far[x] < 50 ? 0 : 2);
        for (y = near_[x]; y < CH; y++) px(x, y, 3);
    }
    keyline(3);
    { static const char *const bird[3] = {"3...3", ".3.3.", "..3.."}; art(WX0 + 30, 2, bird, 3); art(WX0 + 40, 6, bird, 3); }
}

/* 7 Wheat: ears of wheat rising up the sides of a light mat with scattered
 * grains ("zrno" is a grain). */
static void ear(int x, int base, int top, int lean)
{
    int y;
    for (y = base; y >= top; y--) px(x + (base - y) * lean / 40, y, 3);
    for (y = top + 2; y < top + 22; y += 4) {
        int cx = x + (base - y) * lean / 40;
        static const char *const grain[4] = {".33.", "3223", "3223", ".33."};
        art(cx - 4, y, grain, 4); art(cx + 1, y + 2, grain, 4);
    }
    { static const char *const tip[3] = {".2.", "212", ".2."}; art(x + (base - top) * lean / 40 - 1, top - 2, tip, 3); }
}
static void frame_wheat(void)
{
    int x, y;
    unsigned s = 0x51ed27u;
    rect(0, 0, CW - 1, CH - 1, 0);
    for (y = 2; y < CH; y += 7) for (x = 2 + (y % 2) * 5; x < CW; x += 9) {
        unsigned r = rnd(&s);
        if (r & 1) { px(x, y, 1); px(x + 1, y, 1); px(x, y + 1, 1); px(x + 1, y + 1, 2); }
    }
    box(WX0 - 3, WY0 - 3, WX1 + 3, WY1 + 3, 1);
    keyline(3);
    ear(4, CH - 1, 14, 1); ear(10, CH - 1, 44, -1); ear(CW - 5, CH - 1, 14, -1); ear(CW - 11, CH - 1, 44, 1);
}

/* 8 Window: a desktop window of the 90s, title bar, scroll bar, tool
 * buttons and a status bar. */
static void frame_window(void)
{
    int y, k;
    rect(0, 0, CW - 1, CH - 1, 1);
    box(0, 0, CW - 1, CH - 1, 3);
    rect(1, 1, CW - 2, WY0 - 3, 3);
    for (k = 0; k < 3; k++) {
        int x = CW - 10 - k * 9;
        rect(x, 2, x + 6, 8, 1); rect(x, 2, x + 6, 2, 0); rect(x, 2, x, 8, 0);
    }
    rect(CW - 8, 5, CW - 6, 5, 3);                                  /* close: a bar */
    box(CW - 17, 4, CW - 15, 6, 3);                                 /* maximise */
    rect(CW - 26, 7, CW - 24, 7, 3);                                /* minimise */
    rect(WX0 - 1, WY0 - 1, WX1 + 1, WY0 - 1, 2); rect(WX0 - 1, WY0 - 1, WX0 - 1, WY1 + 1, 2);   /* sunken */
    rect(WX0 - 1, WY1 + 1, WX1 + 1, WY1 + 1, 0); rect(WX1 + 1, WY0 - 1, WX1 + 1, WY1 + 1, 0);
    /* Scroll bar. */
    for (y = WY0 + 10; y < WY1 - 9; y++) for (k = WX1 + 3; k <= WX1 + 12; k++) px(k, y, mix(k, y, 8, 2, 1));
    for (k = 0; k < 2; k++) {
        int y0 = k ? WY1 - 9 : WY0;
        rect(WX1 + 3, y0, WX1 + 12, y0 + 9, 1); box(WX1 + 3, y0, WX1 + 12, y0 + 9, 3);
        rect(WX1 + 7, y0 + (k ? 6 : 3), WX1 + 8, y0 + (k ? 6 : 3), 3);
        rect(WX1 + 6, y0 + (k ? 5 : 4), WX1 + 9, y0 + (k ? 5 : 4), 3);
        rect(WX1 + 5, y0 + (k ? 4 : 5), WX1 + 10, y0 + (k ? 4 : 5), 3);
    }
    rect(WX1 + 3, WY0 + 26, WX1 + 12, WY0 + 42, 1); box(WX1 + 3, WY0 + 26, WX1 + 12, WY0 + 42, 3);
    /* Tool buttons: pencil, fill, eraser, magnifier. */
    for (k = 0; k < 4; k++) {
        int y0 = WY0 + k * 14;
        rect(WX0 - 14, y0, WX0 - 3, y0 + 11, k ? 1 : 0); box(WX0 - 14, y0, WX0 - 3, y0 + 11, 3);
    }
    { static const char *const pencil[8] = {"......3", ".....32", "....32.", "...32..", "..32...", ".33....", "333....", "3......"}; art(WX0 - 12, WY0 + 2, pencil, 8); }
    { static const char *const fill[8] = {"..33...", ".3..3..", "3....3.", "33333.3", ".3333.3", "..33..3", "......3", "......."}; art(WX0 - 12, WY0 + 16, fill, 8); }
    { static const char *const eraser[8] = {".......", "...3333", "..3..33", ".3..3.3", "3333..3", "3..3.3.", "33333..", "......."}; art(WX0 - 12, WY0 + 30, eraser, 8); }
    { static const char *const lens[8] = {".333...", "3...3..", "3...3..", "3...3..", ".333...", "....33.", ".....33", "......3"}; art(WX0 - 12, WY0 + 44, lens, 8); }
    rect(1, WY1 + 3, CW - 2, WY1 + 3, 2);
}

/* 9 TV set: a wooden set, a bezel, knobs and a speaker grille. */
static void frame_tv(void)
{
    int x, y;
    unsigned s = 0x1234567u;
    rect(0, 0, CW - 1, CH - 1, 2);
    for (y = 1; y < CH; y += 3) for (x = 0; x < CW; x++) if ((rnd(&s) & 7) == 0) { px(x, y, 3); px(x + 1, y, 3); }   /* wood grain */
    rect(WX0 - 3, WY0 - 3, WX1 + 3, WY1 + 3, 1);
    box(WX0 - 3, WY0 - 3, WX1 + 3, WY1 + 3, 3);
    px(WX0 - 3, WY0 - 3, 2); px(WX1 + 3, WY0 - 3, 2); px(WX0 - 3, WY1 + 3, 2); px(WX1 + 3, WY1 + 3, 2);
    keyline(3);
    for (y = 0; y < 2; y++) {
        int cy = WY0 + 14 + y * 20;
        disc(WX1 + 10, cy, 4, 1); ring(WX1 + 10, cy, 4, 3);
        rect(WX1 + 10, cy - 3, WX1 + 10, cy - 1, 3);
    }
    for (y = WY0 + 50; y < WY1 - 2; y += 4) rect(WX1 + 7, y, WX1 + 13, y + 1, 3);
}

/* 10 Light leak: a black film rebate with light fogging in from one or two
 * places on its edge up to the picture: a white core fading out through the
 * light shades, a different shape on every shot. */
static void frame_leak(unsigned seed)
{
    int x, y, k, n;
    unsigned s = seed ? seed : 0x6b43a9b5u;
    rect(0, 0, CW - 1, CH - 1, 3);
    /* A rough inner edge to the rebate. */
    for (x = WX0 - 1; x <= WX1 + 1; x++) { if (rnd(&s) & 1) px(x, WY0 - 1, 2); if (rnd(&s) & 1) px(x, WY1 + 1, 2); }
    for (y = WY0 - 1; y <= WY1 + 1; y++) { if (rnd(&s) & 1) px(WX0 - 1, y, 2); if (rnd(&s) & 1) px(WX1 + 1, y, 2); }
    n = 1 + (int)(rnd(&s) & 1);
    for (k = 0; k < n; k++) {
        /* A source on the edge, and an ellipse reaching in from it. */
        int edge = (int)(rnd(&s) % 4), sx, sy, rx, ry, along = (int)(rnd(&s) % 100);
        if (edge < 2) { sx = edge ? CW - 1 : 0; sy = 10 + along * (CH - 20) / 100; rx = 24 + (int)(rnd(&s) % 16); ry = 30 + (int)(rnd(&s) % 30); }
        else { sy = edge == 3 ? CH - 1 : 0; sx = 16 + along * (CW - 32) / 100; rx = 30 + (int)(rnd(&s) % 30); ry = 18 + (int)(rnd(&s) % 12); }
        for (y = 0; y < CH; y++) for (x = 0; x < CW; x++) {
            /* f: 64 at the source, 0 on the ellipse. */
            int dx = (x - sx) * 64 / rx, dy = (y - sy) * 64 / ry, d2 = dx * dx + dy * dy, f, c;
            if (d2 >= 64 * 64) continue;
            f = 64 - d2 / 64;
            c = get(x, y);
            if (f >= 40) px(x, y, mix(x, y, clamp(f - 40, 0, 16), 0, 1));
            else if (f >= 16) { if (mix(x, y, (f - 16) * 16 / 24, 1, -1) == 1 && c >= 2) px(x, y, 1); else if (c == 3) px(x, y, 2); }
            else if (c == 3 && mix(x, y, f, 2, -1) == 2) px(x, y, 2);
        }
    }
}

/* Canon's third-stop names: shutter speeds from 30" (Tv96 -480) to
 * 1/4000 (1152), f-numbers from 1.0 (Av96 0) to 22 (864), ISO 50 to 12800. */
static const char *const tv_names[] = {
    "30\"", "25\"", "20\"", "15\"", "13\"", "10\"", "8\"", "6\"", "5\"", "4\"", "3\"2", "2\"5", "2\"", "1\"6", "1\"3",
    "1\"", "0\"8", "0\"6", "0\"5", "0\"4", "0\"3", "1/4", "1/5", "1/6", "1/8", "1/10", "1/13", "1/15", "1/20", "1/25", "1/30",
    "1/40", "1/50", "1/60", "1/80", "1/100", "1/125", "1/160", "1/200", "1/250", "1/320", "1/400", "1/500", "1/640", "1/800",
    "1/1000", "1/1250", "1/1600", "1/2000", "1/2500", "1/3200", "1/4000"};
static const char *const av_names[] = {
    "F1.0", "F1.1", "F1.2", "F1.4", "F1.6", "F1.8", "F2.0", "F2.2", "F2.5", "F2.8", "F3.2", "F3.5", "F4.0", "F4.5",
    "F5.0", "F5.6", "F6.3", "F7.1", "F8.0", "F9.0", "F10", "F11", "F13", "F14", "F16", "F18", "F20", "F22"};
static const short iso_values[] = {50, 64, 80, 100, 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1250, 1600, 2000,
                                   2500, 3200, 4000, 5000, 6400, 8000, 10000, 12800};
static int third(int v96) { return v96 >= 0 ? (v96 + 16) / 32 : -((-v96 + 16) / 32); }
static void copy(char *d, const char *s, unsigned n) { unsigned i; for (i = 0; i + 1 < n && s[i]; i++) d[i] = s[i]; d[i] = 0; }
void film_gboy_exposure_text(const film_gboy_exposure *e, char *tv, char *av, char *iso)
{
    int t = clamp(third(e->tv96) + 15, 0, (int)(sizeof(tv_names) / sizeof(tv_names[0])) - 1);
    int a = clamp(third(e->av96), 0, (int)(sizeof(av_names) / sizeof(av_names[0])) - 1);
    copy(tv, e->tv96 == FILM_GBOY_TV_UNKNOWN ? "--" : tv_names[t], 8);
    copy(av, av_names[a], 6);
    if (e->iso <= 0) { copy(iso, "AUTO", 9); return; }
    {
        /* The nearest standard value: ratios compared as products. */
        unsigned i, best = 0;
        for (i = 1; i < sizeof(iso_values) / sizeof(iso_values[0]); i++) {
            long a2 = (long)iso_values[i] * iso_values[best];
            if ((long)e->iso * e->iso > a2) best = i;             /* past the geometric midpoint */
        }
        {
            char num[6]; int v = iso_values[best], n = 0, k;
            do { num[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 5);
            iso[0] = 'I'; iso[1] = 'S'; iso[2] = 'O';
            for (k = 0; k < n; k++) iso[3 + k] = num[n - 1 - k];
            iso[3 + n] = 0;
        }
    }
}

void film_gboy_frame(unsigned frame, unsigned resolution, unsigned char *canvas, unsigned seed,
                     const film_gboy_exposure *ex)
{
    film_gboy_geom g;
    unsigned i;
    exposure = ex;
    film_gboy_geometry(frame, resolution, &g);
    if (!frame || frame >= FILM_GBOY_FRAMES) {
        for (i = 0; i < g.cw * g.ch; i++) canvas[i] = PIC;
        return;
    }
    cv = canvas;
    switch (frame) {
    case 1: frame_classic(); break;
    case 2: frame_strip(); break;
    case 3: frame_cicmany(); break;
    case 4: frame_viewfinder(); break;
    case 5: frame_stamp(); break;
    case 6: frame_tatras(); break;
    case 7: frame_wheat(); break;
    case 8: frame_window(); break;
    case 9: frame_tv(); break;
    default: frame_leak(seed); break;
    }
    window_open();                 /* the frame goes round the picture, never over it */
    cv = 0;
    /* The higher resolution: the frame's pixel art at twice the size,
     * spread in place from the last pixel back (each lands at or past its
     * source). */
    if (g.cw > CW) {
        unsigned s = g.cw / CW, x, y, k;
        for (y = CH; y-- > 0;)
            for (x = CW; x-- > 0;) {
                unsigned char v = canvas[y * CW + x];
                for (k = 0; k < s * s; k++) canvas[(y * s + k / s) * g.cw + x * s + k % s] = v;
            }
    }
}

/* ---- The picture ------------------------------------------------------ */

/* The cartridge's dither anchors (its "high light" table): one row of four
 * per contrast step, on the sensor's scale. Cell n's three thresholds lie
 * between consecutive anchors, at the cell's place r/16 in the 4x4 order
 * below (published by the cartridge's reverse engineers; see ZRNOBOY.md). */
static const unsigned char anchors[FILM_GBOY_CONTRASTS][4] = {
    {0x80, 0x8F, 0xD0, 0xE6}, {0x82, 0x90, 0xC8, 0xE3}, {0x84, 0x90, 0xC0, 0xE0}, {0x85, 0x91, 0xB8, 0xDD},
    {0x86, 0x91, 0xB1, 0xDB}, {0x87, 0x92, 0xAA, 0xD8}, {0x88, 0x92, 0xA5, 0xD5}, {0x89, 0x92, 0xA2, 0xD2},
    {0x8A, 0x92, 0xA1, 0xC8}, {0x8B, 0x92, 0xA0, 0xBE}, {0x8C, 0x92, 0x9E, 0xB4}, {0x8D, 0x92, 0x9C, 0xAC},
    {0x8E, 0x92, 0x9B, 0xA5}, {0x8F, 0x92, 0x99, 0xA0}, {0x90, 0x92, 0x97, 0x9A}, {0x92, 0x92, 0x92, 0x92}};
static const unsigned char order[16] = {0, 12, 3, 15, 8, 4, 11, 7, 2, 14, 1, 13, 10, 6, 9, 5};
/* The sensor's signal for a scene stretched to 0..255: it drives only the
 * upper part of the converter, and a scene filling it spans about these
 * levels (the calibration a sensor-replica project found to match the
 * console's photos). */
#define SENSOR_LO 135
#define SENSOR_HI 195

void film_gboy_thresholds(unsigned contrast, int dither, unsigned x, unsigned y, int t[3])
{
    const unsigned char *a = anchors[contrast < FILM_GBOY_CONTRASTS ? contrast : 7];
    int r = dither ? order[(y & 3) * 4 + (x & 3)] : 8, k;
    for (k = 0; k < 3; k++) t[k] = a[k] + r * (a[k + 1] - a[k]) / 16;
}

void film_gboy_levels_measure(const unsigned char *luma, unsigned n, film_gboy_levels *lv)
{
    static unsigned hist[256];                 /* static: called in Canon's develop task too */
    unsigned i, sum = 0, cut = n / 20;
    int lo = 0, hi = 255;
    for (i = 0; i < 256; i++) hist[i] = 0;
    for (i = 0; i < n; i++) hist[luma[i]]++;
    for (i = 0; i < 256; i++) { sum += hist[i]; if (sum > cut) { lo = (int)i; break; } }
    sum = 0;
    for (i = 256; i-- > 0;) { sum += hist[i]; if (sum > cut) { hi = (int)i; break; } }
    /* A flat scene is not stretched into noise: at least 64 levels. */
    if (hi - lo < 64) { int m = (lo + hi) / 2; lo = m - 32; hi = m + 32; if (lo < 0) { hi -= lo; lo = 0; } if (hi > 255) { lo -= hi - 255; hi = 255; } }
    lv->lo = lo; lv->hi = hi;
}

void film_gboy_levels_follow(film_gboy_levels *cur, const film_gboy_levels *t)
{
    int d = t->lo - cur->lo, e = t->hi - cur->hi;
    cur->lo += d / 4 ? d / 4 : d;
    cur->hi += e / 4 ? e / 4 : e;
}

void film_gboy_picture(const unsigned char *luma, unsigned char *work, const film_gboy_settings *s,
                       const film_gboy_geom *g, const film_gboy_levels *lv, unsigned seed,
                       unsigned char *canvas)
{
    static const int alpha4[FILM_GBOY_EDGES] = {0, 1, 2, 4};    /* edge ratio x4 */
    unsigned w = g->ww, h = g->wh, x, y;
    unsigned char *sig = work;                                  /* the sensor's signal */
    int range = lv->hi - lv->lo > 16 ? lv->hi - lv->lo : 16;
    int bright = ((int)(s->brightness < FILM_GBOY_BRIGHTNESS ? s->brightness : 3) - 3) * 6;
    int a4 = alpha4[s->edges < FILM_GBOY_EDGES ? s->edges : 2];
    unsigned rs = seed ? seed : 1;
    int t[16][3];
    for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) film_gboy_thresholds(s->contrast, s->dither, x, y, t[y * 4 + x]);
    /* Luma to the sensor's level, once per value (the second half of work:
     * no division per pixel, and nothing large on the caller's stack). */
    unsigned char *map = work + w * h;
    for (x = 0; x < 256; x++) map[x] = (unsigned char)clamp(((int)x - lv->lo) * (SENSOR_HI - SENSOR_LO) / range + SENSOR_LO + bright, 0, 255);
    /* A little noise while dithering: a sensor level up or down on about
     * one pixel in twenty, which the matrix spreads like the sensor's grain.
     * No column pattern: without the matrix to hide it, a level of column
     * bias showed as vertical lines. */
    for (x = 0; x < w * h; x++) {
        int v = map[luma[x]];
        if (s->dither) {
            unsigned r;
            rs = rs * 1103515245u + 12345u;
            r = rs >> 24;
            v += r < 6 ? -1 : r > 249 ? 1 : 0;
        }
        sig[x] = (unsigned char)clamp(v, 0, 255);
    }
    /* Edges, P + a(4P - N - S - E - W), and each pixel against its cell's
     * three thresholds. Lean inner loop (the live view runs it ten times a
     * second): edge rows repeat their neighbour, no division. */
    for (y = 0; y < h; y++) {
        const unsigned char *row = sig + y * w, *up = y ? row - w : row, *dn = y + 1 < h ? row + w : row;
        const int (*cell)[3] = t + (y & 3) * 4;
        unsigned char *out = canvas + (g->wy + y) * g->cw + g->wx;
        for (x = 0; x < w; x++) {
            int p = row[x], v = p, shade;
            const int *k = cell[x & 3];
            if (a4) {
                int we = x ? row[x - 1] : p, ea = x + 1 < w ? row[x + 1] : p;
                v = p + ((a4 * (4 * p - up[x] - dn[x] - we - ea) + 2) >> 2);
            }
            shade = v < k[0] ? 3 : v < k[1] ? 2 : v < k[2] ? 1 : 0;
            if (out[x] == PIC) out[x] = (unsigned char)shade;
        }
    }
}

/* ---- Sampling the camera's frames ------------------------------------- */

/* 65536 / n, rounded (recip[0] unused). */
static unsigned recip[64];
static void recip_fill(void)
{
    unsigned n;
    if (recip[1]) return;
    for (n = 1; n < 64; n++) recip[n] = (65536u + n / 2) / n;
}

/* The window's crop of a width x height frame: the middle, ww:wh. */
static void crop(unsigned width, unsigned height, unsigned ww, unsigned wh, unsigned *x0, unsigned *y0, unsigned *cw, unsigned *ch)
{
    if ((unsigned long long)width * wh > (unsigned long long)height * ww) { *ch = height; *cw = (unsigned)((unsigned long long)height * ww / wh); }
    else { *cw = width; *ch = (unsigned)((unsigned long long)width * wh / ww); }
    *x0 = (width - *cw) / 2; *y0 = (height - *ch) / 2;
}

void film_gboy_sample_uyvy(const unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                           unsigned ww, unsigned wh, unsigned step, unsigned char *luma)
{
    unsigned x0, y0, cw, ch, gx, gy, sx, sy;
    if (!step) step = 1;
    crop(width, height, ww, wh, &x0, &y0, &cw, &ch);
    x0 &= ~1u;
    for (gy = 0; gy < wh; gy++) {
        unsigned ya = y0 + gy * ch / wh, yb = y0 + (gy + 1) * ch / wh;
        for (gx = 0; gx < ww; gx++) {
            unsigned xa = (x0 + gx * cw / ww) & ~1u, xb = (x0 + (gx + 1) * cw / ww) & ~1u, sum = 0, n = 0;
            if (xb <= xa) xb = xa + 2;
            for (sy = ya; sy < yb || sy == ya; sy += step) {
                const unsigned char *r = frame + (size_t)sy * stride;
                for (sx = xa; sx < xb; sx += 2 * step) { sum += r[2 * sx + 1] + r[2 * sx + 3]; n += 2; }
            }
            luma[gy * ww + gx] = (unsigned char)(n ? (sum + n / 2) / n : 0);
        }
    }
}

void film_gboy_sample_live(const unsigned char *view, unsigned width, unsigned height, unsigned rowbytes,
                           unsigned ww, unsigned wh, unsigned char *luma)
{
    /* UYVYYY: Y of pixel i at byte 6*(i/4) + {1,3,4,5}[i%4]. The camera's
     * CPU has no divide: column bounds once per call, and each cell's mean
     * by a reciprocal (a cell holds at most a few dozen samples). */
    static const unsigned char yo[4] = {1, 3, 4, 5};
    static unsigned short off[1024];               /* each pixel's Y byte in its row */
    unsigned short xs[FILM_GBOY_MAX_W + 1];
    unsigned x0, y0, cw, ch, gx, gy, sx, sy;
    if (ww > FILM_GBOY_MAX_W || width > 1024) return;
    recip_fill();
    for (sx = 0; sx < width; sx++) off[sx] = (unsigned short)(6 * (sx >> 2) + yo[sx & 3]);
    crop(width, height, ww, wh, &x0, &y0, &cw, &ch);
    for (gx = 0; gx <= ww; gx++) xs[gx] = (unsigned short)(x0 + gx * cw / ww);
    if (ch < 3 * wh && cw < 4 * ww) {
        /* Small cells (the higher resolution, about 2 x 2 pixels): the mean
         * of a cell's first two pixels on its first row. */
        for (gy = 0; gy < wh; gy++) {
            const unsigned char *r = view + (size_t)(y0 + gy * ch / wh) * rowbytes;
            unsigned char *o = luma + gy * ww;
            for (gx = 0; gx < ww; gx++) { unsigned xa = xs[gx]; o[gx] = (unsigned char)((r[off[xa]] + r[off[xa + 1 < width ? xa + 1 : xa]] + 1) >> 1); }
        }
        return;
    }
    for (gy = 0; gy < wh; gy++) {
        unsigned ya = y0 + gy * ch / wh, yb = y0 + (gy + 1) * ch / wh;
        if (yb <= ya) yb = ya + 1;
        for (gx = 0; gx < ww; gx++) {
            unsigned xa = xs[gx], xb = xs[gx + 1] > xa ? xs[gx + 1] : xa + 1, sum = 0, n = 0;
            /* Every other row and pixel: the buffer is uncached, and the
             * cell is only 4 or 5 pixels across. */
            for (sy = ya; sy < yb; sy += 2) {
                const unsigned char *r = view + (size_t)sy * rowbytes;
                for (sx = xa; sx < xb; sx += 2) { sum += r[off[sx]]; n++; }
            }
            luma[gy * ww + gx] = (unsigned char)(n < 64 ? (sum * recip[n] + 32768u) >> 16 : sum / n);
        }
    }
}

/* ---- Painting the frame ------------------------------------------------ */

int film_gboy_paint_uyvy(unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                         const unsigned char *canvas, const film_gboy_geom *g, unsigned palette,
                         unsigned char *row, void (*progress)(unsigned, unsigned))
{
    int yy[4], uu[4], vv[4];
    unsigned k, x, cy, y = 0;
    if (!frame || !row || !canvas || !width || !height || (width & 1) || stride < 2 * width) return -1;
    for (k = 0; k < 4; k++) film_gboy_yuv(palette, k, &yy[k], &uu[k], &vv[k]);
    for (cy = 0; cy < g->ch; cy++) {
        const unsigned char *c = canvas + cy * g->cw;
        unsigned y2 = (cy + 1) * height / g->ch;
        /* One frame row of this canvas row. Each canvas pixel starts on an
         * even column (18 or 20 wide at L): a UYVY pair shares its chroma,
         * and so does the JPEG's 4:2:2, so an edge inside a pair left a
         * one-pixel fringe of the wrong colour at every other edge. */
        unsigned k, half = width / 2;
        for (k = 0, x = 0; k < g->cw; k++) {
            unsigned x1 = 2 * ((k + 1) * half / g->cw), s0 = c[k] & 3;
            for (; x < x1; x += 2) {
                row[2 * x] = (unsigned char)uu[s0];
                row[2 * x + 1] = (unsigned char)yy[s0];
                row[2 * x + 2] = (unsigned char)vv[s0];
                row[2 * x + 3] = (unsigned char)yy[s0];
            }
        }
        for (; y < y2; y++) {
            unsigned char *d = frame + (size_t)y * stride;
            if (!(((size_t)d | (size_t)row) & 3) && !(width & 1)) {
                unsigned *dw = (unsigned *)(void *)d;
                const unsigned *sw = (const unsigned *)(const void *)row;
                for (x = 0; x < width / 2; x++) dw[x] = sw[x];
            } else for (x = 0; x < 2 * width; x++) d[x] = row[x];
        }
        if (progress && !(cy & 7)) progress(y, height);
    }
    return 0;
}

void film_gboy_live_paint(const unsigned char *canvas, const film_gboy_geom *g, const unsigned char index[4],
                          unsigned char *overlay, unsigned stride, unsigned cols, unsigned rows)
{
    static unsigned short psrc[480];
    unsigned r, k, p, pairs = cols / 2;
    unsigned short val[4];
    if (g->cw > FILM_GBOY_MAX_W || pairs > 480) return;
    /* Whole screen pixels: CHDK draws each of its pixels as two equal
     * bytes, and only pairs survive the swung-round screen's mirror intact,
     * so each picture pixel is a whole number of screen pixels (2 or 3 at
     * 160 across, 1 or 2 at 320), each written as one halfword with its
     * canvas column looked up. The columns and values once per call (the
     * camera's CPU has no divide instruction). */
    for (k = 0, p = 0; k < g->cw; k++) {
        unsigned end = ((k + 1) * pairs + g->cw / 2) / g->cw;
        for (; p < end; p++) psrc[p] = (unsigned short)k;
    }
    for (k = 0; k < 4; k++) val[k] = (unsigned short)(index[k] | index[k] << 8);
    for (r = 0; r < rows; r++) {
        const unsigned char *c = canvas + (r * g->ch / rows) * g->cw;
        unsigned short *d = (unsigned short *)(overlay + (size_t)r * stride);
        if (r && r * g->ch / rows == (r - 1) * g->ch / rows) {
            const unsigned short *prev = (const unsigned short *)(overlay + (size_t)(r - 1) * stride);
            for (p = 0; p < pairs; p++) d[p] = prev[p];         /* the canvas row's second screen row */
            continue;
        }
        for (p = 0; p < pairs; p++) d[p] = val[c[psrc[p]] & 3];
    }
}

/* ---- Colour calibration ------------------------------------------------ */

/* A chart of FILM_GBOY_CHART_PATCHES flat patches, 18 across and 12 down,
 * patch i at Y, U and V levels film_gboy_chart_level(i): what Canon's JPEG
 * step makes of written values, measured from one shot (tools/gboy_cal.py). */
static const signed short chart_y[6] = {16, 58, 100, 142, 184, 226}, chart_c[6] = {-100, -60, -20, 20, 60, 100};
void film_gboy_chart_level(unsigned i, int *y, int *u, int *v)
{
    *y = chart_y[i % 6]; *u = chart_c[(i / 6) % 6]; *v = chart_c[(i / 36) % 6];
}
int film_gboy_paint_chart(unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                          unsigned char *row)
{
    unsigned r, c, x, y = 0;
    if (!frame || !row || !width || !height || (width & 1) || stride < 2 * width) return -1;
    for (r = 0; r < FILM_GBOY_CHART_ROWS; r++) {
        unsigned y2 = (r + 1) * height / FILM_GBOY_CHART_ROWS;
        for (c = 0, x = 0; c < FILM_GBOY_CHART_COLS; c++) {
            int yy, uu, vv;
            unsigned x1 = 2 * ((c + 1) * (width / 2) / FILM_GBOY_CHART_COLS);
            film_gboy_chart_level(r * FILM_GBOY_CHART_COLS + c, &yy, &uu, &vv);
            for (; x < x1; x += 2) {
                row[2 * x] = (unsigned char)uu; row[2 * x + 1] = (unsigned char)yy;
                row[2 * x + 2] = (unsigned char)vv; row[2 * x + 3] = (unsigned char)yy;
            }
        }
        for (; y < y2; y++) {
            unsigned char *d = frame + (size_t)y * stride;
            for (x = 0; x < 2 * width; x++) d[x] = row[x];
        }
    }
    return 0;
}
