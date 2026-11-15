/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe (G12 1.00G only): runs Canon's still
 * Miniature pipeline -- hardware pyramid shrink, enlarge and gradient
 * composite -- on a small test image of our own, from CHDK's Tools menu in
 * the gallery. It repeats DevelopModule's sequence (ROM 0xff8e6434..66e8):
 *
 *   (Sharemem layout 3, which the gallery already uses), reset blocks
 *   0x780 (0xff948514), GradateComposite init (0xffab3818, Canon's channel
 *   IDs), still Miniature (0xffab4040), release blocks (0xff9487a4).
 *
 * It runs only in the gallery with Sharemem already in layout 3: in
 * shooting mode live view uses layout 9, and switching it under live view
 * shut the camera down.
 *
 * Every buffer sits inside a larger block filled with a marker byte, so the
 * log shows how far each one was actually read/written (footprint and
 * padding). Writes A/CHDK/FILM/DGPROBE.TXT and the output frame and first
 * pyramid level as raw UYVY (DGOUT.UYV, DGL1.UYV). Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "gui.h"
#include "module_def.h"
#include "clock.h"
#include "meminfo.h"

#define W 192
#define H 128
#define LEVELS 4
#define MARGIN 2048                      /* bytes of marker before and after each buffer */
#define MARK 0xA5

typedef void (*fn_i)(int);
typedef int (*fn_init)(int, int, int, int, int, int, int, int, int, int);
typedef int (*fn_mini)(void *, void *, void **, int, int, int, int, int, int, int, const void *, void *);
#define SM_LAYOUT ((fn_i)0xff9483f4)
#define SM_RESET  ((fn_i)0xff948514)
#define SM_FREE   ((fn_i)0xff9487a4)
#define MINI_INIT ((fn_init)0xffab3818)
#define MINI_RUN  ((fn_mini)0xffab4040)
#define SM_CURRENT (*(volatile int *)(0x6830 + 4))
#define MINI_TABLE ((const void *)0xffbc0e28)
#define BAND_K   ((const int *)0xffbc0d68)   /* per strength x level, non-"1" branch */
#define BAND_LEN ((const int *)0xffbc0ca8)

static char logbuf[2048];
static int logn;
static void lg(const char *s) { int n = strlen(s); if (logn + n < (int)sizeof(logbuf)) { memcpy(logbuf + logn, s, n); logn += n; } }

typedef struct { unsigned char *block, *p; int size; } buf_t;
static buf_t bufs[LEVELS + 2];            /* 0: frame, 1..5: levels[0..4] */

static int alloc_buf(buf_t *b, int size)
{
    char t[80];
    b->size = size;
    b->block = umalloc(size + 2 * MARGIN + 64);
    sprintf(t, "alloc %d -> %08x\n", size + 2 * MARGIN + 64, (unsigned)b->block);
    lg(t);
    if (!b->block) return 0;
    b->p = (unsigned char *)(((unsigned)b->block + MARGIN + 63) & ~63u);
    memset(b->block, MARK, size + 2 * MARGIN + 64);
    return 1;
}

/* First and last byte that differ from the marker, relative to b->p. */
static void extent(const buf_t *b, const char *name)
{
    char t[120];
    int total = b->size + 2 * MARGIN + 64, first = -1, last = -1, i;
    for (i = 0; i < total; i++) if (b->block[i] != MARK) { first = i; break; }
    for (i = total - 1; i >= 0; i--) if (b->block[i] != MARK) { last = i; break; }
    if (first < 0) sprintf(t, "%s untouched\n", name);
    else sprintf(t, "%s at %08x size %d touched %d..%d\n", name, (unsigned)b->p, b->size,
                 first - (int)(b->p - b->block), last - (int)(b->p - b->block));
    lg(t);
}

static void save(const char *path, const unsigned char *p, int n)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd >= 0) { write(fd, p, n); close(fd); }
}

int _run()
{
    char t[160];
    void *levels[LEVELS + 1];
    int bands[LEVELS * 11], i, x, y, w, h, old, t0, t1, t2, rc = -1, ok = 1;
    logn = 0;
    sprintf(t, "dgprobe W=%d H=%d levels=%d play=%d sharemem=%d\n", W, H, LEVELS, camera_info.state.mode_play, SM_CURRENT);
    lg(t);
    if (!camera_info.state.mode_play || SM_CURRENT != 3) {
        lg("refused: run it from the gallery (Sharemem layout 3)\n");
        goto out;
    }
    {
        cam_meminfo m;
        GetMemInfo(&m);
        sprintf(t, "heap free %d largest %d blocks %d\n", m.free_size, m.free_block_max_size, m.free_block_count);
        lg(t);
        if (GetExMemInfo(&m)) {
            sprintf(t, "exmem free %d largest %d\n", m.free_size, m.free_block_max_size);
            lg(t);
        }
    }
    /* frame (in place, as Canon) and the padded pyramid, sized as 0xffab4040
     * lays it out (found by running it in an emulator): level 0 is a
     * (W+92) x (H+46) work copy, then (W/2+44) x (H/2+22), (W/4+20) x
     * (H/4+10), (W/8+8) x (H/8+4), and level 4 about (W/16+8) x (H/16+8). */
    {
        static const int padw[LEVELS + 1] = {92, 44, 20, 8, 8}, padh[LEVELS + 1] = {46, 22, 10, 4, 12};
        ok &= alloc_buf(&bufs[0], W * H * 2);
        for (i = 0, w = W, h = H; i <= LEVELS; i++, w /= 2, h /= 2)
            ok &= alloc_buf(&bufs[i + 1], (w + padw[i]) * (h + padh[i]) * 2 + 256);
    }
    if (!ok) { lg("alloc failed\n"); goto out; }
    for (i = 0; i <= LEVELS; i++) levels[i] = bufs[i + 1].p;
    /* Test image: Y stripes 8 px wide on the left, a horizontal ramp on the right, a
     * white square in the middle; neutral chroma (signed 0). */
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x += 2) {
            unsigned char *q = bufs[0].p + (y * W + x) * 2, v;
            if (x < W / 2) v = ((x / 8) & 1) ? 220 : 40;
            else v = 16 + (((x - W / 2) * (200 * 65536 / (W / 2))) >> 16);   /* no division: modules get no __aeabi_idiv */
            if (x > W / 2 - 32 && x < W / 2 + 32 && y > H / 2 - 32 && y < H / 2 + 32) v = 250;
            q[0] = 0; q[1] = v; q[2] = 0; q[3] = v;
        }
    save("A/CHDK/FILM/DGIN.UYV", bufs[0].p, W * H * 2);
    /* Band records as DevelopModule builds them (horizontal band, strength 1):
     * {x0, y0, x1, y1, k, k, 0, 2, 0, 2, 0x400}, centre halving per level. */
    {
        int centre = (H / 2 + 0x10) & ~0x1f, s = 1;
        for (i = 0; i < LEVELS; i++) {
            /* Canon's band lengths are for 2736 rows; scale to a middle third
             * of the test image so the top and bottom are blurred. */
            int *r = bands + 11 * i, half = (H / 6) >> i, lim = (H >> i) - 1;
            r[0] = 0; r[2] = W - 1;
            r[1] = centre - half - 1 >= 0 ? centre - half - 1 : 0;
            r[3] = centre + half - 1 < lim ? centre + half - 1 : lim;
            r[4] = r[5] = BAND_K[s * 4 + i];
            r[6] = 0; r[7] = 2; r[8] = 0; r[9] = 2; r[10] = 0x400;
            centre >>= 1;
            sprintf(t, "band%d y %d..%d k %d len %d\n", i, r[1], r[3], r[4], BAND_LEN[s * 4 + i]);
            lg(t);
        }
    }
    /* Write the log so far: if Canon asserts, this survives. */
    save("A/CHDK/FILM/DGPROBE.TXT", (unsigned char *)logbuf, logn);
    old = SM_CURRENT;
    t0 = get_tick_count();
    SM_RESET(0x780);
    MINI_INIT(0xc, 5, 4, 0, 0xc, 5, 0xf, 5, 4, 0);
    t1 = get_tick_count();
    rc = MINI_RUN(bufs[0].p, bufs[0].p, levels, W, W, W, H, LEVELS, 0, 1, MINI_TABLE, bands);
    t2 = get_tick_count();
    SM_FREE(0x780);
    (void)old;
    sprintf(t, "rc=%d setup_ms=%d run_ms=%d sharemem_after=%d\n", rc, t1 - t0, t2 - t1, SM_CURRENT);
    lg(t);
    extent(&bufs[0], "frame");
    for (i = 0; i <= LEVELS; i++) { sprintf(t, "level%d", i); extent(&bufs[i + 1], t); }
    save("A/CHDK/FILM/DGOUT.UYV", bufs[0].p, W * H * 2);
    save("A/CHDK/FILM/DGL1.UYV", bufs[2].p, bufs[2].size);
out:
    save("A/CHDK/FILM/DGPROBE.TXT", (unsigned char *)logbuf, logn);
    for (i = 0; i < LEVELS + 2; i++) if (bufs[i].block) { ufree(bufs[i].block); bufs[i].block = 0; }
    return 0;
}

int _module_can_unload() { return 1; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdg_sym;
libdg_sym _libdg = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"DIGIC Miniature probe",
    MTYPE_TOOL,
    &_libdg.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
