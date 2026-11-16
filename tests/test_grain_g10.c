/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Grain on a G10 L frame (4416 x 3312, 8832 bytes a row) on the host:
 * only luma changes, U/V and the guard bytes around the frame stay, the
 * grain strength on flat grey matches the G12 width, and widths above 4416
 * are refused. Build: cc -DMAGUS_G10 -O1 tests/test_grain_g10.c camera/film_grain.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../camera/film_grain.h"

#define W 4416u
#define H 3312u
#define STRIDE 8832u
#define GUARD 4096u

static double luma_sd(const unsigned char *f, unsigned w, unsigned h, unsigned stride)
{
    double s = 0, s2 = 0; unsigned long n = 0;
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 1; x < w * 2; x += 2) { double v = f[(size_t)y * stride + x]; s += v; s2 += v * v; n++; }
    return sqrt(s2 / n - (s / n) * (s / n));
}

int main(void)
{
    size_t size = (size_t)STRIDE * H;
    unsigned char *buf = malloc(size + 2 * GUARD), *frame = buf + GUARD, *orig = malloc(size);
    static unsigned char work[(W / 2 + 1) * 2 + 16];
    int fails = 0;
    memset(buf, 0xA5, size + 2 * GUARD);
    for (size_t i = 0; i < size; i++) frame[i] = (i & 1) ? 128 : (unsigned char)(i * 7);   /* Y 128, varied U/V */
    memcpy(orig, frame, size);

    if (film_grain_work_size(W) != (W / 2 + 1) * 2) { puts("FAIL work size at 4416"); fails++; }
    if (film_grain_work_size(4418)) { puts("FAIL 4418 accepted"); fails++; }
    if (film_uyvy_grain(frame, size, W, H, STRIDE, 3, 12345, work, sizeof work - 16, 0)) { puts("FAIL grain rc"); fails++; }

    for (unsigned i = 0; i < GUARD; i++)
        if (buf[i] != 0xA5 || buf[GUARD + size + i] != 0xA5) { puts("FAIL guard bytes changed"); fails++; break; }
    unsigned long uv_changed = 0, y_changed = 0;
    for (size_t i = 0; i < size; i++) {
        if (frame[i] == orig[i]) continue;
        if (i & 1) y_changed++; else uv_changed++;
    }
    if (uv_changed) { printf("FAIL %lu U/V bytes changed\n", uv_changed); fails++; }
    double sd = luma_sd(frame, W, H, STRIDE);
    printf("G10 frame: %lu of %u luma bytes changed, luma sd on grey %.2f\n", y_changed, W * H, sd);

    /* Same strength as on the G12's width: grain is per pixel, not per frame. */
    unsigned char *g12 = malloc((size_t)7296 * 64);
    for (size_t i = 0; i < (size_t)7296 * 64; i++) g12[i] = (i & 1) ? 128 : 0;
    film_uyvy_grain(g12, (size_t)7296 * 64, 3648, 64, 7296, 3, 12345, work, sizeof work - 16, 0);
    double sd12 = luma_sd(g12, 3648, 64, 7296);
    printf("G12 width: luma sd on grey %.2f\n", sd12);
    if (fabs(sd - sd12) > 0.3) { puts("FAIL strength differs from G12"); fails++; }
    if (y_changed < W * H / 2) { puts("FAIL too few pixels grained"); fails++; }

    puts(fails ? "FAILED" : "ALL OK");
    return fails != 0;
}
