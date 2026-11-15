/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ZrnoBoy kernels (camera/film_gboy.c): the cartridge's thresholds, frame
 * and picture bounds, painting every image size, and the live overlay. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "film_gboy.h"

static unsigned char canvas[FILM_GBOY_CANVAS_BYTES];

static void test_thresholds(void)
{
    unsigned c, x, y;
    int t[3], u[3];
    for (c = 0; c < FILM_GBOY_CONTRASTS; c++)
        for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++) {
                film_gboy_thresholds(c, 1, x, y, t);
                assert(t[0] <= t[1] && t[1] <= t[2]);
                assert(t[0] >= 0x80 && t[2] <= 0xE6);
            }
    /* Cell (0,0) is the matrix's first place: exactly the anchors. */
    film_gboy_thresholds(7, 1, 0, 0, t);
    assert(t[0] == 0x89 && t[1] == 0x92 && t[2] == 0xA2);
    /* Step 16 is a single threshold: pure black and white. */
    film_gboy_thresholds(15, 1, 3, 1, t);
    assert(t[0] == 0x92 && t[1] == 0x92 && t[2] == 0x92);
    /* Without dithering every cell has the same three. */
    film_gboy_thresholds(4, 0, 0, 0, t);
    for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) { film_gboy_thresholds(4, 0, x, y, u); assert(!memcmp(t, u, sizeof(t))); }
}

static void test_frames(void)
{
    unsigned f, i, x, y, res;
    for (res = 0; res < FILM_GBOY_RESOLUTIONS; res++)
    for (f = 0; f < FILM_GBOY_FRAMES; f++) {
        film_gboy_geom g;
        unsigned pic = 0, outside = 0;
        film_gboy_geometry(f, res, &g);
        assert(g.cw * g.ch <= FILM_GBOY_CANVAS_BYTES && g.wx + g.ww <= g.cw && g.wy + g.wh <= g.ch);
        assert(g.cw * 3 == g.ch * 4 && g.cw == 160 * (res + 1));         /* 4:3, as the photo */
        memset(canvas, 0xEE, sizeof(canvas));
        film_gboy_frame(f, res, canvas, 99, 0);
        for (y = 0; y < g.ch; y++)
            for (x = 0; x < g.cw; x++) {
                unsigned v = canvas[y * g.cw + x];
                int inside = x >= g.wx && x < g.wx + g.ww && y >= g.wy && y < g.wy + g.wh;
                assert(v <= FILM_GBOY_PIC);
                if (v == FILM_GBOY_PIC) { assert(inside); pic++; }
                else if (!inside) outside++;
            }
        /* The frame goes round the whole picture, never over it, and the
         * picture is 4:3 like the sensor (its full view). */
        assert(pic == g.ww * g.wh && g.ww * 3 == g.wh * 4);
        if (f) assert(outside == g.cw * g.ch - g.ww * g.wh);
        for (i = g.cw * g.ch; i < FILM_GBOY_CANVAS_BYTES; i++) assert(canvas[i] == 0xEE);   /* nothing past the canvas */
        assert(strcmp(film_gboy_frame_name(f), "?"));
        if (res) {
            /* The higher resolution is the same frame, each pixel 2 x 2. */
            static unsigned char base[FILM_GBOY_CANVAS_BYTES];
            film_gboy_frame(f, 0, base, 99, 0);
            for (y = 0; y < g.ch; y++) for (x = 0; x < g.cw; x++) assert(canvas[y * g.cw + x] == base[(y / 2) * 160 + x / 2]);
        }
    }
    /* The light leak differs per shot; the other frames do not. */
    {
        static unsigned char other[FILM_GBOY_CANVAS_BYTES];
        film_gboy_frame(FILM_GBOY_FRAMES - 1, 0, canvas, 1, 0); film_gboy_frame(FILM_GBOY_FRAMES - 1, 0, other, 2, 0);
        assert(memcmp(canvas, other, 160 * 120));
        film_gboy_frame(5, 0, canvas, 1, 0); film_gboy_frame(5, 0, other, 2, 0);
        assert(!memcmp(canvas, other, 160 * 120));
    }
}

/* A frame with a horizontal ramp, a dark disc and a light square. */
static unsigned char *scene(unsigned w, unsigned h)
{
    unsigned char *f = malloc((size_t)w * h * 2);
    unsigned x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int dx = (int)x - (int)w / 3, dy = (int)y - (int)h / 2, v = (int)(x * 255 / w);
            if (dx * dx + dy * dy < (int)(h * h / 25)) v = 20;
            if (x > w * 2 / 3 && x < w * 5 / 6 && y > h / 4 && y < h / 2) v = 240;
            f[(size_t)y * w * 2 + 2 * x + 1] = (unsigned char)v;
            f[(size_t)y * w * 2 + 2 * x] = (unsigned char)((x & 1) ? 0 : 7);   /* chroma that must go */
        }
    return f;
}

static unsigned char luma[FILM_GBOY_MAX_W * FILM_GBOY_MAX_H], work[FILM_GBOY_MAX_W * FILM_GBOY_MAX_H * 2];
static void test_shot(unsigned w, unsigned h, unsigned frame, unsigned res)
{
    unsigned char *f = scene(w, h), *row = malloc(w * 2);
    film_gboy_settings s = {0, (unsigned char)frame, 7, 3, 2, 1, (unsigned char)res};
    film_gboy_geom g;
    film_gboy_levels lv;
    int yy[4], uu[4], vv[4], k, used[4] = {0, 0, 0, 0};
    unsigned x, y;
    film_gboy_geometry(frame, res, &g);
    film_gboy_frame(frame, res, canvas, 7, 0);
    film_gboy_sample_uyvy(f, w, h, w * 2, g.ww, g.wh, w >= 2400 ? 2 : 1, luma);
    film_gboy_levels_measure(luma, g.ww * g.wh, &lv);
    assert(lv.lo < lv.hi && lv.hi - lv.lo >= 64);
    film_gboy_picture(luma, work, &s, &g, &lv, 3, canvas);
    for (x = 0; x < g.cw * g.ch; x++) assert(canvas[x] < 4);           /* every picture pixel has a shade */
    assert(film_gboy_paint_uyvy(f, w, h, w * 2, canvas, &g, s.palette, row, 0) == 0);
    for (k = 0; k < 4; k++) film_gboy_yuv(s.palette, (unsigned)k, &yy[k], &uu[k], &vv[k]);
    /* Every pixel is one of the palette's four, chroma included. */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x += 2) {
            const unsigned char *p = f + (size_t)y * w * 2 + 2 * x;
            int a = -1, b = -1;
            for (k = 0; k < 4; k++) {
                if (p[1] == yy[k] && (signed char)p[0] == uu[k] && (signed char)p[2] == vv[k]) a = k;
                if (p[3] == yy[k]) b = k;
            }
            assert(a >= 0 && b >= 0);
            used[a] = used[b] = 1;
        }
    assert(used[0] && used[3]);
    /* Each UYVY pair is one colour: canvas pixels start on even columns,
     * so no pair straddles an edge and takes the wrong chroma. */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x += 2) assert(f[(size_t)y * w * 2 + 2 * x + 1] == f[(size_t)y * w * 2 + 2 * x + 3]);
    {
        unsigned edges = 0, prev = f[(size_t)(h / 2) * w * 2 + 1];
        for (x = 0; x < w; x++) {
            unsigned v = f[(size_t)(h / 2) * w * 2 + 2 * x + 1];
            if (v != prev) { assert(!(x & 1)); edges++; prev = v; }   /* edges only on even columns */
        }
        assert(edges > 4);
    }
    free(f); free(row);
}

static void test_live(void)
{
    /* A 720 x 480 UYVYYY viewport and the 720 x 240 overlay. */
    unsigned char *view = malloc(1080 * 480), *overlay = malloc(960 * 270);
    const unsigned char index[4] = {0xB6, 0xBD, 0xBE, 0xBF};
    unsigned f, i, r, res;
    for (i = 0; i < 1080 * 480; i++) view[i] = (unsigned char)(i * 7 / 9);
    for (res = 0; res < FILM_GBOY_RESOLUTIONS; res++)
    for (f = 0; f < FILM_GBOY_FRAMES; f++) {
        film_gboy_settings s = {(unsigned char)(f % FILM_GBOY_PALETTES), (unsigned char)f, 7, 3, 2, 1, (unsigned char)res};
        film_gboy_geom g;
        film_gboy_levels lv = {0, 255}, target;
        film_gboy_geometry(f, res, &g);
        film_gboy_frame(f, res, canvas, 0, 0);
        film_gboy_sample_live(view, 720, 480, 1080, g.ww, g.wh, luma);
        film_gboy_levels_measure(luma, g.ww * g.wh, &target);
        for (i = 0; i < 40; i++) film_gboy_levels_follow(&lv, &target);
        assert(lv.lo == target.lo && lv.hi == target.hi);                 /* the exposure settles */
        film_gboy_picture(luma, work, &s, &g, &lv, f + 1, canvas);
        memset(overlay, 0x11, 960 * 270);
        film_gboy_live_paint(canvas, &g, index, overlay, 960, 720, 240);
        for (r = 0; r < 270; r++)
            for (i = 0; i < 960; i++) {
                unsigned char v = overlay[r * 960 + i];
                if (r < 240 && i < 720) assert(v == 0xB6 || v == 0xBD || v == 0xBE || v == 0xBF);
                else assert(v == 0x11);                                    /* nothing outside the screen */
            }
        /* Whole screen pixels: each pair of bytes is one colour, so the
         * swung-round screen's mirror keeps them intact. */
        for (r = 0; r < 240; r++) for (i = 0; i < 720; i += 2) assert(overlay[r * 960 + i] == overlay[r * 960 + i + 1]);
        /* The whole canvas shows: two screen rows per canvas row at 160 x
         * 120, one at 320 x 240 (each canvas row's colours, in order). */
        if (!res) for (r = 0; r < 240; r += 2) assert(!memcmp(overlay + r * 960, overlay + (r + 1) * 960, 720));
        for (r = 0; r < 240; r++) {
            const unsigned char *c = canvas + (r * g.ch / 240) * g.cw;
            assert(overlay[r * 960] == index[c[0] & 3] && overlay[r * 960 + 719] == index[c[g.cw - 1] & 3]);
        }
    }
    free(view); free(overlay);
}

int main(void)
{
    unsigned p, k;
    test_thresholds();
    test_frames();
    /* The Viewfinder frame's readout: Canon's third-stop names. */
    {
        static const struct { short tv, av, iso, ev; const char *t, *a, *i; } cases[] = {
            {672, 576, 400, 0, "1/125", "F8.0", "ISO400"}, {1152, 288, 80, 0, "1/4000", "F2.8", "ISO80"},
            {-384, 352, 3200, 0, "15\"", "F3.5", "ISO3200"}, {-128, 448, 1600, 0, "2\"5", "F5.0", "ISO1600"},
            {0, 512, 0, 0, "1\"", "F6.3", "AUTO"}, {683, 570, 210, 0, "1/125", "F8.0", "ISO200"},
            {2000, -50, 9999, 0, "1/4000", "F1.0", "ISO10000"}, {-999, 2000, 101, 0, "30\"", "F22", "ISO100"}, {FILM_GBOY_TV_UNKNOWN, 288, 0, 0, "--", "F2.8", "AUTO"}};
        unsigned i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            film_gboy_exposure e = {cases[i].tv, cases[i].av, cases[i].iso, cases[i].ev};
            char tv[8], av[6], iso[9];
            film_gboy_exposure_text(&e, tv, av, iso);
            assert(!strcmp(tv, cases[i].t) && !strcmp(av, cases[i].a) && !strcmp(iso, cases[i].i));
        }
    }
    /* The Viewfinder frame prints it (the default is 1/125 F8.0 ISO400 at
     * 0 EV); the other frames ignore it. */
    {
        static unsigned char a[FILM_GBOY_CANVAS_BYTES], b[FILM_GBOY_CANVAS_BYTES];
        film_gboy_exposure std = {672, 576, 400, 0}, other = {1152, 288, 3200, -96};
        film_gboy_frame(FILM_GBOY_FRAME_VIEWFINDER, 0, a, 1, 0);
        film_gboy_frame(FILM_GBOY_FRAME_VIEWFINDER, 0, b, 1, &std);
        assert(!memcmp(a, b, 160 * 120));
        film_gboy_frame(FILM_GBOY_FRAME_VIEWFINDER, 0, b, 1, &other);
        assert(memcmp(a, b, 160 * 120));
        film_gboy_frame(1, 0, a, 1, &std); film_gboy_frame(1, 0, b, 1, &other);
        assert(!memcmp(a, b, 160 * 120));
    }
    for (p = 0; p < FILM_GBOY_PALETTES; p++) {
        int y0, u0, v0, y3, u3, v3;
        film_gboy_yuv(p, 0, &y0, &u0, &v0); film_gboy_yuv(p, 3, &y3, &u3, &v3);
        assert(y0 > y3 + 60);                                            /* lightest to darkest */
        for (k = 1; k < 4; k++) { int a, b, c, d, e, f; film_gboy_yuv(p, k - 1, &a, &b, &c); film_gboy_yuv(p, k, &d, &e, &f); assert(a > d); }
    }
    /* L, M1, M2 and S, framed and not. */
    test_shot(3648, 2736, 1, 0);
    test_shot(3648, 2736, 0, 0);
    test_shot(2816, 2112, 6, 0);
    test_shot(1600, 1200, 11, 0);
    test_shot(640, 480, 4, 0);
    test_shot(3648, 2736, 1, 1);
    test_shot(3648, 2736, 0, 1);
    test_shot(2816, 2112, 3, 1);
    test_shot(1600, 1200, 9, 1);
    test_shot(640, 480, 10, 1);
    test_live();
    /* The calibration chart: every patch flat at its levels, all 216 different. */
    {
        unsigned w = 3648, h = 2736, i;
        unsigned char *f = malloc((size_t)w * h * 2), *row = malloc(w * 2);
        assert(film_gboy_paint_chart(f, w, h, w * 2, row) == 0);
        for (i = 0; i < FILM_GBOY_CHART_PATCHES; i++) {
            unsigned c = i % FILM_GBOY_CHART_COLS, r = i / FILM_GBOY_CHART_COLS, j;
            unsigned x = (2 * c + 1) * w / (2 * FILM_GBOY_CHART_COLS) & ~1u, y = (2 * r + 1) * h / (2 * FILM_GBOY_CHART_ROWS);
            const unsigned char *q = f + (size_t)y * w * 2 + 2 * x;
            int yy, uu, vv;
            film_gboy_chart_level(i, &yy, &uu, &vv);
            assert(q[1] == yy && q[3] == yy && (signed char)q[0] == uu && (signed char)q[2] == vv);
            for (j = 0; j < i; j++) { int a, b, c2; film_gboy_chart_level(j, &a, &b, &c2); assert(a != yy || b != uu || c2 != vv); }
        }
        free(f); free(row);
    }
    puts("ZrnoBoy kernels: thresholds, frames, shots at L/M1/M2/S and the live overlay passed");
    return 0;
}
