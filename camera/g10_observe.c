// SPDX-License-Identifier: GPL-2.0-or-later
/* G10 1.03B, first camera build: observe only. The develop task copy
 * (film_reloc.c, MAGUS_G10) calls these hooks; they only copy a few words
 * into a ring of records in RAM. No file I/O, no pixel is changed, nothing
 * of Canon's is written. G10OBS.LUA finds the log by its magic and saves it.
 * See docs/PORT-G10.md. */
#include "film_reloc.h"
#include "film_stage.h"

extern long get_tick_count(void);

#define G10_MAGIC   0x3031474du          /* "MG10" */
#define G10_RECORDS 96
#define G10_WORDS   16

/* Words saved with each record (develop job, develop state, the frame). */
static const unsigned watch[G10_WORDS] = {
    0x210ac, 0x210b0, 0x210b4, 0x210b8, 0x210bc, 0x210c0, 0x210c4, 0x210d8,
    0x210dc, 0x210e0, 0x21074, 0x21078, 0x6ee8, 0x6eec,
    0x42902100u + 1656u * 8832u + 4416u,          /* frame centre */
    0x42902100u + 400u * 8832u + 1000u,           /* frame, upper left */
};

typedef struct {
    unsigned tick, phase, type, seq;      /* phase 0 = message (type = msg[0]) */
    unsigned w[G10_WORDS];
} g10_record;

struct {
    unsigned magic, version, count, reloc_status, records, words;
    unsigned watch[G10_WORDS];
    g10_record rec[G10_RECORDS];
} magus_g10_log = { G10_MAGIC, 1, 0, 0, G10_RECORDS, G10_WORDS, {0}, {{0}} };

static void add(unsigned phase, const unsigned *msg)
{
    unsigned n = magus_g10_log.count, i;
    g10_record *r = &magus_g10_log.rec[n % G10_RECORDS];
    r->tick = (unsigned)get_tick_count();
    r->phase = phase;
    r->type = msg ? msg[0] : 0;
    r->seq = n;
    if (msg)
        for (i = 0; i < G10_WORDS; i++) r->w[i] = msg[i];
    else
        for (i = 0; i < G10_WORDS; i++) r->w[i] = *(volatile unsigned *)watch[i];
    magus_g10_log.reloc_status = (unsigned)film_reloc_status;
    magus_g10_log.count = n + 1;
}

static unsigned last_type;

void film_develop_trace(const unsigned *msg)
{
    if (!magus_g10_log.watch[0]) {
        unsigned i;
        for (i = 0; i < G10_WORDS; i++) magus_g10_log.watch[i] = watch[i];
    }
    last_type = msg[0];
    add(0, msg);
}

#if defined(MAGUS_G10_SQUARE) || defined(MAGUS_G10_GRAIN)
#define ROW_BYTES  8832u
/* The G10 alternates two raw buffers, each developed into its own L frame
 * (uncached addresses, seen on the camera). */
static unsigned l_frame(unsigned src, unsigned dst)
{
    if (src == 0x43164d80u && dst == 0x42902100u) return dst;
    if (src == 0x413245a0u && dst == 0x41516720u) return dst;
    return 0;
}
#endif

#ifdef MAGUS_G10_SQUARE
/* Test build: right after the RAW -> YC message (type 1) has developed the
 * L frame, invert the luma of a 256 x 256 square at rows 200-455, pixels
 * 200-455. U and V stay. Only when the job names the raw buffer as source
 * and the L frame as destination. Logged as phase 5. */
static int square(void)
{
    unsigned frame = l_frame(*(volatile unsigned *)0x210b4, *(volatile unsigned *)0x210d8), y, x;
    if (!frame) return 0;
    for (y = 200; y < 456; y++) {
        volatile unsigned *row = (volatile unsigned *)(frame + y * ROW_BYTES + 400u);
        for (x = 0; x < 128; x++) row[x] ^= 0xff00ff00u;     /* U Y0 V Y1: flip both Y */
    }
    return 1;
}
#endif

#ifdef MAGUS_G10_GRAIN
/* Grain test build: zrno's film_uyvy_grain on the developed L frame, right
 * after the RAW -> YC message, through the frame's cached alias as the G12
 * stage does (FRAMECACHE): the D-cache is cleaned and invalidated before (no
 * stale line of the frame) and after (every write in RAM before Canon's JPEG
 * engine reads it). Short sleeps every 200 ms keep the other tasks running.
 * Logged as phase 6 with type = (rc << 24) | milliseconds. */
#include "film_grain.h"
extern void msleep(long);
#define G10_W 4416u
#define G10_H 3312u
#define G10_GRAIN_LEVEL 3u                      /* High, to be easy to see */
static unsigned char grain_work[(G10_W / 2 + 1) * 2];
static unsigned last_yield;

static void __attribute__((target("arm"), noinline)) dcache_clean_flush_all(void)
{
    unsigned config, size, seg, i;
    asm volatile("mrc p15, 0, %0, c0, c0, 1" : "=r"(config));
    size = (config >> 18) & 0xfu;
    if (!size) return;
    size = 0x200u << size;
    for (seg = 0; seg < 4; seg++)
        for (i = 0; i < size / 4; i += 32)
            asm volatile("mcr p15, 0, %0, c7, c14, 2" :: "r"((seg << 30) | i) : "memory");
    asm volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory");
}

static void progress(unsigned row, unsigned rows)
{
    (void)row; (void)rows;
    if ((unsigned)get_tick_count() - last_yield >= 200) {
        msleep(10);
        last_yield = (unsigned)get_tick_count();
    }
}

static void grain(void)
{
    unsigned frame = l_frame(*(volatile unsigned *)0x210b4, *(volatile unsigned *)0x210d8);
    unsigned t0, seed;
    int rc;
    if (!frame) return;
    t0 = last_yield = (unsigned)get_tick_count();
    seed = t0 * 0x9e3779b9u ^ magus_g10_log.count;
    dcache_clean_flush_all();
    rc = film_uyvy_grain((unsigned char *)(frame & ~0x40000000u), (size_t)ROW_BYTES * G10_H, G10_W, G10_H,
                         ROW_BYTES, G10_GRAIN_LEVEL, seed, grain_work, sizeof grain_work, progress);
    dcache_clean_flush_all();
    add(6, 0);
    magus_g10_log.rec[(magus_g10_log.count - 1) % G10_RECORDS].type =
        ((unsigned)rc << 24) | (((unsigned)get_tick_count() - t0) & 0xffffffu);
}
#endif

void film_stage_observe(unsigned phase)
{
    add(phase, 0);
#ifdef MAGUS_G10_SQUARE
    if (phase == 2 && last_type == 1 && square()) add(5, 0);
#endif
#ifdef MAGUS_G10_GRAIN
    if (phase == 2 && last_type == 1) grain();
#endif
}
