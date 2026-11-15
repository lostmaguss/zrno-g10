/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe (G12 1.00G only): what frame memory
 * traffic costs, and whether BltDmac can hide it.
 *
 * Zrno's passes work on Canon's frame through the uncached alias
 * (0x4238a730). The colour pass copies 1 KiB chunks into cached scratch and
 * back; Bloom, soft focus and grain read and write the frame uncached. This
 * probe measures, on a buffer of its own in an unused exmem pool:
 *   - uncached word writes and reads, Canon's memcpy in and out of cached
 *     scratch, and reads through the cached alias after a range flush;
 *   - an in-place pass (x ^= KEY plus W synthetic work iterations per word)
 *     four ways: uncached direct, cached alias, the current row cache, and a
 *     double-buffered BltDmac pipeline (DMA in and out on two channels while
 *     the CPU works on the previous chunk), next to the compute-only floor;
 *   - BltDmac: which of its three modes copies RAM to RAM (guarded), and its
 *     throughput by transfer size.
 * Every pass is checked word for word through the uncached alias.
 *
 * BltDmac (0xC0A10000, 8 channels; Canon uses 0 angel table, 1 colour
 * table, 2 lens MCU), from the ROM:
 *   0xff94ddd8 alloc(ch, mode, cb, arg) -> slot, or 31 if busy
 *   0xff94de78 start(slot, dst, src, bytes): program (powers the block, one
 *              reference per transfer), then sets bit 0 of +0x08
 *   0xff94debc power down: drops that reference (takes a semaphore, so it
 *              is called from the task, never from the callback)
 *   0xff94de3c free(slot)
 *   The interrupt clears +0x14/+0x10/+0x04/+0x00 and calls cb(arg).
 * Argument order checked against Canon's colour-table upload (0xff9401ac:
 * dst 0xC0F20000, src [0x6748+8], 4096 bytes). Addresses are passed as
 * uncached RAM addresses, as Canon does.
 *
 * Run in playback mode. Over USB:
 *   lua 'return call_func_ptr(MODULE_RUN, "dgmem.flt")'
 * (MODULE_RUN for the installed core; see USB.md). It runs for about 20 s,
 * appends to A/CHDK/FILM/DGMEM.TXT line by line, and frees its memory.
 * Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "module_def.h"
#include "clock.h"
#include "exmem.h"

#define LOG "A/CHDK/FILM/DGMEM.TXT"
#define FRAME_BYTES (3648u * 2736u * 2u)
#define LINE 32u
#define CHUNK 1024u
#define TEST_MAX (4u << 20)
#define TEST_MIN (1u << 20)
#define DMA_MAX (64u << 10)
#define GUARD (16u << 10)
#define SBUF 4096u
#define POOL_EXTRA (DMA_MAX + 2 * GUARD + 6 * SBUF + 4 * GUARD + 256)
#define KEY 0x5a5aa5a5u
#define MARK 0xa5a5a5a5u
#define DMA_TIMEOUT_MS 200

typedef void *(*bd_alloc_t)(int ch, int mode, void (*cb)(int), int arg);
typedef void (*bd_start_t)(void *slot, unsigned dst, unsigned src, unsigned bytes);
typedef void (*bd_void_t)(void);
typedef void (*bd_free_t)(void *slot);
#define BD_ALLOC ((bd_alloc_t)0xff94ddd8)
#define BD_START ((bd_start_t)0xff94de78)
#define BD_POWER_DOWN ((bd_void_t)0xff94debc)
#define BD_FREE ((bd_free_t)0xff94de3c)
#define BD_REG(ch, off) (*(volatile unsigned *)(0xC0A10000u + ((unsigned)(ch) << 16) + (off)))

static volatile unsigned zero;               /* always 0; keeps the work loop alive */
static volatile int dma_done[8];
static int dma_pending[8];
static int dma_mode = -1, dma_broken, dma_powered, dma_timeouts;
static unsigned uncached_bit;
static unsigned *A_u, *A_c, *D_u;            /* test buffer (both aliases), DMA target */
static unsigned *Sin_u[2], *Sin_c[2], *Sout_u[2], *Sout_c[2], *Srow_c;
static unsigned test_bytes;

/* ---- log ---- */
static void lg(const char *s)
{
    int fd = open(LOG, O_WRONLY | O_CREAT | O_APPEND, 0777);
    if (fd >= 0) { write(fd, s, strlen(s)); close(fd); }
}
static char line[256];
#define LOGF(...) do { sprintf(line, __VA_ARGS__); lg(line); } while (0)

/* ---- CP15 (the module is built ARM) ---- */
#define MRC(op1, crn, crm, op2) ({ unsigned v_; \
    asm volatile("mrc p15, " #op1 ", %0, " #crn ", " #crm ", " #op2 : "=r"(v_)); v_; })
static void drain(void) { asm volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory"); }
static void dc_clean_inv(const void *p, unsigned n)
{
    unsigned a = (unsigned)p & ~(LINE - 1), e = (unsigned)p + n;
    for (; a < e; a += LINE) asm volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(a) : "memory");
    drain();
}
static void dc_clean(const void *p, unsigned n)
{
    unsigned a = (unsigned)p & ~(LINE - 1), e = (unsigned)p + n;
    for (; a < e; a += LINE) asm volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(a) : "memory");
    drain();
}
/* Only for our own line-aligned buffers: discards dirty data. */
static void dc_inv(const void *p, unsigned n)
{
    unsigned a = (unsigned)p, e = a + n;
    for (; a < e; a += LINE) asm volatile("mcr p15, 0, %0, c7, c6, 1" :: "r"(a) : "memory");
}

static void log_cp15(void)
{
    LOGF("cp15 ctrl=%08x cachetype=%08x dcacheable=%02x icacheable=%02x dbufferable=%02x dap=%08x\n",
         MRC(0, c1, c0, 0), MRC(0, c0, c0, 1), MRC(0, c2, c0, 0), MRC(0, c2, c0, 1),
         MRC(0, c3, c0, 0), MRC(0, c5, c0, 2));
    LOGF("cp15 regions %08x %08x %08x %08x %08x %08x %08x %08x dtcm=%08x itcm=%08x\n",
         MRC(0, c6, c0, 0), MRC(0, c6, c1, 0), MRC(0, c6, c2, 0), MRC(0, c6, c3, 0),
         MRC(0, c6, c4, 0), MRC(0, c6, c5, 0), MRC(0, c6, c6, 0), MRC(0, c6, c7, 0),
         MRC(0, c9, c1, 0), MRC(0, c9, c1, 1));
}

/* ---- pattern and checks ---- */
static unsigned pattern(unsigned i) { return (i ^ 0xc0ffee00u) * 2654435761u; }
static void fill_marks(unsigned *u, unsigned bytes) { unsigned i; for (i = 0; i < bytes / 4; i++) u[i] = MARK; }
static int verify(const char *what, unsigned key)
{
    unsigned i, n = test_bytes / 4, bad = 0, first = 0;
    for (i = 0; i < n; i++)
        if (A_u[i] != (pattern(i) ^ key)) { if (!bad) first = i; bad++; }
    LOGF("verify %s bad=%u first=%u\n", what, bad, bad ? first : 0);
    return bad == 0;
}
static int marks_ok(const unsigned *u, unsigned bytes)
{
    unsigned i;
    for (i = 0; i < bytes / 4; i++) if (u[i] != MARK) return 0;
    return 1;
}

/* ---- the synthetic per-word pass ---- */
static inline __attribute__((always_inline)) unsigned work(unsigned x, unsigned w, unsigned z)
{
    unsigned t = x, j;
    for (j = 0; j < w; j++) t = t * 0x9e3779b1u + j;
    return x ^ KEY ^ (t & z);
}
static void work_span(unsigned *dst, const unsigned *src, unsigned words, unsigned w)
{
    unsigned i, z = zero;
    for (i = 0; i < words; i++) dst[i] = work(src[i], w, z);
}

static unsigned ms_since(unsigned t) { return (unsigned)get_tick_count() - t; }
static void report(const char *what, unsigned w, unsigned ms, unsigned bytes)
{
    unsigned frame = ms * (FRAME_BYTES >> 10) / (bytes >> 10);   /* 32-bit: no libgcc division */
    unsigned kbs = ms ? bytes / ms : 0;               /* bytes per ms = kB/s */
    LOGF("%s w=%u ms=%u bytes=%u frame_ms=%u kB_s=%u\n", what, w, ms, bytes, frame, kbs);
}

/* ---- raw bandwidth ---- */
static void bandwidth(void)
{
    unsigned i, n = test_bytes / 4, t, off, s1 = 0, s2 = 0;
    unsigned *s = Srow_c;
    t = get_tick_count();
    for (i = 0; i < n; i++) A_u[i] = pattern(i);
    report("fill_uncached", 0, ms_since(t), test_bytes);

    t = get_tick_count();
    for (i = 0; i < n; i++) s1 += A_u[i];
    report("read_uncached", 0, ms_since(t), test_bytes);

    t = get_tick_count();
    for (off = 0; off < test_bytes; off += CHUNK) memcpy(s, (char *)A_u + off, CHUNK);
    report("copy_in_1k", 0, ms_since(t), test_bytes);
    t = get_tick_count();
    for (off = 0; off < test_bytes; off += SBUF) memcpy(s, (char *)A_u + off, SBUF);
    report("copy_in_4k", 0, ms_since(t), test_bytes);

    /* Copy out writes each chunk back with its own bytes. */
    t = get_tick_count();
    for (off = 0; off < test_bytes; off += CHUNK) {
        memcpy(s, (char *)A_u + off, CHUNK);
        memcpy((char *)A_u + off, s, CHUNK);
    }
    report("copy_in_out_1k", 0, ms_since(t), test_bytes);

    t = get_tick_count();
    dc_clean_inv(A_c, test_bytes);
    report("flush_range", 0, ms_since(t), test_bytes);

    t = get_tick_count();
    for (i = 0; i < n; i++) s2 += A_c[i];
    report("read_cached", 0, ms_since(t), test_bytes);
    LOGF("read_sums uncached=%08x cached=%08x %s\n", s1, s2, s1 == s2 ? "match" : "DIFFER");
    verify("bandwidth", 0);
}

/* ---- the in-place pass, four ways ---- */
static unsigned pass_direct(unsigned w)
{
    unsigned t = get_tick_count();
    work_span(A_u, A_u, test_bytes / 4, w);
    return ms_since(t);
}
static unsigned pass_cached(unsigned w)
{
    unsigned t = get_tick_count();
    dc_clean_inv(A_c, test_bytes);
    work_span(A_c, A_c, test_bytes / 4, w);
    dc_clean(A_c, test_bytes);
    return ms_since(t);
}
static unsigned pass_rowcache(unsigned w)
{
    unsigned off, t = get_tick_count();
    for (off = 0; off < test_bytes; off += CHUNK) {
        memcpy(Srow_c, (char *)A_u + off, CHUNK);
        work_span(Srow_c, Srow_c, CHUNK / 4, w);
        memcpy((char *)A_u + off, Srow_c, CHUNK);
    }
    return ms_since(t);
}
static unsigned compute_only(unsigned w)
{
    unsigned off, t = get_tick_count();
    for (off = 0; off < test_bytes; off += CHUNK) work_span(Srow_c, Srow_c, CHUNK / 4, w);
    return ms_since(t);
}

/* ---- BltDmac ---- */
static void dma_cb(int arg) { dma_done[arg & 7] = 1; }
static void dma_abort(int ch)
{
    BD_REG(ch, 0x14) = 0; BD_REG(ch, 0x10) = 0; BD_REG(ch, 0x04) = 0; BD_REG(ch, 0x00) = 0;
}
static void dma_go(void *slot, int ch, void *dst_u, const void *src_u, unsigned bytes)
{
    dma_done[ch] = 0;
    dma_pending[ch] = 1;
    drain();                     /* uncached stores still in the write buffer */
    BD_START(slot, (unsigned)dst_u, (unsigned)src_u, bytes);
    dma_powered++;
}
/* Waits for channel ch and drops its power reference. On a timeout it stops
 * the channel the way the interrupt does (while the block is still powered;
 * engine registers are never read back) and returns 0. */
static int dma_wait(int ch)
{
    unsigned t = get_tick_count();
    if (!dma_pending[ch]) return 1;
    while (!dma_done[ch])
        if (ms_since(t) > DMA_TIMEOUT_MS) {
            dma_abort(ch);
            BD_POWER_DOWN(); dma_powered--; dma_pending[ch] = 0;
            dma_broken = 1; dma_timeouts++;
            LOGF("dma TIMEOUT ch=%d\n", ch);
            return 0;
        }
    BD_POWER_DOWN(); dma_powered--; dma_pending[ch] = 0;
    return 1;
}
static void *dma_alloc(int mode, int *ch_out)
{
    int ch;
    for (ch = 7; ch >= 3; ch--) {
        void *s = BD_ALLOC(ch, mode, dma_cb, ch);
        if (((unsigned)s & 1) == 0) { *ch_out = ch; return s; }
    }
    return 0;
}

/* Which mode copies RAM to RAM: 256 bytes into the middle of the guarded
 * DMA target, everything else must keep its marks. */
static void dma_modes(void)
{
    int mode, ch;
    unsigned bytes = 256, t;
    unsigned *dst = D_u + GUARD / 4;
    /* Modes 1 and 2 never completed, wedged the block and
     * were followed by a Canon lens error (the lens MCU uses channel 2).
     * Only mode 0 is ever tried now. */
    for (mode = 0; mode < 1; mode++) {
        void *slot = dma_alloc(mode, &ch);
        int done, same, before, after, fixed;
        if (!slot) { LOGF("dma mode=%d no free channel\n", mode); continue; }
        fill_marks(D_u, DMA_MAX + 2 * GUARD);
        LOGF("dma mode=%d ch=%d start\n", mode, ch);
        t = get_tick_count();
        dma_go(slot, ch, dst, A_u, bytes);
        done = dma_wait(ch);
        t = ms_since(t);
        if (done) BD_FREE(slot);   /* a channel that timed out stays allocated, unused */
        same = !memcmp(dst, A_u, bytes);
        before = marks_ok(D_u, GUARD);
        after = marks_ok(dst + bytes / 4, DMA_MAX + GUARD - bytes);
        fixed = dst[0] != MARK && marks_ok(dst + 1, bytes - 4);
        LOGF("dma mode=%d ch=%d done=%d ms=%u copied=%d guard_before=%d guard_after=%d only_first_word=%d dst0=%08x src0=%08x\n",
             mode, ch, done, t, same, before, after, fixed, dst[0], A_u[0]);
        if (done && same && before && after && dma_mode < 0) dma_mode = mode;
    }
    dma_broken = dma_mode < 0;
    LOGF("dma mode_chosen=%d timeouts=%d\n", dma_mode, dma_timeouts);
}

/* Sequential transfers from the test buffer into the DMA target. */
static void dma_throughput(void)
{
    static const unsigned sizes[] = {1024, 4096, 16384, 65536};
    int ch, k;
    void *slot = dma_alloc(dma_mode, &ch);
    if (!slot) { LOGF("dma throughput: no free channel\n"); return; }
    for (k = 0; k < 4 && !dma_broken; k++) {   /* sizes in bytes, all multiples of 32 */
        unsigned off, n = 0, t = get_tick_count();
        for (off = 0; off + sizes[k] <= test_bytes && !dma_broken; off += sizes[k], n++) {
            dma_go(slot, ch, D_u + GUARD / 4, (char *)A_u + off, sizes[k]);
            dma_wait(ch);
        }
        t = ms_since(t);
        sprintf(line, "dma_copy_%uk", sizes[k] / 1024);
        report(line, 0, t, test_bytes);
        LOGF("dma_copy_%uk transfers=%u us_per_transfer=%u\n", sizes[k] / 1024, n, n ? t * 1000u / n : 0);
    }
    if (!dma_broken) BD_FREE(slot);
    LOGF("dma throughput last_copy_ok=%d guards=%d\n",
         !memcmp(D_u + GUARD / 4, (char *)A_u + test_bytes - 65536, 65536),
         marks_ok(D_u, GUARD) && marks_ok(D_u + (GUARD + DMA_MAX) / 4, GUARD));
}

/* Double-buffered: DMA chunk k+1 in and chunk k-1 out while the CPU works
 * on chunk k in cached scratch. */
static unsigned pass_dma(unsigned w)
{
    int chi, cho;
    void *si = dma_alloc(dma_mode, &chi), *so;
    unsigned k, n = test_bytes / CHUNK, t;
    if (!si) return 0;
    so = dma_alloc(dma_mode, &cho);
    if (!so) { BD_FREE(si); return 0; }
    for (k = 0; k < 2; k++) { dc_clean_inv(Sin_c[k], CHUNK); dc_clean_inv(Sout_c[k], CHUNK); }
    t = get_tick_count();
    dma_go(si, chi, Sin_u[0], A_u, CHUNK);
    for (k = 0; k < n && !dma_broken; k++) {
        unsigned b = k & 1;
        if (!dma_wait(chi)) break;
        if (k + 1 < n) dma_go(si, chi, Sin_u[b ^ 1], (char *)A_u + (k + 1) * CHUNK, CHUNK);
        dc_inv(Sin_c[b], CHUNK);
        work_span(Sout_c[b], Sin_c[b], CHUNK / 4, w);
        dc_clean(Sout_c[b], CHUNK);
        if (k && !dma_wait(cho)) break;
        dma_go(so, cho, (char *)A_u + k * CHUNK, Sout_u[b], CHUNK);
    }
    dma_wait(chi); dma_wait(cho);
    t = ms_since(t);
    if (!dma_broken) { BD_FREE(si); BD_FREE(so); }
    return dma_broken ? 0 : t;
}

static void passes(unsigned w)
{
    unsigned key = 0;
    report("pass_compute_only", w, compute_only(w), test_bytes);
    report("pass_direct_uncached", w, pass_direct(w), test_bytes); key ^= KEY; verify("direct", key);
    report("pass_cached_alias", w, pass_cached(w), test_bytes); key ^= KEY; verify("cached", key);
    report("pass_rowcache_1k", w, pass_rowcache(w), test_bytes); key ^= KEY; verify("rowcache", key);
    if (dma_mode >= 0 && !dma_broken) {
        unsigned ms = pass_dma(w);
        if (ms) { report("pass_dma_1k", w, ms, test_bytes); key ^= KEY; verify("dma", key); }
        else LOGF("pass_dma_1k w=%u failed broken=%d\n", w, dma_broken);
    }
    if (key) { work_span(A_u, A_u, test_bytes / 4, 0); key = 0; } /* back to the pattern */
}

/* ---- pool ---- */
static int find_pool(unsigned *type, unsigned need, exmem_alloc_info *ai)
{
    static const char *names[] = {"EXMEM_FIRMUP", "EXMEM_FAMODE", 0};
    unsigned i, n;
    for (i = 0; names[i]; i++)
        for (n = 0; n < exmem_type_count; n++) {
            const char *nm = get_exmem_type_name(n);
            unsigned l = strlen(names[i]);
            if (!strncmp(names[i], nm, l) && (nm[l] == ' ' || !nm[l])) {
                get_exmem_type_status(n, ai);
                if (!ai->len) { *type = n; return 1; }
                break;
            }
        }
    (void)need;
    return 0;
}

int _run()
{
    exmem_alloc_info ai;
    unsigned type = 99, need, k;
    unsigned char *base, *p;
    static const unsigned ws[] = {0, 16, 64};

    LOGF("dgmem v1 tick=%u mode_play=%d\n", (unsigned)get_tick_count(), camera_info.state.mode_play);
    if (!camera_info.state.mode_play) { LOGF("dgmem: run it in playback mode\n"); return 0; }
    uncached_bit = camera_info.cam_uncached_bit;
    log_cp15();
    if (!find_pool(&type, 0, &ai)) { LOGF("dgmem: no free exmem pool\n"); return 0; }
    for (test_bytes = TEST_MAX; test_bytes >= TEST_MIN; test_bytes >>= 1) {
        need = test_bytes + POOL_EXTRA;
        base = exmem_alloc_uncached(type, need, &ai);
        if (base && ai.len >= need) break;
        if (base) exmem_free_uncached(type);
        base = 0;
    }
    if (!base) { LOGF("dgmem: pool %u too small\n", type); return 0; }
    LOGF("pool type=%u %s base=%08x len=%u test_bytes=%u uncached_bit=%08x\n",
         type, get_exmem_type_name(type), (unsigned)base, ai.len, test_bytes, uncached_bit);

    /* Layout: guard | test | guard | DMA target (guard|64K|guard) | Sin0 Sin1 Sout0 Sout1 Srow | guard */
    p = (unsigned char *)(((unsigned)base + 63) & ~63u) + GUARD;
    A_u = (unsigned *)p; p += test_bytes + GUARD;
    D_u = (unsigned *)p; p += DMA_MAX + 2 * GUARD;
    for (k = 0; k < 2; k++) { Sin_u[k] = (unsigned *)p; p += SBUF; }
    for (k = 0; k < 2; k++) { Sout_u[k] = (unsigned *)p; p += SBUF; }
    Srow_c = (unsigned *)((unsigned)p & ~uncached_bit); p += SBUF;
    A_c = (unsigned *)((unsigned)A_u & ~uncached_bit);
    for (k = 0; k < 2; k++) {
        Sin_c[k] = (unsigned *)((unsigned)Sin_u[k] & ~uncached_bit);
        Sout_c[k] = (unsigned *)((unsigned)Sout_u[k] & ~uncached_bit);
    }
    /* Nothing of ours may sit in the cache under the cached aliases. */
    dc_clean_inv((void *)((unsigned)base & ~uncached_bit), need);

    bandwidth();
    /* BltDmac only with the marker present: it shares the block with the
     * lens MCU, and one test was followed by a lens error. */
    if (!stat("A/CHDK/FILM/DGMEMDMA.TXT", 0)) {
        lg("dma probe: mode test begins\n");
        dma_modes();
        if (dma_mode >= 0 && !dma_broken) dma_throughput();
    } else { lg("dma probe: skipped (no DGMEMDMA.TXT)\n"); dma_broken = 1; }
    verify("after_dma_copies", 0);
    for (k = 0; k < sizeof(ws) / sizeof(ws[0]); k++) passes(ws[k]);

    dc_clean_inv((void *)((unsigned)base & ~uncached_bit), need);
    exmem_free_uncached(type);
    LOGF("dgmem done dma_powered=%d dma_broken=%d dma_timeouts=%d tick=%u\n", dma_powered, dma_broken, dma_timeouts, (unsigned)get_tick_count());
    return 0;
}

int _module_can_unload() { return 1; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgmem_sym;
libdgmem_sym _libdgmem = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"DIGIC memory and DMA probe",
    MTYPE_TOOL,
    &_libdgmem.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
