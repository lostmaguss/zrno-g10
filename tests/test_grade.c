/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Grade colour maths (camera/film_grade.c): the neutral grade is Canon's
 * own "My Colors Off" table, each row moves colours the way its name says
 * and leaves the rest alone, the tone curve never reverses, and every
 * setting packs into a valid hardware table. With a ROM dump as argument,
 * the neutral table is also checked against Canon's identity (0xffbcb7c0). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "film_grade.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static void unpack(unsigned w, int *y, int *u, int *v)
{
    *y = (int)(w >> 20 & 0x3ff);
    *u = (int)(w >> 10 & 0x3ff); if (*u >= 512) *u -= 1024;
    *v = (int)(w & 0x3ff); if (*v >= 512) *v -= 1024;
}
static unsigned node(const unsigned *t, int r, int g, int b) { return t[r + 10 * g + 100 * b]; }
static int Y(const unsigned *t, int r, int g, int b) { int y, u, v; unpack(node(t, r, g, b), &y, &u, &v); return y; }
static void yuv(const unsigned *t, int r, int g, int b, int *y, int *u, int *v) { unpack(node(t, r, g, b), y, u, v); }
/* Hue angle (degrees) and chroma of a node, from its U/V. */
static double chroma(const unsigned *t, int r, int g, int b) { int y, u, v; yuv(t, r, g, b, &y, &u, &v); return hypot(u, v); }
static double angle(const unsigned *t, int r, int g, int b) { int y, u, v; yuv(t, r, g, b, &y, &u, &v); return atan2(v, u) * 180 / M_PI; }
static double turn(double a, double b) { double d = b - a; while (d > 180) d -= 360; while (d < -180) d += 360; return d; }

static film_grade_settings with(int field, int value)
{
    film_grade_settings s;
    film_grade_neutral(&s);
    s.v[field] = (signed char)value;
    return s;
}
static void table(const film_grade_settings *s, unsigned *t) { memset(t, 0xff, 4000); film_grade_table(s, t); }

static void check_format(const unsigned *t, const char *what)
{
    int r, g, b;
    for (r = 0; r < 1000; r++) CHECK(!(t[r] >> 30), "%s: word %d has the top bits set", what, r);
    for (b = 0; b < 10; b++)
        for (g = 0; g < 10; g++)
            for (r = 0; r < 10; r++)
                if (r == 9 || g == 9 || b == 9)
                    CHECK(node(t, r, g, b) == node(t, r < 9 ? r : 8, g < 9 ? g : 8, b < 9 ? b : 8), "%s: node 9 is not node 8", what);
}
static int grey_monotonic_within(const unsigned *t, int slack)
{
    int k;
    for (k = 1; k < 9; k++) if (Y(t, k, k, k) < Y(t, k - 1, k - 1, k - 1) - slack) return 0;
    return 1;
}
static int grey_monotonic(const unsigned *t) { return grey_monotonic_within(t, 0); }

int main(int argc, char **argv)
{
    static unsigned t[1000], n[1000];
    film_grade_settings s;
    int r, g, b, k, y, u, v, worst = 0;

    /* Neutral: BT.601 x2 on the 9-bit scale, node n at 32n. */
    film_grade_neutral(&s);
    CHECK(film_grade_valid(&s) && film_grade_is_neutral(&s), "neutral settings");
    table(&s, n);
    check_format(n, "neutral");
    for (b = 0; b < 9; b++)
        for (g = 0; g < 9; g++)
            for (r = 0; r < 9; r++) {
                double R = 64 * r, G = 64 * g, B = 64 * b;
                yuv(n, r, g, b, &y, &u, &v);
                int e = (int)fmax(fabs(y - (.299 * R + .587 * G + .114 * B)),
                                  fmax(fabs(u - (-.168736 * R - .331264 * G + .5 * B)), fabs(v - (.5 * R - .418688 * G - .081312 * B))) + 0.999);
                if (e > worst) worst = e;
            }
    CHECK(worst <= 1, "neutral is %d steps off BT.601", worst);
    for (k = 0; k < 9; k++) CHECK(n[111 * k] == (unsigned)(64 * k) << 20, "grey node %d: %08x", k, n[111 * k]);
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        static unsigned canon[1000];
        CHECK(f && !fseek(f, 0xffbcb7c0 - 0xff810000, SEEK_SET) && fread(canon, 4, 1000, f) == 1000, "ROM %s", argv[1]);
        if (f) fclose(f);
        worst = 0;
        for (b = 0; b < 9; b++) for (g = 0; g < 9; g++) for (r = 0; r < 9; r++) {
            int y2, u2, v2;
            yuv(n, r, g, b, &y, &u, &v); unpack(canon[r + 10 * g + 100 * b], &y2, &u2, &v2);
            int e = abs(y - y2) > abs(u - u2) ? abs(y - y2) : abs(u - u2);
            if (abs(v - v2) > e) e = abs(v - v2);
            if (e > worst) worst = e;
        }
        CHECK(worst <= 2, "neutral is %d steps off Canon's identity", worst);
        printf("neutral grade vs Canon's identity: within %d step(s) of 1024\n", worst);
    }

    /* Every value of every field: a valid table. Tone rows: grey stays in
     * order. Each value apart from 0 changes the table. */
    for (k = 0; k < FILM_GRADE_FIELDS; k++)
        for (v = film_grade_min(k); v <= film_grade_max(k); v++) {
            char what[40];
            s = with(k, v);
            CHECK(film_grade_valid(&s), "field %d value %d invalid", k, v);
            table(&s, t);
            snprintf(what, sizeof(what), "field %d value %d", k, v);
            check_format(t, what);
            CHECK(grey_monotonic(t), "%s: grey axis reverses", what);
            if (k != FILM_GRADE_SHADOW_AMOUNT && k != FILM_GRADE_HIGHLIGHT_AMOUNT && v)
                CHECK(memcmp(t, n, sizeof(t)), "%s changes nothing", what);
        }
    s = with(FILM_GRADE_CONTRAST, 6);
    CHECK(!film_grade_valid(&s), "contrast 6 accepted");

    /* Contrast: darker shadows, brighter highlights, mid grey stays. */
    table((s = with(FILM_GRADE_CONTRAST, 5), &s), t);
    CHECK(Y(t, 2, 2, 2) < Y(n, 2, 2, 2) && Y(t, 6, 6, 6) > Y(n, 6, 6, 6) && Y(t, 4, 4, 4) == Y(n, 4, 4, 4), "contrast +5");
    CHECK(Y(t, 0, 0, 0) == 0 && Y(t, 8, 8, 8) == 512, "contrast moves black or white");
    table((s = with(FILM_GRADE_CONTRAST, -5), &s), t);
    CHECK(Y(t, 2, 2, 2) > Y(n, 2, 2, 2) && Y(t, 6, 6, 6) < Y(n, 6, 6, 6), "contrast -5");
    /* Midtones, shadows, highlights: each where its name says. */
    table((s = with(FILM_GRADE_MIDTONES, 5), &s), t);
    CHECK(Y(t, 4, 4, 4) - Y(n, 4, 4, 4) >= 60 && Y(t, 8, 8, 8) == 512 && Y(t, 0, 0, 0) == 0, "midtones +5: %d", Y(t, 4, 4, 4) - Y(n, 4, 4, 4));
    table((s = with(FILM_GRADE_SHADOWS, 5), &s), t);
    CHECK(Y(t, 3, 3, 3) - Y(n, 3, 3, 3) > Y(t, 6, 6, 6) - Y(n, 6, 6, 6) && Y(t, 3, 3, 3) > Y(n, 3, 3, 3), "shadows +5");
    table((s = with(FILM_GRADE_HIGHLIGHTS, -5), &s), t);
    CHECK(Y(n, 6, 6, 6) - Y(t, 6, 6, 6) > Y(n, 2, 2, 2) - Y(t, 2, 2, 2) && Y(t, 6, 6, 6) < Y(n, 6, 6, 6), "highlights -5");
    /* Fade: black lifted 38 levels at 5, white stays. */
    table((s = with(FILM_GRADE_FADE, 5), &s), t);
    CHECK(Y(t, 0, 0, 0) >= 70 && Y(t, 0, 0, 0) <= 80 && Y(t, 8, 8, 8) == 512, "fade 5: black %d", Y(t, 0, 0, 0));
    /* Blacks and whites: the ends move, the middle does not. Blacks -5
     * darkens node 1 (32 levels) to about a fifth and keeps black; +5 lifts
     * black 30 levels (60 on the 9-bit scale) and leaves node 2 (64) as it
     * was. Whites -5 brings white down 30 levels; +5 brightens node 7
     * (224) and keeps white. Mid grey and the far end stay put. */
    table((s = with(FILM_GRADE_BLACKS, -5), &s), t);
    CHECK(Y(t, 0, 0, 0) == 0 && Y(t, 1, 1, 1) < Y(n, 1, 1, 1) / 3 && Y(t, 2, 2, 2) == Y(n, 2, 2, 2) && Y(t, 8, 8, 8) == 512,
          "blacks -5: node 1 %d", Y(t, 1, 1, 1));
    table((s = with(FILM_GRADE_BLACKS, 5), &s), t);
    CHECK(Y(t, 0, 0, 0) >= 56 && Y(t, 0, 0, 0) <= 64 && Y(t, 1, 1, 1) > Y(n, 1, 1, 1) && Y(t, 2, 2, 2) == Y(n, 2, 2, 2), "blacks +5: black %d", Y(t, 0, 0, 0));
    table((s = with(FILM_GRADE_WHITES, -5), &s), t);
    CHECK(Y(t, 8, 8, 8) >= 448 && Y(t, 8, 8, 8) <= 456 && Y(t, 6, 6, 6) == Y(n, 6, 6, 6) && Y(t, 0, 0, 0) == 0, "whites -5: white %d", Y(t, 8, 8, 8));
    table((s = with(FILM_GRADE_WHITES, 5), &s), t);
    CHECK(Y(t, 8, 8, 8) == 512 && Y(t, 7, 7, 7) > Y(n, 7, 7, 7) + 20 && Y(t, 6, 6, 6) == Y(n, 6, 6, 6), "whites +5: node 7 %d", Y(t, 7, 7, 7));
    /* Extremes together still keep grey in order. */
    film_grade_neutral(&s);
    s.v[FILM_GRADE_CONTRAST] = 5; s.v[FILM_GRADE_SHADOWS] = -5; s.v[FILM_GRADE_HIGHLIGHTS] = 5; s.v[FILM_GRADE_MIDTONES] = -5;
    table(&s, t);
    CHECK(grey_monotonic(t), "extreme tone reverses");

    /* Temperature: light grey warmer (U down, V up) at its own luma,
     * black and white untouched (the shift stays in the gamut); tint +5
     * magenta (U, V up). */
    table((s = with(FILM_GRADE_TEMPERATURE, 5), &s), t);
    yuv(t, 6, 6, 6, &y, &u, &v);
    CHECK(u < -10 && v > 5 && abs(y - Y(n, 6, 6, 6)) <= 1, "temperature +5: light grey Y %d U %d V %d", y, u, v);
    CHECK(node(t, 0, 0, 0) == 0 && node(t, 8, 8, 8) == node(n, 8, 8, 8), "temperature moves black or white");
    table((s = with(FILM_GRADE_TEMPERATURE, -5), &s), t);
    yuv(t, 6, 6, 6, &y, &u, &v);
    CHECK(u > 5 && v < -5, "temperature -5: U %d V %d", u, v);
    table((s = with(FILM_GRADE_TINT, 5), &s), t);
    yuv(t, 6, 6, 6, &y, &u, &v);
    CHECK(u > 5 && v > 5, "tint +5: U %d V %d", u, v);

    /* Saturation -5: black and white. */
    table((s = with(FILM_GRADE_SATURATION, -5), &s), t);
    for (k = 0; k < 729; k++) { int w = (k % 9) + 10 * (k / 9 % 9) + 100 * (k / 81); unpack(t[w], &y, &u, &v); CHECK(abs(u) <= 1 && abs(v) <= 1, "saturation -5 node %d: %d %d", k, u, v); }
    table((s = with(FILM_GRADE_SATURATION, 5), &s), t);
    CHECK(chroma(t, 5, 3, 3) > 1.3 * chroma(n, 5, 3, 3), "saturation +5");
    /* Vibrance: muted colours gain more than saturated ones. */
    table((s = with(FILM_GRADE_VIBRANCE, 5), &s), t);
    CHECK(chroma(t, 5, 4, 4) / chroma(n, 5, 4, 4) > chroma(t, 8, 0, 0) / chroma(n, 8, 0, 0) + 0.2, "vibrance +5");
    CHECK(Y(t, 4, 4, 4) == Y(n, 4, 4, 4), "vibrance moves grey");

    /* HSL: red's hue +5 turns red toward orange (about 30 degrees) and
     * leaves blue and grey alone; green's saturation -5 greys green only;
     * blue's lightness -5 darkens blue only. */
    table((s = with(FILM_GRADE_HSL_HUE + 0, 5), &s), t);
    CHECK(turn(angle(n, 8, 0, 0), angle(t, 8, 0, 0)) > 20 && turn(angle(n, 8, 0, 0), angle(t, 8, 0, 0)) < 40, "red hue +5 moves %.1f degrees", turn(angle(n, 8, 0, 0), angle(t, 8, 0, 0)));
    CHECK(node(t, 0, 0, 8) == node(n, 0, 0, 8) && node(t, 4, 4, 4) == node(n, 4, 4, 4), "red hue moves blue or grey");
    table((s = with(FILM_GRADE_HSL_SAT + 3, -5), &s), t);
    CHECK(chroma(t, 0, 8, 0) < 2 && node(t, 8, 0, 0) == node(n, 8, 0, 0) && node(t, 0, 0, 8) == node(n, 0, 0, 8), "green saturation -5: %.1f", chroma(t, 0, 8, 0));
    table((s = with(FILM_GRADE_HSL_LIGHT + 5, -5), &s), t);
    CHECK(Y(t, 0, 0, 8) < Y(n, 0, 0, 8) * 0.7 && node(t, 8, 0, 0) == node(n, 8, 0, 0) && node(t, 4, 4, 4) == node(n, 4, 4, 4), "blue lightness -5");
    table((s = with(FILM_GRADE_HSL_LIGHT + 1, 5), &s), t);
    CHECK(Y(t, 8, 4, 0) > Y(n, 8, 4, 0), "orange lightness +5");

    /* Split toning: aqua shadows (+U, -V near black), orange highlights
     * (-U, +V near white), the other end nearly untouched. */
    film_grade_neutral(&s);
    s.v[FILM_GRADE_SHADOW_HUE] = 5; s.v[FILM_GRADE_SHADOW_AMOUNT] = 5;
    s.v[FILM_GRADE_HIGHLIGHT_HUE] = 2; s.v[FILM_GRADE_HIGHLIGHT_AMOUNT] = 5;
    table(&s, t);
    yuv(t, 1, 1, 1, &y, &u, &v);
    CHECK(u > 5 && v < -15, "aqua shadows: U %d V %d", u, v);
    yuv(t, 8, 8, 8, &y, &u, &v);
    CHECK(u < -15 && v > 15, "orange highlights: U %d V %d", u, v);

    /* Black and white: every node grey; no filter keeps the luma; a red
     * filter darkens blue and lightens red against no filter, a blue
     * filter the reverse, and green lightens green; the HSL lightness rows
     * act on the grey; split toning still tints it. */
    {
        static unsigned plain[1000];
        table((s = with(FILM_GRADE_MONO, 1), &s), plain);
        for (k = 0; k < 729; k++) { int w = (k % 9) + 10 * (k / 9 % 9) + 100 * (k / 81); unpack(plain[w], &y, &u, &v); CHECK(!u && !v, "no filter node %d: %d %d", k, u, v); }
        for (k = 0; k < 729; k++) { int w = (k % 9) + 10 * (k / 9 % 9) + 100 * (k / 81); CHECK(abs(Y(plain, w % 10, w / 10 % 10, w / 100) - Y(n, w % 10, w / 10 % 10, w / 100)) <= 1, "no filter moves luma at %d", w); }
        table((s = with(FILM_GRADE_MONO, 4), &s), t);
        CHECK(Y(t, 2, 4, 8) < Y(plain, 2, 4, 8) - 50 && Y(t, 8, 3, 2) > Y(plain, 8, 3, 2) + 50 && Y(t, 4, 4, 4) == Y(plain, 4, 4, 4),
              "red filter: sky %d / %d, red %d / %d", Y(t, 2, 4, 8), Y(plain, 2, 4, 8), Y(t, 8, 3, 2), Y(plain, 8, 3, 2));
        table((s = with(FILM_GRADE_MONO, 6), &s), t);
        CHECK(Y(t, 2, 4, 8) > Y(plain, 2, 4, 8) + 50 && Y(t, 8, 3, 2) < Y(plain, 8, 3, 2) - 50, "blue filter");
        table((s = with(FILM_GRADE_MONO, 5), &s), t);
        CHECK(Y(t, 2, 8, 2) > Y(plain, 2, 8, 2) + 30 && Y(t, 8, 2, 2) < Y(plain, 8, 2, 2) - 30, "green filter");
        table((s = with(FILM_GRADE_MONO, 2), &s), t);
        CHECK(Y(t, 2, 4, 8) < Y(plain, 2, 4, 8) && Y(t, 2, 4, 8) > Y(plain, 2, 4, 8) - 40, "yellow filter: sky %d / %d", Y(t, 2, 4, 8), Y(plain, 2, 4, 8));
        s = with(FILM_GRADE_MONO, 1); s.v[FILM_GRADE_HSL_LIGHT + 5] = -5;      /* blue darker in the grey */
        table(&s, t);
        CHECK(Y(t, 0, 0, 8) < Y(plain, 0, 0, 8) && Y(t, 8, 0, 0) == Y(plain, 8, 0, 0) && !(node(t, 0, 0, 8) & 0xfffff), "mono blue lightness -5");
        s = with(FILM_GRADE_MONO, 1); s.v[FILM_GRADE_SATURATION] = 5; s.v[FILM_GRADE_VIBRANCE] = -5;
        table(&s, t);
        CHECK(!memcmp(t, plain, sizeof(t)), "saturation or vibrance changes black and white");
        s = with(FILM_GRADE_MONO, 1); s.v[FILM_GRADE_HIGHLIGHT_HUE] = 2; s.v[FILM_GRADE_HIGHLIGHT_AMOUNT] = 5;
        table(&s, t);
        yuv(t, 8, 8, 8, &y, &u, &v);
        CHECK(u < -15 && v > 15, "toned black and white: U %d V %d", u, v);
        s = with(FILM_GRADE_MONO, 7);
        CHECK(!film_grade_valid(&s), "mono 7 accepted");
    }

    /* Random grades: valid, and grey in order but for 5 steps (of 1024,
     * 2.5 levels; 3 of 2000 grades reverse at all): the curve is
     * monotonic, but a white balance tints grey and a colour's lightness
     * then moves that tinted grey too. */
    srand(12);
    for (k = 0; k < 2000; k++) {
        int i;
        for (i = 0; i < FILM_GRADE_FIELDS; i++) s.v[i] = (signed char)(film_grade_min(i) + rand() % (film_grade_max(i) - film_grade_min(i) + 1));
        table(&s, t);
        check_format(t, "random");
        CHECK(grey_monotonic_within(t, 5), "random grade %d: grey reverses", k);
    }
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("Grade: neutral is BT.601 / Canon's table, every row moves its colours, %d fields x every value valid\n", FILM_GRADE_FIELDS);
    return 0;
}
