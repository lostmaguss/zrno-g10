/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Grade's colour maths (see film_grade.h). Integer
 * Q12 throughout (4096 = 1.0 = the table's node 8, 256 levels). A table is
 * 729 node evaluations, two or three divisions each: about 2 ms on the
 * camera, done once per change in SpyTask, never per pixel. */
#include "film_grade.h"

#define ONE 4096
static const char *const hue_names[FILM_GRADE_HUES] = {"Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"};
const char *film_grade_hue_name(unsigned hue) { return hue < FILM_GRADE_HUES ? hue_names[hue] : "?"; }
static const char *const mono_names[FILM_GRADE_MONOS] = {"Off", "No filter", "Yellow filter", "Orange filter", "Red filter", "Green filter", "Blue filter"};
const char *film_grade_mono_name(unsigned mono) { return mono < FILM_GRADE_MONOS ? mono_names[mono] : "?"; }
/* The grey mix of each filter (R, G, B in Q12, summing to 4096 so grey
 * stays grey): no filter is BT.601's luma; a filter passes its own colour
 * and holds back the rest, as on black-and-white film. Yellow darkens blue
 * sky a little, orange more, red to near black (and lightens skin and
 * brick); green lightens foliage and darkens red; blue lightens sky and
 * darkens skin. */
static const short mono_mix[FILM_GRADE_MONOS][3] = {
    {0, 0, 0}, {1225, 2404, 467}, {1475, 2458, 163}, {2253, 1761, 82}, {3277, 819, 0}, {737, 3195, 164}, {328, 1720, 2048}};

int film_grade_min(unsigned field)
{
    if (field == FILM_GRADE_FADE || field == FILM_GRADE_SHADOW_HUE || field == FILM_GRADE_HIGHLIGHT_HUE || field == FILM_GRADE_MONO) return 0;
    if (field == FILM_GRADE_SHADOW_AMOUNT || field == FILM_GRADE_HIGHLIGHT_AMOUNT) return 1;
    return -5;
}
int film_grade_max(unsigned field)
{
    if (field == FILM_GRADE_SHADOW_HUE || field == FILM_GRADE_HIGHLIGHT_HUE) return FILM_GRADE_HUES;
    if (field == FILM_GRADE_MONO) return FILM_GRADE_MONOS - 1;
    return 5;
}
void film_grade_neutral(film_grade_settings *s)
{
    unsigned i;
    for (i = 0; i < FILM_GRADE_FIELDS; i++) s->v[i] = 0;
    s->v[FILM_GRADE_SHADOW_AMOUNT] = s->v[FILM_GRADE_HIGHLIGHT_AMOUNT] = 3;
}
int film_grade_valid(const film_grade_settings *s)
{
    unsigned i;
    for (i = 0; i < FILM_GRADE_FIELDS; i++)
        if (s->v[i] < film_grade_min(i) || s->v[i] > film_grade_max(i)) return 0;
    return 1;
}
int film_grade_is_neutral(const film_grade_settings *s)
{
    unsigned i;
    for (i = 0; i < FILM_GRADE_FIELDS; i++)
        if (i != FILM_GRADE_SHADOW_AMOUNT && i != FILM_GRADE_HIGHLIGHT_AMOUNT && s->v[i]) return 0;
    return 1;
}

static int clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

/* ---- The prepared grade: the tone curve as 257 samples (every 16 in Q12),
 * made monotonic, and the step sizes. */
typedef struct {
    unsigned short curve[257];
    int du, dv;                       /* colour balance: chroma per unit of luma (Q12) */
    int sat;                          /* saturation factor (Q12) */
    int vib;                          /* vibrance: extra factor at zero saturation (Q12) */
    int hsl;                          /* any HSL value set */
    const short *mono;                /* black and white: the filter's grey mix, or 0 */
    int w_top, w_knee, w_slope;       /* whites: the white, where it is reached past the top 64 levels' start, slope (Q12; 0 off) */
    int split_u[2], split_v[2];       /* shadow and highlight tint at full weight (Q12) */
} prepared;

/* Per step of each row: contrast moves x by 0.16 x(1-x)(2x-1), midtones by
 * 0.12 x(1-x) (up to 38 levels at 5), shadows and highlights by 0.169
 * x(1-x)^2 and x^2(1-x) (each peaks 6.4 levels a step, at a third and two
 * thirds), fade lifts black 0.03 (8 levels) a step. */
#define K_CONTRAST 655
#define K_MIDTONES 492
#define K_SHADOWS 691
#define K_HIGHLIGHTS 691
#define K_FADE 123
/* Blacks and whites move the ends of the range, over the darkest and the
 * brightest 64 levels (the table's last two nodes at each end: it has one
 * every 32 levels, so nothing finer would show). Blacks -: the black point
 * rises 6 levels a step (30 at -5), everything below it is black and the
 * toe steepens (crushed slide-film blacks); blacks +: black lifts 6 levels
 * a step, fading out by 64 (faded instant-film blacks; Fade lifts the whole
 * range instead, and comes after, so a crushed toe can still be faded).
 * Whites -: white comes down 6 levels a step (a rolled-off, paper-white
 * print); whites +: the white point comes down, so the top clips earlier
 * (brighter, harder highlights). Whites act last, on each channel after
 * saturation and HSL, so nothing can push a colour past that white. */
#define END 1024
#define K_END 96
static int blacks(const film_grade_settings *s, int y)
{
    int b = s->v[FILM_GRADE_BLACKS];
    if (b && y < END) {
        int lift = b > 0 ? b * K_END : 0, cut = b < 0 ? -b * K_END : 0;
        y = y <= cut ? lift : lift + (y - cut) * (END - lift) / (END - cut);
    }
    return y;
}
static int whites(const prepared *p, int y)
{
    int x = y - (ONE - END);
    if (!p->w_slope || x <= 0) return y;
    return x >= p->w_knee ? p->w_top : ONE - END + (x * p->w_slope >> 12);
}
static int tone(const film_grade_settings *s, int x)
{
    int a = x, b = ONE - x, xb = a * b >> 12, y;
    y = x + ((s->v[FILM_GRADE_CONTRAST] * K_CONTRAST) * ((xb * (2 * a - ONE)) >> 12) +
             (s->v[FILM_GRADE_MIDTONES] * K_MIDTONES) * xb +
             (s->v[FILM_GRADE_SHADOWS] * K_SHADOWS) * (xb * b >> 12) +
             (s->v[FILM_GRADE_HIGHLIGHTS] * K_HIGHLIGHTS) * (xb * a >> 12)) / ONE;
    return clamp(y, 0, ONE);
}
static int curve(const prepared *p, int x)
{
    int i = x >> 4, f = x & 15;
    if (x <= 0) return p->curve[0];
    if (i >= 256) return p->curve[256];
    return p->curve[i] + ((p->curve[i + 1] - p->curve[i]) * f >> 4);
}

/* Unit chroma (U, V in Q12) of each colour's pure hue, BT.601. */
static const short hue_uv[FILM_GRADE_HUES][2] = {
    {-1310, 3881}, {-3091, 2687}, {-4043, 657}, {-2541, -3212}, {1310, -3881}, {4043, -657}, {3795, 1540}, {2541, 3212}};
/* Split toning: 4 levels of chroma a step at full weight. */
#define K_SPLIT 64

static void prepare(const film_grade_settings *s, prepared *p)
{
    int i, lift = s->v[FILM_GRADE_FADE] * K_FADE, top = 0, t = s->v[FILM_GRADE_TEMPERATURE], m = s->v[FILM_GRADE_TINT];
    int sv = s->v[FILM_GRADE_SATURATION];
    for (i = 0; i <= 256; i++) {
        int y = blacks(s, tone(s, i * 16));
        if (y < top) y = top;                     /* never darker for a brighter input */
        top = y;
        p->curve[i] = (unsigned short)(lift + (y * (ONE - lift) >> 12));
    }
    /* Temperature: amber (-U, +V) about 3 and 2.6 levels a step at white;
     * tint: magenta (+U, +V) about 2.5 and 3. */
    p->du = -48 * t + 40 * m;
    p->dv = 42 * t + 48 * m;
    /* Saturation: -5 is black and white (0.2 a step), +5 is 1.5 (0.1 a
     * step). Vibrance: up to 0.08 a step on grey-ish colours, nothing on
     * fully saturated ones. */
    p->sat = sv < 0 ? ONE + sv * 819 : ONE + sv * 410;
    p->vib = s->v[FILM_GRADE_VIBRANCE] * 328;
    p->hsl = 0;
    for (i = FILM_GRADE_HSL_HUE; i < FILM_GRADE_HSL_LIGHT + FILM_GRADE_HUES; i++) p->hsl |= s->v[i];
    {
        int w = s->v[FILM_GRADE_WHITES], cut = w > 0 ? w * K_END : 0;
        p->w_top = w < 0 ? ONE + w * K_END : ONE;
        p->w_knee = END - cut;
        p->w_slope = w ? (p->w_top - (ONE - END)) * ONE / (END - cut) : 0;
    }
    p->mono = s->v[FILM_GRADE_MONO] > 0 && s->v[FILM_GRADE_MONO] < FILM_GRADE_MONOS ? mono_mix[s->v[FILM_GRADE_MONO]] : 0;
    for (i = 0; i < 2; i++) {
        int hue = s->v[i ? FILM_GRADE_HIGHLIGHT_HUE : FILM_GRADE_SHADOW_HUE];
        int amt = s->v[i ? FILM_GRADE_HIGHLIGHT_AMOUNT : FILM_GRADE_SHADOW_AMOUNT] * K_SPLIT;
        p->split_u[i] = hue ? amt * hue_uv[hue - 1][0] >> 12 : 0;
        p->split_v[i] = hue ? amt * hue_uv[hue - 1][1] >> 12 : 0;
    }
}

/* The hues the HSL rows act on, as hexcone hue (one sixth of the circle =
 * 4096): red 0, orange 30, yellow 60, green 120, aqua 180, blue 240, purple
 * 270, magenta 300 degrees, and red again. Between two neighbours the
 * weights go linearly from one to the other. */
static const unsigned short centre[FILM_GRADE_HUES + 1] = {0, 2048, 4096, 8192, 12288, 16384, 18432, 20480, 24576};
#define CIRCLE 24576
/* HSL per step: hue 6 degrees (410 in sixths), saturation -0.2 / +0.12,
 * lightness 0.08 of the luma, scaled by the colour's saturation. */
#define K_HUE 410
#define K_LIGHT 328

static void hsl(const film_grade_settings *s, int *rgb, int *light)
{
    int r = rgb[0], g = rgb[1], b = rgb[2], mx, mn, d, h, sat, i, j, wi, wj, dh, fs, fi, fj, si, sj;
    mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    d = mx - mn;
    *light = 0;
    if (d <= 0 || mx <= 0) return;                /* grey: no hue */
    if (mx == r) { h = (g - b) * ONE / d; if (h < 0) h += CIRCLE; }
    else if (mx == g) h = 2 * ONE + (b - r) * ONE / d;
    else h = 4 * ONE + (r - g) * ONE / d;
    if (h >= CIRCLE) h -= CIRCLE;
    sat = d * ONE / mx;
    for (i = 0; i < FILM_GRADE_HUES - 1 && h >= centre[i + 1]; i++) {}
    j = (i + 1) % FILM_GRADE_HUES;
    wj = centre[i + 1] - centre[i] == 2048 ? (h - centre[i]) << 1 : h - centre[i];
    wi = ONE - wj;
    dh = (wi * s->v[FILM_GRADE_HSL_HUE + i] + wj * s->v[FILM_GRADE_HSL_HUE + j]) * K_HUE >> 12;
    si = s->v[FILM_GRADE_HSL_SAT + i]; sj = s->v[FILM_GRADE_HSL_SAT + j];
    fi = ONE + (si < 0 ? si * 819 : si * 492);
    fj = ONE + (sj < 0 ? sj * 819 : sj * 492);
    fs = (wi * fi + wj * fj) >> 12;
    *light = (((wi * s->v[FILM_GRADE_HSL_LIGHT + i] + wj * s->v[FILM_GRADE_HSL_LIGHT + j]) * K_LIGHT >> 12) * sat) >> 12;
    if (!dh && fs == ONE) return;
    h += dh;
    if (h < 0) h += CIRCLE;
    if (h >= CIRCLE) h -= CIRCLE;
    sat = clamp(sat * fs >> 12, 0, ONE);
    {
        int sector = h >> 12, f = h & (ONE - 1), v = mx;
        int pp = v * (ONE - sat) >> 12, q = v * (ONE - (sat * f >> 12)) >> 12, t = v * (ONE - (sat * (ONE - f) >> 12)) >> 12;
        switch (sector) {
        case 0: r = v; g = t; b = pp; break;
        case 1: r = q; g = v; b = pp; break;
        case 2: r = pp; g = v; b = t; break;
        case 3: r = pp; g = q; b = v; break;
        case 4: r = t; g = pp; b = v; break;
        default: r = v; g = pp; b = q; break;
        }
    }
    rgb[0] = r; rgb[1] = g; rgb[2] = b;
}

static int luma(const int *c) { return (1225 * c[0] + 2404 * c[1] + 467 * c[2]) >> 12; }

/* HSL. Lightness keeps the hue and the gamut: darker scales the colour
 * toward black, lighter mixes it toward white (a bright pure colour can
 * only get lighter by getting paler). Either moves the luma by light x
 * luma. */
static void apply_hsl(const film_grade_settings *s, int *c)
{
    int light = 0, i, y;
    hsl(s, c, &light);
    if (light < 0)
        for (i = 0; i < 3; i++) c[i] = c[i] * (ONE + light) >> 12;
    else if (light > 0) {
        y = luma(c);
        if (y < ONE) {
            int k = (y * light >> 12) * ONE / (ONE - y);
            if (k > ONE) k = ONE;
            for (i = 0; i < 3; i++) c[i] += (ONE - c[i]) * k >> 12;
        }
    }
}

static void pixel(const film_grade_settings *s, const prepared *p, const int in[3], int out[3])
{
    int c[3], y, du, dv, i, f, mx, mn, u, v, yn, ws, wh;
    for (i = 0; i < 3; i++) c[i] = clamp(in[i], 0, ONE);
    /* 1. Colour balance: a chroma shift in proportion to the luma (as a
     * white balance would: black stays black), shortened to stay in the
     * gamut, so the luma never changes (a clipped channel made warm white
     * darker than the grey below it) and white stays white. */
    if (p->du || p->dv) {
        int d[3], k = ONE;
        y = luma(c);
        du = y * p->du >> 12; dv = y * p->dv >> 12;
        d[0] = 5743 * dv >> 12;
        d[1] = -((1410 * du + 2925 * dv) >> 12);
        d[2] = 7258 * du >> 12;
        for (i = 0; i < 3; i++) {
            if (c[i] + d[i] > ONE && (ONE - c[i]) * ONE / d[i] < k) k = (ONE - c[i]) * ONE / d[i];
            if (c[i] + d[i] < 0 && c[i] * ONE / -d[i] < k) k = c[i] * ONE / -d[i];
        }
        for (i = 0; i < 3; i++) c[i] = clamp(c[i] + (d[i] * k >> 12), 0, ONE);
    }
    if (p->mono) {
        /* Black and white: HSL on the colour (its lightness rows lighten or
         * darken each colour in the grey), the filter's grey mix, then the
         * tone curve on the grey. */
        if (p->hsl) apply_hsl(s, c);
        y = clamp((p->mono[0] * c[0] + p->mono[1] * c[1] + p->mono[2] * c[2]) >> 12, 0, ONE);
        c[0] = c[1] = c[2] = curve(p, y);
    } else {
        /* 2. Tone. */
        for (i = 0; i < 3; i++) c[i] = curve(p, c[i]);
        /* 3. Saturation and vibrance, about the luma. */
        if (p->sat != ONE || p->vib) {
            mx = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
            mn = c[0] < c[1] ? (c[0] < c[2] ? c[0] : c[2]) : (c[1] < c[2] ? c[1] : c[2]);
            f = p->sat;
            if (p->vib) f = f * (ONE + (p->vib * (ONE - (mx > 0 ? (mx - mn) * ONE / mx : 0)) >> 12)) >> 12;
            y = luma(c);
            for (i = 0; i < 3; i++) c[i] = clamp(y + ((c[i] - y) * f >> 12), 0, ONE);
        }
        /* 4. HSL. */
        if (p->hsl) apply_hsl(s, c);
    }
    /* Whites, on each channel. */
    for (i = 0; i < 3; i++) c[i] = whites(p, c[i]);
    /* To YCbCr, then split toning there. */
    y = luma(c);
    u = (-691 * c[0] - 1357 * c[1] + 2048 * c[2]) >> 12;
    v = (2048 * c[0] - 1715 * c[1] - 333 * c[2]) >> 12;
    yn = clamp(y, 0, ONE);
    ws = (ONE - yn) * (ONE - yn) >> 12;
    wh = yn * yn >> 12;
    u += (ws * p->split_u[0] + wh * p->split_u[1]) >> 12;
    v += (ws * p->split_v[0] + wh * p->split_v[1]) >> 12;
    out[0] = y; out[1] = u; out[2] = v;
}

void film_grade_pixel(const film_grade_settings *s, const int rgb[3], int out[3])
{
    prepared p;
    prepare(s, &p);
    pixel(s, &p, rgb, out);
}

/* Q12 to the table's 9-bit scale (white Y = 512), rounded. */
static int nine(int x) { return (x + 4) >> 3; }

void film_grade_table(const film_grade_settings *s, unsigned *table)
{
    prepared p;
    int r, g, b, in[3], o[3];
    prepare(s, &p);
    for (b = 0; b < 10; b++)
        for (g = 0; g < 10; g++)
            for (r = 0; r < 10; r++) {
                unsigned yy, uu, vv;
                if (r == 9 || g == 9 || b == 9) {         /* past white: node 8's value */
                    table[r + 10 * g + 100 * b] = table[(r < 9 ? r : 8) + 10 * (g < 9 ? g : 8) + 100 * (b < 9 ? b : 8)];
                    continue;
                }
                in[0] = r * 512; in[1] = g * 512; in[2] = b * 512;
                pixel(s, &p, in, o);
                yy = (unsigned)clamp(nine(o[0]), 0, 1023);
                uu = (unsigned)clamp(nine(o[1]), -512, 511) & 0x3ffu;
                vv = (unsigned)clamp(nine(o[2]), -512, 511) & 0x3ffu;
                table[r + 10 * g + 100 * b] = yy << 20 | uu << 10 | vv;
            }
}
