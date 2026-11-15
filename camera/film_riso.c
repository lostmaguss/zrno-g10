/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Print kernels (see film_riso.h).
 *
 * The shot, per two rows of the frame:
 * 1. tone: per 4x2 block and ink, one UYVY word read where that ink's drum
 *    put it (shifted, and turned about the frame's centre; never above the
 *    current rows, so never an overwritten pixel), one separation lookup of
 *    its chroma and mean luma, plus tone noise, the drum's soft streaks and
 *    now and then a white speck, stored as a | a << 16.
 * 2. screen: per pixel pair and ink one word subtract against the packed
 *    threshold tile gives both pixels' ink bits at once; now and then a dot
 *    is lost; the bits of all inks pick one precomputed UYVY word, so the
 *    inks' overprint colours come out exactly.
 * No divide or float in the pixel loops; tiles wrap by masks. */
#include "film_riso.h"

#define AI static inline __attribute__((always_inline))
#define LCG(s) ((s) = (s) * 1664525u + 1013904223u)

/* ---- PRINT.BIN (tools/riso_pack.py) ------------------------------------- */
#define D_GAMMA 16
#define D_GRAIN 272
#define D_DOT45 (D_GRAIN + 3 * FILM_RISO_TILE * FILM_RISO_TILE)   /* 16 x 16 */
#define D_DOT0 (D_DOT45 + 256)                                     /* 8 x 8 */
#define D_LDOT45 (D_DOT0 + 64)                                     /* 8 x 8 */
#define D_LDOT0 (D_LDOT45 + 64)                                    /* 4 x 4 */
#define D_FLAT (D_LDOT0 + 16)                                      /* 128 x 128 each: */
#define D_ENGRAVE (D_FLAT + FILM_RISO_TILE * FILM_RISO_TILE)
#define D_WOOD (D_ENGRAVE + FILM_RISO_TILE * FILM_RISO_TILE)
#define D_CDOT45 (D_WOOD + FILM_RISO_TILE * FILM_RISO_TILE)        /* 32 x 32 */
#define D_CDOT0 (D_CDOT45 + 1024)                                  /* 16 x 16 */
#define D_GLYPHS (D_CDOT0 + 256)                                   /* 16 x 16 rows of 8 bits */
#define D_SETS (D_GLYPHS + 256)
#define SET_BYTES 24784u
/* A set's header: inks, style, register mask, flags, screen per ink. */
#define S_STYLE 1
#define S_REG 2
#define S_FLAGS 3
#define S_SCREENS 4
#define F_KEY 1                                                    /* black keylines from edges */
#define F_DARKKEY 2                                                /* and the darkest tones in key */
#define F_COLSEP 4                                                 /* the set's own colour separation */
#define F_TEXTCOL 8                                                /* text in the cells' own colours */
enum { SCR_ROW = 255, SCR_FLAT = 1, SCR_ENGRAVE, SCR_WOOD, SCR_CDOT45, SCR_CDOT0, SCR_CDOT0B, SCR_GRAIN = 100, SCR_DOTS };
#define S_PAL 16                                                   /* [paper][8][y, u, v, 0] */
#define S_DUO 112                                                  /* [ink][32] */
#define S_SEP 208                                                  /* [ink][256 uv][32 luma] */
#define SEP_BYTES 8192u

static const unsigned char *set_rec(const unsigned char *data, unsigned set) { return data + D_SETS + set * SET_BYTES; }

int film_riso_data_check(const unsigned char *data, size_t size)
{
    static const char magic[8] = "G12RS02";
    unsigned i;
    if (!data || size != FILM_RISO_DATA_BYTES) return -1;
    for (i = 0; i < 8; i++) if (data[i] != (unsigned char)magic[i]) return -1;
    if ((data[8] | data[9] << 8) != FILM_RISO_ALL_SETS || (data[10] | data[11] << 8) != FILM_RISO_TILE ||
        (data[12] | data[13] << 8 | (unsigned)data[14] << 16 | (unsigned)data[15] << 24) != SET_BYTES) return -1;
    for (i = 0; i < FILM_RISO_ALL_SETS; i++) {
        const unsigned char *r = set_rec(data, i);
        unsigned n = r[0], k;
        if (n > FILM_RISO_INKS || r[S_STYLE] >= FILM_RISO_STYLES) return -1;
        if ((r[S_STYLE] == FILM_RISO_STYLE_TEXT) != (n == 0) || (i < FILM_RISO_SETS && (n < 2 || r[S_STYLE]))) return -1;
        for (k = 0; k < n; k++) if (r[S_SCREENS + k] != SCR_ROW && (r[S_SCREENS + k] < SCR_FLAT || r[S_SCREENS + k] > SCR_CDOT0B)) return -1;
    }
    return 0;
}
unsigned film_riso_ink_count(const unsigned char *data, unsigned set) { return set < FILM_RISO_ALL_SETS ? set_rec(data, set)[0] : 2; }

/* ---- Styles ------------------------------------------------------------------ */
static const unsigned char style_base[FILM_RISO_STYLES] = {0, 7, 10, 13, 15, 17, 19};
static const unsigned char style_count[FILM_RISO_STYLES] = {7, 3, 3, 2, 2, 2, 3};
unsigned film_riso_variants(unsigned style) { return style < FILM_RISO_STYLES ? style_count[style] : 1; }
unsigned film_riso_set(const film_riso_settings *s)
{
    if (!s->style || s->style >= FILM_RISO_STYLES) return s->inks < FILM_RISO_SETS ? s->inks : 0;
    return style_base[s->style] + (s->variant < style_count[s->style] ? s->variant : 0);
}
/* The styles bring their own paper: the Paper row only changes Riso sets. */
static unsigned paper_of(const film_riso_settings *s) { return s->style ? 0 : s->paper < FILM_RISO_PAPERS ? s->paper : 0; }
void film_riso_colour(const unsigned char *data, const film_riso_settings *s, unsigned m, int *y, int *u, int *v)
{
    const unsigned char *e = set_rec(data, film_riso_set(s)) + S_PAL + (paper_of(s) * 8 + (m & 7)) * 4;
    *y = e[0]; *u = (signed char)e[1]; *v = (signed char)e[2];
}
unsigned film_riso_live_colours(const unsigned char *data, const film_riso_settings *s)
{
    const unsigned char *r = set_rec(data, film_riso_set(s));
    return r[0] > 2 || (r[S_FLAGS] & F_TEXTCOL) ? 8 : 4;
}

/* ---- Names ------------------------------------------------------------------ */
static const char *pick(const char *const *names, unsigned n, unsigned v) { return v < n ? names[v] : "?"; }
const char *film_riso_inks_name(unsigned v)
{
    static const char *const n[FILM_RISO_SETS] = {"Pink + Blue", "Teal + Orange", "Blue + Orange", "Blue + Yellow",
                                                  "Green + Pink", "Black + Pink", "Yellow+Pink+Blue"};
    return pick(n, FILM_RISO_SETS, v);
}
const char *film_riso_separation_name(unsigned v) { static const char *const n[] = {"Colour", "Duotone"}; return pick(n, 2, v); }
const char *film_riso_screen_name(unsigned v) { static const char *const n[] = {"Grain", "Dots"}; return pick(n, 2, v); }
const char *film_riso_noise_name(unsigned v) { static const char *const n[] = {"0 Clean", "1 Fine", "2 Light", "3 Medium", "4 Heavy"}; return pick(n, 5, v); }
const char *film_riso_register_name(unsigned v) { static const char *const n[] = {"Off", "1 mm", "1.5 mm", "2 mm"}; return pick(n, 4, v); }
const char *film_riso_flaws_name(unsigned v) { static const char *const n[] = {"Off", "Subtle", "Strong", "Heavy"}; return pick(n, 4, v); }
const char *film_riso_paper_name(unsigned v) { static const char *const n[] = {"Off-white", "White", "Cream"}; return pick(n, 3, v); }
const char *film_riso_style_name(unsigned v)
{
    static const char *const n[FILM_RISO_STYLES] = {"Riso", "Screenprint", "Engraving", "Woodcut", "Photocopy", "Comic", "Text mode"};
    return pick(n, FILM_RISO_STYLES, v);
}
const char *film_riso_variant_name(unsigned style, unsigned v)
{
    static const char *const n[FILM_RISO_ALL_SETS - FILM_RISO_SETS] = {
        "Pop", "Sunset", "Mint + Crimson", "Aubergine", "Steel blue", "Two plates", "Black", "Two blocks",
        "White paper", "Pink paper", "Full key", "Outlines only", "Green", "Amber", "Colour"};
    if (!style) return film_riso_inks_name(v);
    if (style >= FILM_RISO_STYLES || v >= style_count[style]) return "?";
    return n[style_base[style] - FILM_RISO_SETS + v];
}

/* ---- Settings in numbers ------------------------------------------------------ */
static const unsigned char noise_amp[FILM_RISO_NOISES] = {0, 15, 30, 50, 75};
static const unsigned char reg_px[FILM_RISO_REGISTERS] = {0, 12, 18, 25};     /* at L: 12.3 px per mm on an A4 print */
static const struct {
    short skew;                  /* drum turn, 1/65536 per pixel (137: 0.12 deg) */
    unsigned char drop_gap;      /* lost dots: twice the mean gap, in steps of 4 px (0: none) */
    unsigned char speck;         /* white specks: chance per 4x2 block, of 4096 */
    unsigned char speck_run;     /* mask of extra blocks a speck runs on */
    unsigned char streak;        /* streak tone, at most */
    unsigned char bands_lo, bands_hi, width_lo, width_hi;   /* soft bands: count, width in blocks of 4 px at L */
} flaws[FILM_RISO_FLAWS] = {
    {0, 0, 0, 0, 0, 0, 0, 1, 1},
    {137, 13, 12, 1, 8, 2, 3, 2, 6},
    {330, 6, 60, 1, 28, 3, 5, 4, 14},
    {570, 3, 170, 3, 45, 4, 7, 4, 18},
};

/* ---- The print job --------------------------------------------------------- */
typedef struct {
    unsigned inks;
    const unsigned char *sep[FILM_RISO_INKS];      /* [uv][32] tones, levels applied */
    const unsigned *screen[FILM_RISO_INKS];        /* 128 rows x 64 words: t(x) | t(x+1) << 16 | 0x01000100 */
    int dx[FILM_RISO_INKS], dy[FILM_RISO_INKS], skew[FILM_RISO_INKS];
    const unsigned short *gain;                    /* tone noise by tone, or 0 */
    const signed char *streak[FILM_RISO_INKS];     /* tone offset per 4 px column, or 0 */
    const unsigned char *fade;                     /* 0..64 by tone */
    unsigned seed, speck, speck_run, drop_gap;
    unsigned pair[64];                             /* UYVY word by inverted ink bits */
    /* Comic keylines: lum[] is the frame's luma (Y >> 3) after the levels,
     * 0..255; a block whose neighbours differ by more than key_edge, or
     * (dark key) darker than key_dark, prints in key (the all-ink colour). */
    unsigned keylines, key_edge, key_dark;
    unsigned char lum[32];
} job_t;

/* Tone noise: per 4x2 block a random number, smoothed along the row and down
 * the rows by two one-pole filters (std about 16). An AR(1) process has a
 * symmetric correlation, so the grains are round on average although the
 * filters only look back; nothing repeats. The filters round to nearest:
 * shifts that floor bias the noise by -2.5, about 12 tone levels of less
 * ink at noise 4 (seen in tests/test_riso.c). */
AI void tone_pass(const unsigned char *frame, unsigned w, unsigned h, unsigned stride, const job_t *p,
                  unsigned *dens, unsigned y, unsigned k, unsigned *rng)
{
    const int nb = (int)(w >> 2);
    const unsigned char *sep = p->sep[k];
    const unsigned short *gain = p->gain;
    const signed char *streak = p->streak[k];
    const unsigned char *fade = p->fade;
    const unsigned speck = p->speck, srun = p->speck_run;
    unsigned *d = dens + k * nb;
    int *vs = (int *)dens + (FILM_RISO_INKS + k) * nb;                                   /* noise state down the rows */
    unsigned char *vr = (unsigned char *)((int *)dens + 2 * FILM_RISO_INKS * nb) + k * nb;   /* specks running down */
    unsigned s = rng[k], sp = rng[FILM_RISO_INKS + k], spr = 0;
    int hs = 0, i;
    /* The drum, turned about the frame's centre: x' = x + dx - (y - h/2) skew,
     * y' = y + dy + (x - w/2) skew. The column shift is per row; the row shift
     * is set per 64 px segment (a 0.12 degree turn moves one row in ~500 px). */
    const int sk = p->skew[k];
    const int xb = (p->dx[k] - ((((int)y - (int)(h >> 1)) * sk) >> 16)) >> 2;
    int q = ((int)y + p->dy[k]) * 65536 + sk * (32 - (int)(w >> 1));
    const int ylo = (int)y, yhi = (int)h - 1;
#define TONE_BLOCK(wd) do { \
        unsigned uv = ((wd) & 0xF0) | (((wd) >> 20) & 0xF); \
        unsigned yy = ((((wd) >> 8) & 255) + ((wd) >> 24)) >> 4; \
        int v = sep[(uv << 5) + yy]; \
        if (gain) { \
            int r = (int)(LCG(s) >> 24) - 128; \
            hs += (r - hs + 1) >> 1; \
            int g = vs[i] += (hs - vs[i] + 2) >> 2; \
            v += (g * gain[v]) >> 8; \
            if (v > 255) v = 255; if (v < 0) v = 0; \
        } \
        if (streak) { v += (streak[i] * fade[v]) >> 6; if (v > 255) v = 255; if (v < 0) v = 0; } \
        if (speck) { /* a fibre blocks this block, running on across or down */ \
            if (spr) { v >>= 2; spr--; } \
            else if (vr[i]) { v >>= 2; vr[i]--; } \
            else if ((LCG(sp) >> 20) < speck) { \
                v >>= 2; \
                if (sp & 0x10000) spr = (sp >> 8) & srun; else vr[i] = (unsigned char)((sp >> 8) & srun); } } \
        d[i] = (unsigned)v | (unsigned)v << 16; } while (0)
    for (int i0 = 0; i0 < nb; i0 += 16, q += 64 * sk) {
        int row = q >> 16, i1 = i0 + 16 < nb ? i0 + 16 : nb;
        if (row < ylo) row = ylo; else if (row > yhi) row = yhi;           /* never above: those rows are written */
        const unsigned char *base = frame + (unsigned)row * stride;
        if (i0 + xb >= 0 && i1 - 1 + xb < nb) {
            const unsigned *src = (const unsigned *)(base + (unsigned)(i0 + xb) * 8);
            for (i = i0; i < i1; i++, src += 2) { unsigned wd = *src; TONE_BLOCK(wd); }
        } else {
            for (i = i0; i < i1; i++) {
                int xi = i + xb; if (xi < 0) xi = 0; else if (xi >= nb) xi = nb - 1;
                unsigned wd = *(const unsigned *)(base + (unsigned)xi * 8); TONE_BLOCK(wd);
            }
        }
    }
#undef TONE_BLOCK
    rng[k] = s; rng[FILM_RISO_INKS + k] = sp;
}

#define INK(k, j) (((s##k[j] - a##k) & 0x01000100u) >> (8 - (k)))

AI void run(unsigned char *frame, unsigned w, unsigned h, unsigned stride, const job_t *p, unsigned *dens,
            void (*progress)(unsigned, unsigned), const unsigned n)
{
    unsigned nb = w >> 2, y, i, k, rng[2 * FILM_RISO_INKS];
    const unsigned sh = 16 - n, msk = (1u << (2 * n)) - 1;
    /* Noise state: a seed per ink, and the row filter run in for 16 rows so
     * the top of the frame is as grainy as the rest. */
    for (k = 0; k < n; k++) {
        int *vs = (int *)dens + (FILM_RISO_INKS + k) * nb;
        unsigned char *vr = (unsigned char *)((int *)dens + 2 * FILM_RISO_INKS * nb) + k * nb;
        unsigned s = p->seed * 2654435761u + k * 0x9E3779B9u + 1;
        for (i = 0; i < nb; i++) { vs[i] = 0; vr[i] = 0; }
        for (unsigned pre = 0; pre < 16 && p->gain; pre++) {
            int hs = 0;
            for (i = 0; i < nb; i++) { int r = (int)(LCG(s) >> 24) - 128; hs += (r - hs + 1) >> 1; vs[i] += (hs - vs[i] + 2) >> 2; }
        }
        rng[k] = s; rng[FILM_RISO_INKS + k] = s ^ 0x5bd1e995u;
    }
    /* Lost dots: the gap to the next one is drawn (uniform, mean drop_gap/2
     * steps of 4 px), so the pixel loop only compares an index. */
    unsigned lost = p->seed * 0x85EBCA77u + 777u;
    const unsigned gap2 = p->drop_gap;
    /* Keylines: this pair of rows' block luma, the last pair's, and the flags
     * (bit 0: edge here; bit 1: a vertical edge last time, carried down so
     * horizontal lines are as thick as vertical ones). */
    unsigned char *kl = (unsigned char *)((int *)dens + 2 * FILM_RISO_INKS * nb) + FILM_RISO_INKS * nb, *kp = kl + nb, *kf = kp + nb;
    if (p->keylines) for (i = 0; i < nb; i++) { kp[i] = 255; kf[i] = 0; }
    for (y = 0; y < h; y += 2) {
        if (progress && !(y & 31)) progress(y, h);
        if (p->keylines) {
            const unsigned *src = (const unsigned *)(frame + y * stride);
            const int e = (int)p->key_edge, dk = (int)p->key_dark;
            for (i = 0; i < nb; i++) { unsigned wd = src[2 * i]; kl[i] = p->lum[(((wd >> 8) & 255) + (wd >> 24)) >> 4]; }
            for (i = 0; i < nb; i++) {
                int l = kl[i], f = 0, d;
                d = l - kl[i ? i - 1 : 0]; if (d > e || -d > e) f = 1;
                d = l - kl[i + 1 < nb ? i + 1 : i]; if (d > e || -d > e) f = 1;
                d = l - kp[i]; if (y && (d > e || -d > e)) f = 3;
                if (kf[i] & 2) f |= 1;
                if (l < dk) f |= 1;
                kf[i] = (unsigned char)(f == 3 ? 3 : f & 1);
                kp[i] = (unsigned char)l;
            }
        }
        for (k = 0; k < n; k++) tone_pass(frame, w, h, stride, p, dens, y, k, rng);
        for (unsigned r = y; r < y + 2 && r < h; r++) {
            unsigned *out = (unsigned *)(frame + r * stride);
            const unsigned *d0 = dens, *d1 = dens + nb, *d2 = dens + 2 * nb;
            const unsigned rr = r & (FILM_RISO_TILE - 1);
            const unsigned *t0 = p->screen[0] + rr * (FILM_RISO_TILE / 2);
            const unsigned *t1 = n > 1 ? p->screen[1] + rr * (FILM_RISO_TILE / 2) : t0;
            const unsigned *t2 = n > 2 ? p->screen[2] + rr * (FILM_RISO_TILE / 2) : t0;
            unsigned next = gap2 ? (LCG(lost) >> 24) * gap2 >> 8 : ~0u;
            for (i = 0; i < nb; i++) {
                const unsigned j = (i << 1) & (FILM_RISO_TILE / 2 - 1);
                const unsigned *s0 = t0 + j, *s1 = t1 + j, *s2 = t2 + j;
                unsigned a0 = d0[i], a1 = n > 1 ? d1[i] : 0, a2 = n > 2 ? d2[i] : 0, m0, m1;
                m0 = INK(0, 0); m1 = INK(0, 1);
                if (n > 1) { m0 |= INK(1, 0); m1 |= INK(1, 1); }
                if (n > 2) { m0 |= INK(2, 0); m1 |= INK(2, 1); }
                /* A lost dot: a fibre in the stencil blocks 1 px (half the
                 * time), 2 px or the whole 4 px step of one drum. The bits mean
                 * "no ink", so it only shows where that ink would print. */
                if (i == next) {
                    unsigned b = LCG(lost) >> 8, ink = (b >> 1) & 3;
                    if (ink < n) {
                        unsigned one = 1u << ink, len = (b >> 4) & 3;
                        unsigned bit = len == 0 ? one | one << 16 : one << ((b & 1) << 4);
                        if (len == 1) { m0 |= one | one << 16; m1 |= one | one << 16; }
                        else if (b & 0x400) m1 |= bit; else m0 |= bit;
                    }
                    next = i + 1 + ((b >> 16) * gap2 >> 8);
                }
                out[2 * i] = p->pair[(m0 | m0 >> sh) & msk];
                out[2 * i + 1] = p->pair[(m1 | m1 >> sh) & msk];
                if (p->keylines && (kf[i] & 1)) out[2 * i] = out[2 * i + 1] = p->pair[0];   /* all inks: the key colour */
            }
        }
    }
}

/* ---- Setting a shot up --------------------------------------------------- */
static unsigned rnd(unsigned *s, unsigned lo, unsigned hi) { LCG(*s); return lo + (((*s >> 16) * (hi - lo + 1)) >> 16); }

void film_riso_levels_uyvy(const unsigned char *frame, unsigned width, unsigned height, unsigned stride, int *lo, int *hi)
{
    static unsigned hist[256];
    unsigned x, y, n = 0, c, v;
    for (v = 0; v < 256; v++) hist[v] = 0;
    for (y = 4; y < height; y += 8)
        for (x = 4; x < width; x += 8) { hist[frame[y * stride + 2 * x + 1]]++; n++; }
    for (c = 0, v = 0; v < 255 && (c += hist[v]) * 100 < n; v++) ;
    *lo = (int)v;
    for (c = 0, v = 255; v > 0 && (c += hist[v]) * 100 < n; v--) ;
    *hi = (int)v;
    if (*hi < *lo + 16) { int m = (*lo + *hi) >> 1; *lo = m - 8 < 0 ? 0 : m - 8; *hi = *lo + 16; }
}

/* The set's tones with the levels and the midtone lift in the luma axis. */
static void make_sep(const unsigned char *data, const film_riso_settings *s, int lo, int hi, unsigned char *out)
{
    const unsigned char *rec = set_rec(data, film_riso_set(s)), *gamma = data + D_GAMMA;
    /* Riso sets: the Separation row; the styles: their own curves, or (comic)
     * their own colour separation. */
    int duotone = s->style ? !(rec[S_FLAGS] & F_COLSEP) : s->separation;
    unsigned n = rec[0], k, uv, j;
    unsigned char map[32];
    int span = hi - lo > 0 ? hi - lo : 1;
    for (j = 0; j < 32; j++) {
        int t = ((int)(j * 8 + 4) - lo) * 255 / span;           /* 32 divides per shot, none per pixel */
        map[j] = gamma[t < 0 ? 0 : t > 255 ? 255 : t] >> 3;
    }
    for (k = 0; k < n; k++) {
        unsigned char *o = out + k * SEP_BYTES;
        if (duotone) {
            const unsigned char *duo = rec + S_DUO + k * 32;
            for (j = 0; j < 32; j++) o[j] = duo[map[j]];
            for (uv = 1; uv < 256; uv++) for (j = 0; j < 32; j++) o[uv * 32 + j] = o[j];
        } else {
            const unsigned char *src = rec + S_SEP + k * SEP_BYTES;
            for (uv = 0; uv < 256; uv++) for (j = 0; j < 32; j++) o[uv * 32 + j] = src[uv * 32 + map[j]];
        }
    }
}

/* Screen tiles: the thresholds of 128 x 128 pixels for ink k. Riso sets
 * take the Screen row (grain: a tile per ink; dots: the dark ink at 45
 * degrees and 11.3 px, the others at 0 degrees and 8 px, the third half a
 * cell over); the styles name their own. */
static unsigned tile_at(const unsigned char *data, unsigned id, unsigned k, unsigned x, unsigned y)
{
    const unsigned T = FILM_RISO_TILE;
    switch (id) {
    case SCR_GRAIN: return data[D_GRAIN + k * T * T + (y & (T - 1)) * T + (x & (T - 1))];
    case SCR_DOTS: return k == 0 ? data[D_DOT45 + (y & 15) * 16 + (x & 15)] : data[D_DOT0 + ((y + (k == 2) * 4) & 7) * 8 + ((x + (k == 2) * 4) & 7)];
    case SCR_FLAT: return data[D_FLAT + (y & (T - 1)) * T + (x & (T - 1))];
    case SCR_ENGRAVE: return data[D_ENGRAVE + (y & (T - 1)) * T + (x & (T - 1))];
    case SCR_WOOD: return data[D_WOOD + (y & (T - 1)) * T + (x & (T - 1))];
    case SCR_CDOT45: return data[D_CDOT45 + (y & 31) * 32 + (x & 31)];
    case SCR_CDOT0: return data[D_CDOT0 + (y & 15) * 16 + (x & 15)];
    default: return data[D_CDOT0 + ((y + 8) & 15) * 16 + ((x + 8) & 15)];
    }
}
static unsigned screen_id(const unsigned char *rec, const film_riso_settings *s, unsigned k)
{
    unsigned id = rec[S_SCREENS + k];
    return id == SCR_ROW ? (s->screen ? SCR_DOTS : SCR_GRAIN) : id;
}
static void make_screens(const unsigned char *data, const film_riso_settings *s, unsigned n, unsigned *out)
{
    const unsigned char *rec = set_rec(data, film_riso_set(s));
    unsigned k, x, y;
    for (k = 0; k < n; k++) {
        unsigned *o = out + k * (FILM_RISO_TILE * FILM_RISO_TILE / 2), id = screen_id(rec, s, k);
        for (y = 0; y < FILM_RISO_TILE; y++)
            for (x = 0; x < FILM_RISO_TILE; x += 2)
                o[y * (FILM_RISO_TILE / 2) + x / 2] = 0x01000100u + tile_at(data, id, k, x, y) + (tile_at(data, id, k, x + 1, y) << 16);
    }
}

/* Soft streaks along the feed (top to bottom) for one drum: a slow drift
 * across the sheet and a few soft bands, tone offsets per 4 px column. */
static void make_streak(signed char *st, int nb, int scale1024, unsigned level, unsigned *seed)
{
    int amp = flaws[level].streak, knots[40], i, b, nbands, step, nk;
    step = 64 * scale1024 >> 10;                 /* a knot every 256 px at L */
    if (step < 4) step = 4;
    nk = nb / step + 2;
    if (nk > 40) { nk = 40; step = nb / 38 + 1; }
    for (i = 0; i < nk; i++) knots[i] = (int)rnd(seed, 0, (unsigned)(amp * 6 / 5)) - amp * 3 / 5;   /* about 0.6 amp */
    for (i = 0; i < nb; i++) {                   /* one divide per column, per shot */
        int kk = i / step, f = i - kk * step;
        st[i] = (signed char)(knots[kk] + (knots[kk + 1] - knots[kk]) * f / step);
    }
    nbands = (int)rnd(seed, flaws[level].bands_lo, flaws[level].bands_hi);
    for (b = 0; b < nbands; b++) {
        int c = (int)rnd(seed, 0, (unsigned)nb - 1);
        int half = 2 * (int)rnd(seed, flaws[level].width_lo, flaws[level].width_hi) * scale1024 >> 10;
        int sign = rnd(seed, 0, 1) ? 1 : -1, recip;
        if (half < 2) half = 2;
        recip = 65536 / (half * half);
        for (i = c - half + 1; i < c + half; i++) {
            int d = i - c, v;
            if (i < 0 || i >= nb) continue;
            v = st[i] + sign * ((amp * (half * half - d * d) * recip) >> 16);
            st[i] = (signed char)(v > 127 ? 127 : v < -127 ? -127 : v);
        }
    }
}

size_t film_riso_work_size(unsigned width)
{
    size_t nb = width / 4;
    return FILM_RISO_INKS * SEP_BYTES + FILM_RISO_INKS * FILM_RISO_TILE * FILM_RISO_TILE * 2 +
           3 * FILM_RISO_INKS * nb * 4 + FILM_RISO_INKS * ((nb + 3) & ~(size_t)3) + 256 * 2 + 256;   /* keylines use the specks' spare row bytes */
}

/* Text mode: the frame as characters. Cells of 8 x 16 glyph pixels, each
 * 2 x 2 frame pixels from 2400 wide up (1 x 1 below), so a glyph pixel is a
 * whole UYVY pair at L. Per band of cells: their mean luma (and chroma for
 * coloured text) is sampled sparsely, then the band's rows are written:
 * the character whose ink matches the cell's brightness, every third row a
 * little dimmer (scanlines). Bands go top to bottom, so a band only reads
 * rows not yet written. */
static void text_render(unsigned char *frame, unsigned w, unsigned h, unsigned stride, const unsigned char *data,
                        const film_riso_settings *s, int lo, int hi, void (*progress)(unsigned, unsigned))
{
    static unsigned char cell_g[FILM_RISO_LIVE_W * 2], cell_c[FILM_RISO_LIVE_W * 2];
    static unsigned words[8][2][4];                 /* colour, scanline, two pixels' bits -> UYVY word */
    const unsigned char *rec = set_rec(data, film_riso_set(s)), *glyphs = data + D_GLYPHS, *gamma = data + D_GAMMA;
    const unsigned f = w >= 2400 ? 2 : 1, cw = 8 * f, chh = 16 * f, cols = w / cw, colour = rec[S_FLAGS] & F_TEXTCOL;
    unsigned c, band, x, y, k, b, scan;
    unsigned char map[256];
    int span = hi - lo > 0 ? hi - lo : 1;
    if (cols > sizeof(cell_g)) return;
    for (c = 0; c < 256; c++) {                     /* luma -> lifted tone (0.65 on top of the 0.7: text wants light) */
        int t = ((int)c - lo) * 255 / span;
        t = gamma[t < 0 ? 0 : t > 255 ? 255 : t];
        map[c] = (unsigned char)(gamma[t] * 15 / 255);
    }
    for (c = 0; c < 8; c++)
        for (scan = 0; scan < 2; scan++) {
            int yb, ub, vb, yf, uf, vf;
            film_riso_colour(data, s, 0, &yb, &ub, &vb);
            film_riso_colour(data, s, c ? c : 1, &yf, &uf, &vf);
            if (scan) { yf = yf * 7 / 8; uf = uf * 7 / 8; vf = vf * 7 / 8; }
            for (b = 0; b < 4; b++) {
                int y0 = b & 1 ? yf : yb, y1 = b & 2 ? yf : yb;
                int u = ((b & 1 ? uf : ub) + (b & 2 ? uf : ub)) >> 1, v = ((b & 1 ? vf : vb) + (b & 2 ? vf : vb)) >> 1;
                words[c][scan][b] = (unsigned)(u & 255) | (unsigned)y0 << 8 | (unsigned)(v & 255) << 16 | (unsigned)y1 << 24;
            }
        }
    for (band = 0; band + chh <= h; band += chh) {
        if (progress && !(band & 63)) progress(band, h);
        for (c = 0; c < cols; c++) {
            unsigned sum = 0, n = 0, su = 0, sv = 0;
            for (y = band + f; y < band + chh; y += 4 * f)
                for (x = c * cw; x < c * cw + cw; x += 4) {
                    const unsigned char *px = frame + y * stride + 2 * x;
                    sum += px[1] + px[3]; n += 2; su += (unsigned)(px[0] ^ 0x80); sv += (unsigned)(px[2] ^ 0x80);
                }
            cell_g[c] = map[n ? sum / n : 0];       /* one divide per cell: a few thousand a shot */
            cell_c[c] = 1;
            if (colour && n) {
                int u = (int)(su * 2 / n) - 128, v = (int)(sv * 2 / n) - 128, best = 0x7fffffff;
                for (k = 1; k < 8; k++) {           /* the colour whose chroma points the same way, exaggerated */
                    int yy, uu, vv, d;
                    film_riso_colour(data, s, k, &yy, &uu, &vv);
                    d = (u * 4 - uu) * (u * 4 - uu) + (v * 4 - vv) * (v * 4 - vv);
                    if (d < best) { best = d; cell_c[c] = (unsigned char)k; }
                }
            }
        }
        for (y = band; y < band + chh; y++) {
            unsigned *out = (unsigned *)(frame + y * stride), gy = (y - band) / f, sc = (y % 3) == 2;
            for (c = 0; c < cols; c++) {
                unsigned bits = glyphs[cell_g[c] * 16 + gy];
                const unsigned *wd = words[cell_c[c]][sc];
                unsigned *o = out + c * cw / 2;
                if (f == 2) for (b = 0; b < 8; b++) o[b] = wd[(bits >> (7 - b) & 1) * 3];
                else for (b = 0; b < 4; b++) o[b] = wd[(bits >> (7 - 2 * b) & 1) | (bits >> (6 - 2 * b) & 1) << 1];
            }
            for (x = cols * cw; x < w; x += 2) out[x / 2] = words[0][0][0];
        }
    }
    for (y = band; y < h; y++) {                    /* below the last whole band: the screen's background */
        unsigned *out = (unsigned *)(frame + y * stride);
        for (x = 0; x < w; x += 2) out[x / 2] = words[0][0][0];
    }
}

int film_riso_render(unsigned char *frame, unsigned width, unsigned height, unsigned stride,
                     const unsigned char *data, const film_riso_settings *s, unsigned seed,
                     void *work, size_t work_bytes, void (*progress)(unsigned, unsigned))
{
    static job_t job;                              /* kept off Canon's develop-task stack */
    unsigned char *sep = work;
    unsigned *screens = (unsigned *)(sep + FILM_RISO_INKS * SEP_BYTES), *dens;
    signed char *streak;
    unsigned short *gain;
    unsigned char *fade;
    unsigned nb = width / 4, n, k, a, b, full, rs = seed ^ 0x2545F491u, level = s->flaws;
    int lo, hi, scale = (int)(width * 1024u / 3648u);   /* sizes are set for L */
    const unsigned char *rec;
    if (!frame || !data || !s || (width & 3) || (height & 1) || stride < 2 * width || width < 64 || height < 16 ||
        s->inks >= FILM_RISO_SETS || s->paper >= FILM_RISO_PAPERS || s->flaws >= FILM_RISO_FLAWS ||
        s->noise >= FILM_RISO_NOISES || s->reg >= FILM_RISO_REGISTERS || s->style >= FILM_RISO_STYLES ||
        s->variant >= film_riso_variants(s->style) || work_bytes < film_riso_work_size(width))
        return -1;
    dens = screens + FILM_RISO_INKS * FILM_RISO_TILE * FILM_RISO_TILE / 2;
    streak = (signed char *)(dens + 3 * FILM_RISO_INKS * nb);
    gain = (unsigned short *)(streak + FILM_RISO_INKS * ((nb + 3) & ~3u));
    fade = (unsigned char *)(gain + 256);
    rec = set_rec(data, film_riso_set(s));
    n = rec[0];
    film_riso_levels_uyvy(frame, width, height, stride, &lo, &hi);
    if (!n) { text_render(frame, width, height, stride, data, s, lo, hi, progress); if (progress) progress(height, height); return 0; }
    make_sep(data, s, lo, hi, sep);
    make_screens(data, s, n, screens);
    job.keylines = rec[S_FLAGS] & F_KEY;
    if (job.keylines) {
        const unsigned char *gamma = data + D_GAMMA;
        int span = hi - lo > 0 ? hi - lo : 1;
        for (a = 0; a < 32; a++) { int t = ((int)(a * 8 + 4) - lo) * 255 / span; job.lum[a] = gamma[t < 0 ? 0 : t > 255 ? 255 : t]; }
        job.key_edge = 34;
        job.key_dark = rec[S_FLAGS] & F_DARKKEY ? 40 : 0;
    }
    for (a = 0; a < 256; a++) {
        unsigned m = a < 255 - a ? a : 255 - a;
        if (m > 48) m = 48;
        fade[a] = (unsigned char)((m * 4 + 1) / 3);              /* 0..64 */
        gain[a] = (unsigned short)(noise_amp[s->noise] * fade[a] / 4);
    }
    job.inks = n;
    job.gain = s->noise ? gain : 0;
    job.fade = fade;
    job.seed = seed;
    job.speck = flaws[level].speck; job.speck_run = flaws[level].speck_run; job.drop_gap = flaws[level].drop_gap;
    full = (1u << n) - 1;
    for (a = 0; a <= full; a++)
        for (b = 0; b <= full; b++) {
            int ya, ua, va, yb, ub, vb;
            film_riso_colour(data, s, a, &ya, &ua, &va);
            film_riso_colour(data, s, b, &yb, &ub, &vb);
            job.pair[(full ^ a) | (full ^ b) << n] = (unsigned)((ua + ub) >> 1 & 255) | (unsigned)ya << 8 |
                                                     (unsigned)((va + vb) >> 1 & 255) << 16 | (unsigned)yb << 24;
        }
    for (k = 0; k < FILM_RISO_INKS; k++) {
        /* Ink 0 is the reference. The others: out of register by the Register
         * row's distance in a random direction (downward: they read ahead),
         * the third a little further; turned by the flaws' skew either way. */
        static const signed char dirc[16] = {127, 125, 117, 106, 90, 71, 49, 25, 0, -25, -49, -71, -90, -106, -117, -125};
        static const signed char dirs[16] = {0, 25, 49, 71, 90, 106, 117, 125, 127, 125, 117, 106, 90, 71, 49, 25};
        int r = reg_px[s->reg] * (k == 2 ? 9 : 8) / 8 * scale >> 10, dir = (int)rnd(&rs, 0, 15), drift;
        job.sep[k] = sep + k * SEP_BYTES;
        job.screen[k] = screens + k * (FILM_RISO_TILE * FILM_RISO_TILE / 2);
        job.streak[k] = 0;
        job.dx[k] = job.dy[k] = job.skew[k] = 0;
        /* Riso sets: every ink but the first; the styles say which. */
        if (k == 0 || k >= n || (s->style && !(rec[S_REG] >> k & 1))) continue;
        job.dx[k] = (r * dirc[dir]) >> 7; job.dy[k] = (r * dirs[dir]) >> 7;
        job.skew[k] = flaws[level].skew * (k == 2 ? 2 : 3) / 3 * (rnd(&rs, 0, 1) ? 1 : -1);
        drift = ((job.skew[k] < 0 ? -job.skew[k] : job.skew[k]) * (int)(width >> 1) >> 16) + 1;
        if (job.dy[k] < drift) job.dy[k] = drift;                  /* turning about the centre never reads above */
    }
    if (flaws[level].streak)
        for (k = 0; k < n; k++) {
            make_streak(streak + k * ((nb + 3) & ~3u), (int)nb, scale, level, &rs);
            job.streak[k] = streak + k * ((nb + 3) & ~3u);
        }
    if (n == 1) run(frame, width, height, stride, &job, dens, progress, 1);
    else if (n == 2) run(frame, width, height, stride, &job, dens, progress, 2);
    else run(frame, width, height, stride, &job, dens, progress, 3);
    if (progress) progress(height, height);
    return 0;
}

/* ---- Live view ------------------------------------------------------------ */
void film_riso_live_sample(const unsigned char *view, unsigned width, unsigned height, unsigned rowbytes,
                           unsigned cw, unsigned ch, int mirror, unsigned char *yuv)
{
    /* The viewport is uncached, so every read is a bus trip: each 4-pixel
     * group's chroma (one U, one V) is read once per row it is used on and
     * shared by the picture pixels in it; luma once per picture pixel. Out:
     * the luma plane (for the levels), then each pixel's table index
     * (rawU >> 4 << 9 | rawV >> 4 << 5 | Y >> 3), the separation tables'
     * own layout. Columns are worked out once per call. */
    static const unsigned char yo[4] = {1, 3, 4, 5};
    static unsigned short yoff[FILM_RISO_LIVE_W], grp[FILM_RISO_LIVE_W];
    static unsigned short cidx[FILM_RISO_LIVE_W];          /* chroma part of a group's index, per group in the row */
    unsigned char *py = yuv;
    unsigned short *pi = (unsigned short *)(yuv + cw * ch);
    unsigned gx, gy, step, fx, ngroups = 0, last = ~0u;
    if (cw > FILM_RISO_LIVE_W || ch > FILM_RISO_LIVE_H || !cw || !ch) return;
    step = (width << 16) / cw;
    for (gx = 0, fx = step >> 1; gx < cw; gx++, fx += step) {
        unsigned sx = fx >> 16, o = mirror ? cw - 1 - gx : gx;
        yoff[o] = (unsigned short)(6 * (sx >> 2) + yo[sx & 3]);
        if ((sx >> 2) != last) { last = sx >> 2; ngroups++; }
        grp[o] = (unsigned short)(ngroups - 1);
        cidx[ngroups - 1] = (unsigned short)(6 * last);           /* the group's U byte, for now */
    }
    step = (height << 16) / ch;
    for (gy = 0, fx = step >> 1; gy < ch; gy++, fx += step) {
        const unsigned char *r = view + (size_t)(fx >> 16) * rowbytes;
        unsigned short ci[FILM_RISO_LIVE_W];
        unsigned char *oy = py + gy * cw;
        unsigned short *oi = pi + gy * cw;
        for (gx = 0; gx < ngroups; gx++) { const unsigned char *g = r + cidx[gx]; ci[gx] = (unsigned short)((g[0] >> 4) << 9 | (g[2] >> 4) << 5); }
        for (gx = 0; gx < cw; gx++) { unsigned y = r[yoff[gx]]; oy[gx] = (unsigned char)y; oi[gx] = (unsigned short)(ci[grp[gx]] | y >> 3); }
    }
}

void film_riso_live_levels(const unsigned char *yuv, unsigned n, int fresh, int *lo, int *hi)
{
    static unsigned hist[256];
    unsigned i, c, v;
    int tlo, thi;
    for (v = 0; v < 256; v++) hist[v] = 0;
    for (i = 0; i < n; i += 2) hist[yuv[i]]++;           /* every other sample is plenty */
    n = (n + 1) / 2;
    for (c = 0, v = 0; v < 255 && (c += hist[v]) * 100 < n; v++) ;
    tlo = (int)v;
    for (c = 0, v = 255; v > 0 && (c += hist[v]) * 100 < n; v--) ;
    thi = (int)v;
    if (thi < tlo + 16) { int m = (tlo + thi) >> 1; tlo = m - 8 < 0 ? 0 : m - 8; thi = tlo + 16; }
    if (fresh) { *lo = tlo; *hi = thi; return; }
    *lo += (tlo - *lo) / 4; *hi += (thi - *hi) / 4;
}

/* One row of masks for n inks: each ink reads its separation at its
 * register offset and compares with its screen row; no branch per pixel. */
AI void live_row(unsigned char *out, const unsigned short *const src[FILM_RISO_INKS],
                 const unsigned char *const sep[FILM_RISO_INKS], const unsigned char *const trow[FILM_RISO_INKS],
                 const unsigned tmask[FILM_RISO_INKS], const unsigned tofs[FILM_RISO_INKS], unsigned x0, unsigned x1, const unsigned n)
{
    unsigned x;
    for (x = x0; x < x1; x++) {
        unsigned m = sep[0][src[0][x]] > trow[0][(x + tofs[0]) & tmask[0]];
        if (n > 1) m |= (unsigned)(sep[1][src[1][x]] > trow[1][(x + tofs[1]) & tmask[1]]) << 1;
        if (n > 2) m |= (unsigned)(sep[2][src[2][x]] > trow[2][(x + tofs[2]) & tmask[2]]) << 2;
        out[x] = (unsigned char)m;
    }
}

/* Text mode, live: 8 x 16 cells on the live picture (40 x 15 at 320 x 240). */
static void live_text(const unsigned char *yuv, unsigned cw, unsigned ch, const unsigned char *data,
                      const film_riso_settings *s, int lo, int hi, unsigned char *canvas)
{
    const unsigned char *py = yuv, *gamma = data + D_GAMMA, *glyphs = data + D_GLYPHS;
    const unsigned short *idx = (const unsigned short *)(yuv + cw * ch);
    const int colour = set_rec(data, film_riso_set(s))[S_FLAGS] & F_TEXTCOL;
    unsigned cx, cy, x, y;
    int span = hi - lo > 0 ? hi - lo : 1;
    for (cy = 0; cy + 16 <= ch; cy += 16)
        for (cx = 0; cx + 8 <= cw; cx += 8) {
            unsigned sum = 0, g, col = 1;
            int t;
            for (y = cy + 2; y < cy + 16; y += 4) for (x = cx; x < cx + 8; x += 2) sum += py[y * cw + x];
            t = ((int)(sum >> 4) - lo) * 255 / span;                 /* 16 samples */
            t = gamma[t < 0 ? 0 : t > 255 ? 255 : t];
            g = gamma[t] * 15 / 255;
            if (colour) {
                unsigned i = idx[(cy + 8) * cw + cx + 4], k;
                int u = (int)((((i >> 9) & 15) ^ 8) << 4) - 128 + 8, v = (int)((((i >> 5) & 15) ^ 8) << 4) - 128 + 8, best = 0x7fffffff;
                for (k = 1; k < 8; k++) {
                    int yy, uu, vv, d;
                    film_riso_colour(data, s, k, &yy, &uu, &vv);
                    d = (u * 4 - uu) * (u * 4 - uu) + (v * 4 - vv) * (v * 4 - vv);
                    if (d < best) { best = d; col = k; }
                }
            }
            for (y = 0; y < 16; y++) {
                unsigned bits = glyphs[g * 16 + y];
                for (x = 0; x < 8; x++) canvas[(cy + y) * cw + cx + x] = (unsigned char)(bits >> (7 - x) & 1 ? col : 0);
            }
        }
    for (y = (ch / 16) * 16; y < ch; y++) for (x = 0; x < cw; x++) canvas[y * cw + x] = 0;
    for (y = 0; y < (ch / 16) * 16; y++) for (x = (cw / 8) * 8; x < cw; x++) canvas[y * cw + x] = 0;
}

void film_riso_live_render(const unsigned char *yuv, unsigned cw, unsigned ch, const unsigned char *data,
                           const film_riso_settings *s, int lo, int hi, unsigned char *sep, unsigned char *canvas)
{
    const unsigned short *idx = (const unsigned short *)(yuv + cw * ch);
    const unsigned char *rec = set_rec(data, film_riso_set(s));
    unsigned n = rec[0], k, y;
    unsigned dx[FILM_RISO_INKS] = {0, 1, 0}, dy[FILM_RISO_INKS] = {0, 1, 2}, tmask[FILM_RISO_INKS], tofs[FILM_RISO_INKS];
    const unsigned char *seps[FILM_RISO_INKS], *tile[FILM_RISO_INKS];
    unsigned tstride[FILM_RISO_INKS], tymask[FILM_RISO_INKS];
    if (!n) { live_text(yuv, cw, ch, data, s, lo, hi, canvas); return; }
    if (!s->reg) dx[1] = dy[1] = dy[2] = 0;
    else if (s->reg >= 2) { dx[1] = 2; dy[2] = 3; }
    for (k = 1; k < FILM_RISO_INKS; k++) if (s->style && !(rec[S_REG] >> k & 1)) dx[k] = dy[k] = 0;
    make_sep(data, s, lo, hi, sep);
    for (k = 0; k < FILM_RISO_INKS; k++) {
        unsigned id = k < n ? screen_id(rec, s, k) : SCR_GRAIN;
        seps[k] = sep + (k < n ? k : 0) * SEP_BYTES;
        tofs[k] = 0; tstride[k] = FILM_RISO_TILE; tmask[k] = tymask[k] = FILM_RISO_TILE - 1;
        switch (id) {
        case SCR_FLAT: tile[k] = data + D_FLAT; break;
        case SCR_ENGRAVE: tile[k] = data + D_ENGRAVE; break;
        case SCR_WOOD: tile[k] = data + D_WOOD; break;
        case SCR_GRAIN: tile[k] = data + D_GRAIN + k * FILM_RISO_TILE * FILM_RISO_TILE; break;
        default:                                       /* dots (the live view's smaller ones) */
            if (k == 0) { tile[k] = data + D_LDOT45; tstride[k] = 8; tmask[k] = tymask[k] = 7; }
            else { tile[k] = data + D_LDOT0; tstride[k] = 4; tmask[k] = tymask[k] = 3; tofs[k] = k == 2 ? 2 : 0; }
        }
    }
    for (y = 0; y < ch; y++) {
        const unsigned short *src[FILM_RISO_INKS];
        const unsigned char *trow[FILM_RISO_INKS];
        unsigned xs = cw, x;
        for (k = 0; k < FILM_RISO_INKS; k++) {
            unsigned sy = y + dy[k] < ch ? y + dy[k] : ch - 1;
            src[k] = idx + sy * cw + dx[k];
            trow[k] = tile[k] + ((y + tofs[k]) & tymask[k]) * tstride[k];
            if (k < n && cw - dx[k] < xs) xs = cw - dx[k];
        }
        /* Columns whose shifted ink would read past the right edge: that ink
         * takes the last column, as the photo's drums do. */
        if (n == 1) live_row(canvas + y * cw, src, seps, trow, tmask, tofs, 0, xs, 1);
        else if (n == 2) live_row(canvas + y * cw, src, seps, trow, tmask, tofs, 0, xs, 2);
        else live_row(canvas + y * cw, src, seps, trow, tmask, tofs, 0, xs, 3);
        for (x = xs; x < cw; x++) {
            unsigned m = 0;
            for (k = 0; k < n; k++) {
                unsigned sx = x + dx[k] < cw ? x : cw - 1 - dx[k];
                m |= (unsigned)(seps[k][src[k][sx]] > trow[k][(x + tofs[k]) & tmask[k]]) << k;
            }
            canvas[y * cw + x] = (unsigned char)m;
        }
    }
    if (rec[S_FLAGS] & F_KEY) {
        /* Keylines: neighbours whose luma (after the levels) differ, in key. */
        const unsigned char *py = yuv, *gamma = data + D_GAMMA;
        unsigned char lum[256];
        int span = hi - lo > 0 ? hi - lo : 1, dk = rec[S_FLAGS] & F_DARKKEY ? 40 : 0;
        unsigned x;
        for (x = 0; x < 256; x++) { int t = ((int)x - lo) * 255 / span; lum[x] = gamma[t < 0 ? 0 : t > 255 ? 255 : t]; }
        for (y = 1; y < ch; y++)
            for (x = 1; x < cw; x++) {
                int l = lum[py[y * cw + x]], a = lum[py[y * cw + x - 1]], b = lum[py[(y - 1) * cw + x]];
                if (l - a > 40 || a - l > 40 || l - b > 40 || b - l > 40 || l < dk) canvas[y * cw + x] = 7;
            }
    }
}

void film_riso_live_paint(const unsigned char *canvas, unsigned cw, unsigned ch, const unsigned char index[8],
                          unsigned char *overlay, unsigned stride, unsigned cols, unsigned rows)
{
    static unsigned short psrc[480];
    unsigned r, k, p, pairs = cols / 2;
    unsigned short val[8];
    if (cw > FILM_RISO_LIVE_W || !cw || !ch || pairs > 480) return;
    /* Whole screen pixels (two equal bytes each), as ZrnoBoy paints: only
     * pairs survive the swung-round screen's mirror intact. So each pair is
     * one halfword, its canvas column looked up; worked out once per call. */
    for (k = 0, p = 0; k < cw; k++) {
        unsigned end = ((k + 1) * pairs + cw / 2) / cw;      /* the pairs column k covers end before this */
        for (; p < end; p++) psrc[p] = (unsigned short)k;
    }
    for (k = 0; k < 8; k++) val[k] = (unsigned short)(index[k] | index[k] << 8);
    for (r = 0; r < rows; r++) {
        const unsigned char *c = canvas + (r * ch / rows) * cw;
        unsigned short *d = (unsigned short *)(overlay + (size_t)r * stride);
        if (r && r * ch / rows == (r - 1) * ch / rows) {
            const unsigned short *prev = (const unsigned short *)(overlay + (size_t)(r - 1) * stride);
            for (p = 0; p < pairs; p++) d[p] = prev[p];
            continue;
        }
        for (p = 0; p < pairs; p++) d[p] = val[c[psrc[p]] & 7];
    }
}
