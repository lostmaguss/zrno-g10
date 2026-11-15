/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Print kernels (camera/film_riso.c) with card/CHDK/FILM/PRINT.BIN:
 * the table file's checks, exact ink colours in every image size and
 * setting, tone order, noise that keeps the tone, seeds, bad arguments and
 * the live view. Run under AddressSanitizer: the drums' shifts and turns
 * read near the frame's edges. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "film_riso.h"

static unsigned char *data;
static size_t data_size;

static void load(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    data = malloc(FILM_RISO_DATA_BYTES + 1);
    data_size = fread(data, 1, FILM_RISO_DATA_BYTES + 1, f);
    fclose(f);
}

static void test_data(void)
{
    unsigned char *copy = malloc(data_size);
    assert(film_riso_data_check(data, data_size) == 0);
    assert(film_riso_data_check(data, data_size - 1) != 0);
    memcpy(copy, data, data_size); copy[3] ^= 1;
    assert(film_riso_data_check(copy, data_size) != 0);
    memcpy(copy, data, data_size); copy[12] ^= 4;                 /* set size */
    assert(film_riso_data_check(copy, data_size) != 0);
    free(copy);
    static const unsigned char inks[FILM_RISO_ALL_SETS] = {2, 2, 2, 2, 2, 2, 3,   3, 3, 3,  1, 1, 2,  1, 2,  1, 1,  3, 3,  0, 0, 0};
    for (unsigned i = 0; i < FILM_RISO_ALL_SETS; i++) assert(film_riso_ink_count(data, i) == inks[i]);
    {   /* Style and palette to table set: the Riso sets first, then each style's in order. */
        film_riso_settings s = {3, 0, 0, 0, 0, 0, 0, 0, 0};
        unsigned st, v, want = FILM_RISO_SETS;
        assert(film_riso_set(&s) == 3);
        for (st = 1; st < FILM_RISO_STYLES; st++)
            for (v = 0; v < film_riso_variants(st); v++) { s.style = (unsigned char)st; s.variant = (unsigned char)v; assert(film_riso_set(&s) == want++); }
        assert(want == FILM_RISO_ALL_SETS);
    }
}

/* A test frame: a luma ramp across, chroma varying down and across, a dark
 * strip at the top and a light one at the bottom (fixing the levels), and
 * an optional flat band of luma `band` in the middle third. */
static void make_frame(unsigned char *f, unsigned w, unsigned h, int band)
{
    unsigned x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x += 2) {
            unsigned char *p = f + (size_t)y * 2 * w + 2 * x;
            int Y = (int)(x * 255 / w), U = (int)((y * 7) % 200) - 100, V = (int)((x + y) % 160) - 80;
            if (y < h / 8) { Y = 16; U = V = 0; }
            else if (y >= h - h / 8) { Y = 235; U = V = 0; }
            else if (band >= 0 && y >= h / 3 && y < 2 * h / 3) { Y = band; U = V = 0; }
            p[0] = (unsigned char)U; p[1] = (unsigned char)Y; p[2] = (unsigned char)V; p[3] = (unsigned char)Y;
        }
}

/* The UYVY words the kernel may write: for every two ink masks a, b, the
 * pair's colours with their chroma averaged. */
static unsigned allowed(const film_riso_settings *s, unsigned *words)
{
    unsigned n = film_riso_ink_count(data, film_riso_set(s)), a, b, c = 0;
    for (a = 0; a < (1u << n); a++)
        for (b = 0; b < (1u << n); b++) {
            int ya, ua, va, yb, ub, vb;
            film_riso_colour(data, s, a, &ya, &ua, &va);
            film_riso_colour(data, s, b, &yb, &ub, &vb);
            words[c++] = (unsigned)((ua + ub) >> 1 & 255) | (unsigned)ya << 8 | (unsigned)((va + vb) >> 1 & 255) << 16 | (unsigned)yb << 24;
        }
    return c;
}

static int print(unsigned char *f, unsigned w, unsigned h, const film_riso_settings *s, unsigned seed)
{
    size_t bytes = film_riso_work_size(w);
    void *work = malloc(bytes);
    int rc = film_riso_render(f, w, h, 2 * w, data, s, seed, work, bytes, 0);
    free(work);
    return rc;
}

static void check_colours(const unsigned char *f, unsigned w, unsigned h, const film_riso_settings *s)
{
    unsigned words[64], n, i, k;
    const unsigned *p = (const unsigned *)f;
    if (s->style == FILM_RISO_STYLE_TEXT) {
        /* Text: every pixel's luma is the background's, or a character colour's (or its scanline 7/8). */
        int ok[256] = {0}, y, u, v;
        for (k = 0; k < 8; k++) { film_riso_colour(data, s, k, &y, &u, &v); ok[y] = 1; if (k) ok[y * 7 / 8] = 1; }
        for (i = 0; i < w * h / 2; i++) assert(ok[p[i] >> 8 & 255] && ok[p[i] >> 24]);
        return;
    }
    n = allowed(s, words);
    for (i = 0; i < w * h / 2; i++) {
        for (k = 0; k < n && p[i] != words[k]; k++) ;
        if (k == n) { fprintf(stderr, "word %u = %08x not an ink colour (set %u)\n", i, p[i], s->inks); abort(); }
    }
}

/* Every set, paper and setting at S; every size with everything on. */
static void test_colours_and_sizes(void)
{
    static const unsigned sizes[4][2] = {{3648, 2736}, {2816, 2112}, {1600, 1200}, {640, 480}};
    film_riso_settings s = {0, 0, 0, 2, 2, 3, 0, 0, 0};
    unsigned char *f = malloc(3648u * 2736u * 2u);
    unsigned i;
    for (s.inks = 0; s.inks < FILM_RISO_SETS; s.inks++)
        for (s.paper = 0; s.paper < FILM_RISO_PAPERS; s.paper++)
            for (i = 0; i < 4; i++) {
                s.separation = i & 1; s.screen = i >> 1;
                make_frame(f, 640, 480, -1);
                assert(print(f, 640, 480, &s, 7 + i) == 0);
                check_colours(f, 640, 480, &s);
            }
    /* Every style's every palette, at Heavy flaws and noise. */
    s.inks = 0; s.paper = 0; s.noise = 4; s.flaws = 3;
    for (s.style = 1; s.style < FILM_RISO_STYLES; s.style++)
        for (s.variant = 0; s.variant < film_riso_variants(s.style); s.variant++) {
            make_frame(f, 640, 480, -1);
            assert(print(f, 640, 480, &s, 11) == 0);
            check_colours(f, 640, 480, &s);
            make_frame(f, 2816, 2112, -1);                         /* text: 2 x 2 glyph pixels from here up */
            assert(print(f, 2816, 2112, &s, 12) == 0);
            check_colours(f, 2816, 2112, &s);
        }
    s.style = 0; s.variant = 0;
    s.inks = 6; s.paper = 0; s.noise = 4; s.reg = 3; s.flaws = 3; s.screen = 1;
    for (i = 0; i < 4; i++) {
        make_frame(f, sizes[i][0], sizes[i][1], -1);
        assert(print(f, sizes[i][0], sizes[i][1], &s, 99) == 0);
        check_colours(f, sizes[i][0], sizes[i][1], &s);
    }
    /* Bad arguments. */
    make_frame(f, 640, 480, -1);
    assert(print(f, 642, 480, &s, 1) == -1);                  /* width not a multiple of 4 */
    assert(print(f, 640, 479, &s, 1) == -1);                  /* odd height */
    s.inks = FILM_RISO_SETS;
    assert(print(f, 640, 480, &s, 1) == -1);
    s.inks = 0; s.style = FILM_RISO_STYLES;
    assert(print(f, 640, 480, &s, 1) == -1);
    s.style = FILM_RISO_STYLE_WOODCUT; s.variant = 2;               /* woodcut has two palettes */
    assert(print(f, 640, 480, &s, 1) == -1);
    free(f);
}

/* Mean luma of the middle band after printing. */
static double band_mean(unsigned w, unsigned h, int band, const film_riso_settings *s, unsigned seed)
{
    unsigned char *f = malloc((size_t)w * h * 2);
    double sum = 0;
    unsigned x, y, n = 0;
    make_frame(f, w, h, band);
    assert(print(f, w, h, s, seed) == 0);
    for (y = h / 3 + 8; y < 2 * h / 3 - 8; y++)
        for (x = 64; x < w - 64; x++) { sum += f[(size_t)y * 2 * w + 2 * x + 1]; n++; }
    free(f);
    return sum / n;
}

static void test_tone(void)
{
    film_riso_settings s = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned sep, noise;
    for (sep = 0; sep < 2; sep++) {
        s.separation = (unsigned char)sep;
        for (noise = 0; noise < FILM_RISO_NOISES; noise += 4) {
            double d, m, l;
            s.noise = (unsigned char)noise;
            d = band_mean(1600, 1200, 60, &s, 3); m = band_mean(1600, 1200, 128, &s, 3); l = band_mean(1600, 1200, 200, &s, 3);
            printf("separation %u noise %u: band 60 -> %.1f, 128 -> %.1f, 200 -> %.1f\n", sep, noise, d, m, l);
            assert(d + 10 < m && m + 10 < l);                  /* darker stays darker */
        }
        /* The noise keeps the tone: it fades out toward paper and solid ink. */
        s.noise = 0; double a = band_mean(1600, 1200, 128, &s, 3);
        s.noise = 4; double b = band_mean(1600, 1200, 128, &s, 3);
        assert(a - b < 6 && b - a < 6);
        s.noise = 0;
    }
}

static void test_seeds(void)
{
    film_riso_settings s = {1, 0, 0, 3, 2, 3, 0, 0, 0};
    unsigned char *a = malloc(640 * 480 * 2), *b = malloc(640 * 480 * 2);
    make_frame(a, 640, 480, -1); make_frame(b, 640, 480, -1);
    assert(print(a, 640, 480, &s, 5) == 0 && print(b, 640, 480, &s, 5) == 0);
    assert(!memcmp(a, b, 640 * 480 * 2));                      /* the same shot prints the same */
    make_frame(b, 640, 480, -1);
    assert(print(b, 640, 480, &s, 6) == 0);
    assert(memcmp(a, b, 640 * 480 * 2));                       /* another shot, other noise and flaws */
    free(a); free(b);
}

/* The live masks again, one pixel and ink at a time by the rules (the
 * register shift down and right, clamped at the edges; grain tiles at
 * 128, dots at 8 and 4 with the third ink half a cell over). */
static void check_live(const unsigned char *yuv, const unsigned char *d, const film_riso_settings *s,
                       const unsigned char *sep, const unsigned char *canvas)
{
    const unsigned W = FILM_RISO_LIVE_W, H = FILM_RISO_LIVE_H;
    const unsigned short *idx = (const unsigned short *)(yuv + W * H);
    const unsigned grain = 272, ldot45 = 272 + 3 * 16384 + 256 + 64, ldot0 = ldot45 + 64;
    unsigned n = film_riso_ink_count(d, s->inks), x, y, k;
    unsigned dx[3] = {0, 1, 0}, dy[3] = {0, 1, 2};
    if (!s->reg) dx[1] = dy[1] = dy[2] = 0;
    else if (s->reg >= 2) { dx[1] = 2; dy[2] = 3; }
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned m = 0;
            for (k = 0; k < n; k++) {
                unsigned sx = x + dx[k] < W ? x + dx[k] : W - 1, sy = y + dy[k] < H ? y + dy[k] : H - 1, t;
                if (!s->screen) t = d[grain + k * 16384 + (y & 127) * 128 + (x & 127)];
                else if (k == 0) t = d[ldot45 + (y & 7) * 8 + (x & 7)];
                else t = d[ldot0 + ((y + 2 * (k == 2)) & 3) * 4 + ((x + 2 * (k == 2)) & 3)];
                if (sep[k * 8192 + idx[sy * W + sx]] > t) m |= 1u << k;
            }
            if (canvas[y * W + x] != m) { fprintf(stderr, "live mask at %u,%u: %u, rule says %u\n", x, y, canvas[y * W + x], m); abort(); }
        }
}

static void test_live(void)
{
    static unsigned char view[480 * 1080];
    static unsigned short yuvw[3 * FILM_RISO_LIVE_W * FILM_RISO_LIVE_H / 2], mirw[sizeof(yuvw) / 2];   /* halfword aligned */
    unsigned char *yuv = (unsigned char *)yuvw, *mirrored = (unsigned char *)mirw;
    static unsigned char sep[3 * 8192], canvas[FILM_RISO_LIVE_W * FILM_RISO_LIVE_H], overlay[240 * 960];
    static const unsigned char index[8] = {10, 11, 12, 13, 14, 15, 16, 17};
    film_riso_settings s = {0, 0, 0, 2, 2, 3, 0, 0, 0};
    unsigned i, x, y, set;
    int lo = 0, hi = 0;
    for (i = 0; i < sizeof(view); i++) view[i] = (unsigned char)(i * 37 + (i / 1080) * 3);
    film_riso_live_sample(view, 720, 480, 1080, FILM_RISO_LIVE_W, FILM_RISO_LIVE_H, 0, yuv);
    film_riso_live_sample(view, 720, 480, 1080, FILM_RISO_LIVE_W, FILM_RISO_LIVE_H, 1, mirrored);
    {   /* mirrored: the luma plane's and the index plane's rows reversed */
        const unsigned short *ia = (const unsigned short *)(yuv + FILM_RISO_LIVE_W * FILM_RISO_LIVE_H);
        const unsigned short *ib = (const unsigned short *)(mirrored + FILM_RISO_LIVE_W * FILM_RISO_LIVE_H);
        for (y = 0; y < FILM_RISO_LIVE_H; y++)
            for (x = 0; x < FILM_RISO_LIVE_W; x++) {
                unsigned a = y * FILM_RISO_LIVE_W + x, b = y * FILM_RISO_LIVE_W + FILM_RISO_LIVE_W - 1 - x;
                assert(yuv[a] == mirrored[b] && ia[a] == ib[b]);
                assert((ia[a] & 31) == yuv[a] >> 3);           /* the index's luma part */
            }
    }
    film_riso_live_levels(yuv, FILM_RISO_LIVE_W * FILM_RISO_LIVE_H, 1, &lo, &hi);
    assert(lo < hi);
    for (set = 0; set < FILM_RISO_SETS; set++)
        for (i = 0; i < 2; i++) {
            s.inks = (unsigned char)set; s.screen = (unsigned char)i;
            for (s.reg = 0; s.reg < FILM_RISO_REGISTERS; s.reg += 2) {
                film_riso_live_render(yuv, FILM_RISO_LIVE_W, FILM_RISO_LIVE_H, data, &s, lo, hi, sep, canvas);
                for (x = 0; x < sizeof(canvas); x++) assert(canvas[x] < (1u << film_riso_ink_count(data, set)));
                check_live(yuv, data, &s, sep, canvas);
            }
            s.reg = 2;
        }
    for (s.style = 1; s.style < FILM_RISO_STYLES; s.style++)          /* the styles: masks within their colours */
        for (s.variant = 0; s.variant < film_riso_variants(s.style); s.variant++) {
            unsigned n = film_riso_ink_count(data, film_riso_set(&s)), lim = n ? 1u << n : film_riso_live_colours(data, &s), seen = 0;
            film_riso_live_render(yuv, FILM_RISO_LIVE_W, FILM_RISO_LIVE_H, data, &s, lo, hi, sep, canvas);
            for (x = 0; x < sizeof(canvas); x++) { assert(canvas[x] < lim); seen |= 1u << canvas[x]; }
            assert(seen & 1 && seen != 1);                       /* paper (or background) and something printed */
        }
    s.style = 0; s.variant = 0;
    memset(overlay, 0xEE, sizeof(overlay));
    film_riso_live_paint(canvas, FILM_RISO_LIVE_W, FILM_RISO_LIVE_H, index, overlay, 960, 720, 240);
    for (y = 0; y < 240; y++)
        for (x = 0; x < 720; x += 2) {
            unsigned char v = overlay[y * 960 + x];
            assert(v >= 10 && v <= 17 && overlay[y * 960 + x + 1] == v);   /* whole screen pixels, all covered */
        }
    assert(overlay[960 + 720] == 0xEE);                        /* nothing past the 720 columns */
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    load(argv[1]);
    test_data();
    test_colours_and_sizes();
    test_tone();
    test_seeds();
    test_live();
    printf("Print kernels: table checks, exact ink colours (7 ink sets x 3 papers x separations x screens, and L, M1, M2, S "
           "with everything on), bad arguments, tone order and noise keeping the tone, seeds, live sampling, levels, masks "
           "and whole-pixel overlay: passed\n");
    return 0;
}
