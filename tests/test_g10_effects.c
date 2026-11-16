/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The G10 stage's effect calls (camera/g10_magus.c effects()) on the host, on
 * a full L frame (4416 x 3312, 8832 bytes a row) with bright highlights:
 *   - every level of halation (red and amber tint), Bloom and grain returns 0;
 *   - nothing outside the frame or the workspaces is written (guard bytes);
 *   - the fused Bloom + grain pass gives the same bytes as Bloom, then grain;
 *   - halation and Bloom change pixels near the highlights, not far from them;
 *   - a black-and-white look keeps every pixel's colour (U, V) under halation.
 * Build (host): cc -DMAGUS_G10 -DFILM_LAB_CAMERA -O2 tests/test_g10_effects.c
 *   camera/film_colour.c camera/film_retro.c camera/film_toy.c camera/film_print.c
 *   camera/film_bloom.c camera/film_grain.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../camera/film_colour.h"
#include "../camera/film_bloom.h"
#include "../camera/film_grain.h"

#define W 4416u
#define H 3312u
#define S 8832u
#define G 4096u
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); puts(""); fails++; } } while (0)

static unsigned char *guarded(size_t n) { unsigned char *p = malloc(n + 2 * G); memset(p, 0xA5, n + 2 * G); return p + G; }
static int intact(const unsigned char *p, size_t n)
{
    for (unsigned i = 0; i < G; i++) if (p[-1 - (int)i] != 0xA5 || p[n + i] != 0xA5) return 0;
    return 1;
}
/* A dim scene with a few bright lamps. */
static void scene(unsigned char *f)
{
    for (unsigned y = 0; y < H; y++)
        for (unsigned x = 0; x < W; x += 2) {
            unsigned char *p = f + (size_t)y * S + x * 2;
            unsigned yy = 40 + (x * 7 + y * 3) % 40;
            int lamp = ((x / 600) % 2 == 0) && ((y / 500) % 2 == 0) && (x % 600) < 40 && (y % 500) < 40;
            p[0] = (unsigned char)(lamp ? 0 : (x / 8) & 31); p[2] = (unsigned char)(lamp ? 0 : 0xf0);
            p[1] = p[3] = (unsigned char)(lamp ? 250 : yy);
        }
}
static void noop(unsigned a, unsigned b) { (void)a; (void)b; }

int main(void)
{
    size_t bytes = (size_t)S * H, hs = film_halo_work_size(W), bs = film_bloom_work_size(W);
    unsigned char *f = guarded(bytes), *a = guarded(bytes), *b = guarded(bytes), *orig = malloc(bytes);
    unsigned char *halo = guarded(hs), *bloom = guarded(bs), *row = guarded(S), *fade = malloc(film_bloom_fade_table_size());
    static unsigned char gw[(W / 2 + 1) * 2];
    film_halo_tint red = { 256, 51, 0, 0 }, amber = { 256, 128, 0, 0 };
    clock_t t;
    CHECK(hs && bs, "workspace sizes at 4416: halo %zu bloom %zu", hs, bs);
    printf("workspaces: halation %zu, Bloom %zu, row %u bytes\n", hs, bs, S);
    scene(orig);
    film_colour_rows_in_place(1);

    for (int coarse = 0; coarse < 2; coarse++)
    for (unsigned level = 1; level <= 3; level++)
        for (int k = 0; k < 2; k++) {
            film_halo_coarse(coarse);
            memcpy(f, orig, bytes);
            t = clock();
            int rc = film_uyvy_graded_retro(f, bytes, W, H, S, 0, level, k ? &amber : &red, halo, hs, row, S, noop,
                                            0, 0, 1234, 0, 0, 0, 0);
            CHECK(!rc, "halation level %u tint %d rc %d", level, k, rc);
            CHECK(intact(f, bytes) && intact(halo, hs) && intact(row, S), "halation level %u wrote outside", level);
            /* near a lamp vs far from every lamp */
            size_t near = (size_t)60 * S + 2 * 60, far = (size_t)250 * S + 2 * 300;
            CHECK(memcmp(f + near, orig + near, 4) != 0, "halation level %u: no glow next to a lamp", level);
            CHECK(memcmp(f + far, orig + far, 4) == 0, "halation level %u: changed a pixel far from the lamps", level);
            if (k == 0) printf("halation %u: %.2f s host\n", level, (double)(clock() - t) / CLOCKS_PER_SEC);
        }
    /* black-and-white look: colour kept (both masks) */
    for (int coarse = 0; coarse < 2; coarse++) {
        film_halo_coarse(coarse);
        memcpy(f, orig, bytes);
        CHECK(!film_uyvy_graded_retro(f, bytes, W, H, S, 1, 3, &red, halo, hs, row, S, noop, 0, 0, 1, 0, 0, 0, 0), "mono rc");
        { size_t i, changed = 0; for (i = 0; i < bytes; i += 2) changed += f[i] != orig[i]; CHECK(!changed, "mono halation (coarse %d) changed %zu U/V bytes", coarse, changed); }
        CHECK(intact(f, bytes) && intact(halo, hs) && intact(row, S), "mono coarse %d wrote outside", coarse);
    }
    film_halo_coarse(0);

    for (int with_fade = 0; with_fade < 4; with_fade++) {    /* bit 0: fade table, bit 1: fast Bloom (the G10's) */
        film_bloom_fade_table(with_fade & 1 ? fade : 0);
        film_bloom_fast(with_fade >> 1);
        for (unsigned bl = 1; bl <= 3; bl++)
            for (unsigned gr = 0; gr <= 3; gr++) {
                memcpy(a, orig, bytes); memcpy(b, orig, bytes);
                int rc = film_uyvy_bloom(a, bytes, W, H, S, bl, bloom, bs, noop);
                if (gr) rc |= film_uyvy_grain(a, bytes, W, H, S, gr, 99, gw, sizeof gw, noop);
                CHECK(!rc, "bloom %u grain %u rc", bl, gr);
                if (gr) {
                    film_grain_rows g;
                    t = clock();
                    rc = film_grain_begin_rows(&g, W, H, gr, 99, gw, sizeof gw);
                    film_bloom_row_pass pass = { row, S, film_grain_finish_row, &g };
                    rc |= film_uyvy_bloom_rows(b, bytes, W, H, S, bl, bloom, bs, &pass, noop);
                    CHECK(!rc, "fused bloom %u grain %u rc", bl, gr);
                    CHECK(!memcmp(a, b, bytes), "fused bloom %u + grain %u differs from Bloom then grain", bl, gr);
                    if (bl == 3 && gr == 3 && with_fade == 3) printf("fused Bloom High + grain High: %.2f s host\n", (double)(clock() - t) / CLOCKS_PER_SEC);
                }
                CHECK(intact(a, bytes) && intact(b, bytes) && intact(bloom, bs) && intact(row, S), "bloom %u wrote outside", bl);
                if (!gr) {
                    size_t near = (size_t)44 * S + 2 * 44     /* 4 px from the lamp: inside Low's radius (8) */, far = (size_t)250 * S + 2 * 300;
                    CHECK(memcmp(a + near, orig + near, 4) != 0, "bloom %u: no glow next to a lamp", bl);
                    CHECK(memcmp(a + far, orig + far, 4) == 0, "bloom %u: changed a pixel far from the lamps", bl);
                }
            }
    }
    puts(fails ? "FAILED" : "ALL OK");
    return fails != 0;
}
