/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe (G12 1.00G only): one hardware 1/2 shrink
 * through Canon's FilteringResizePath, using GradateComposite's own resize
 * step (0xffab3cac) the way the still Miniature calls it, from CHDK's Tools
 * menu in the gallery:
 *
 *   reset blocks 0x780 (0xff948514), init (0xffab3818, Canon's channel IDs),
 *   open paths (0xffab3f84(0,0)), resize step, close (0xffab4d34(0,0)),
 *   release blocks (0xff9487a4).
 *
 * Run A: a padded source laid out like Miniature's level 0, (W+92) x (H+46)
 * with the image at 23 rows + 46 px and edges extended by the CPU, into a
 * padded half-size buffer (W/2+44) x (H/2+22).
 * Run B: straight from an unpadded W x H frame into a plain W/2 x H/2 buffer,
 * to see whether Canon's own frame could be shrunk without a padded copy.
 * Buffers sit in marker-filled margins; the log gives timings and how far
 * each buffer was touched. Writes A/CHDK/FILM/DGSHRINK.TXT, DGSA.UYV, DGSB.UYV.
 * Only in the gallery with Sharemem already in layout 3. Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "gui.h"
#include "module_def.h"
#include "clock.h"

#define W 192
#define H 128
#define PW (W + 92)
#define PH (H + 46)
#define DW (W / 2 + 44)
#define DH (H / 2 + 22)
#define MARGIN 2048
#define MARK 0xA5

typedef void (*fn_i)(int);
typedef void (*fn_ii)(int, int);
typedef int (*fn_init)(int, int, int, int, int, int, int, int, int, int);
typedef void (*fn_resize)(void *, void *, int, int, int, int, int, int, int, const int *, const int *);
#define SM_RESET  ((fn_i)0xff948514)
#define SM_FREE   ((fn_i)0xff9487a4)
#define MINI_INIT ((fn_init)0xffab3818)
#define OPEN      ((fn_ii)0xffab3f84)
#define CLOSE     ((fn_ii)0xffab4d34)
#define RESIZE    ((fn_resize)0xffab3cac)
#define SM_CURRENT (*(volatile int *)(0x6830 + 4))
#define MINI_STATE ((volatile int *)0xb19c)

static char logbuf[2048];
static int logn;
static void lg(const char *s) { int n = strlen(s); if (logn + n < (int)sizeof(logbuf)) { memcpy(logbuf + logn, s, n); logn += n; } }
static void save(const char *path, const void *p, int n)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd >= 0) { write(fd, p, n); close(fd); }
}

typedef struct { unsigned char *block, *p; int size; } buf_t;
static buf_t bufs[4];                     /* A src, A dst, B src, B dst */

static int alloc_buf(buf_t *b, int size)
{
    char t[64];
    b->size = size;
    b->block = umalloc(size + 2 * MARGIN + 64);
    sprintf(t, "alloc %d -> %08x\n", size + 2 * MARGIN + 64, (unsigned)b->block);
    lg(t);
    if (!b->block) return 0;
    b->p = (unsigned char *)(((unsigned)b->block + MARGIN + 63) & ~63u);
    memset(b->block, MARK, size + 2 * MARGIN + 64);
    return 1;
}
static void extent(const buf_t *b, const char *name)
{
    char t[120];
    int total = b->size + 2 * MARGIN + 64, first = -1, last = -1, i;
    for (i = 0; i < total; i++) if (b->block[i] != MARK) { first = i; break; }
    for (i = total - 1; i >= 0; i--) if (b->block[i] != MARK) { last = i; break; }
    if (first < 0) sprintf(t, "%s untouched\n", name);
    else sprintf(t, "%s size %d touched %d..%d\n", name, b->size, first - (int)(b->p - b->block), last - (int)(b->p - b->block));
    lg(t);
}
/* Test image Y at (x, y), clamped so padding repeats the edges. */
static int pattern(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= W) x = W - 1;
    if (y < 0) y = 0;
    if (y >= H) y = H - 1;
    if (x > W / 2 - 24 && x < W / 2 + 24 && y > H / 2 - 24 && y < H / 2 + 24) return 250;
    if (x < W / 2) return ((x / 8) & 1) ? 220 : 40;
    return 16 + (((x - W / 2) * (200 * 65536 / (W / 2))) >> 16);
}
static void fill(unsigned char *p, int pitch, int rows, int x0, int y0)
{
    int x, y;
    for (y = 0; y < rows; y++)
        for (x = 0; x < pitch; x += 2) {
            unsigned char *q = p + (y * pitch + x) * 2;
            q[0] = 0; q[1] = pattern(x - x0, y - y0); q[2] = 0; q[3] = pattern(x + 1 - x0, y - y0);
        }
}

int _run()
{
    static const int rec[6] = {0, 2, 1, 0x20, 0x20, 0x40};       /* as 0xffab3f28 builds it from 0xffbc0e28 */
    static const int ratio[5] = {64, 128, 256, 512, 1};          /* 1/2 across, 1/2 down */
    char t[160];
    int t0, t1, t2, t3, ok = 1;
    logn = 0;
    sprintf(t, "dgshrink W=%d H=%d play=%d sharemem=%d\n", W, H, camera_info.state.mode_play, SM_CURRENT);
    lg(t);
    if (!camera_info.state.mode_play || SM_CURRENT != 3) { lg("refused: run it from the gallery (Sharemem layout 3)\n"); goto out; }
    ok &= alloc_buf(&bufs[0], PW * PH * 2);
    ok &= alloc_buf(&bufs[1], DW * DH * 2);
    ok &= alloc_buf(&bufs[2], W * H * 2);
    ok &= alloc_buf(&bufs[3], DW * DH * 2);          /* generous for the plain W/2 x H/2 output */
    if (!ok) { lg("alloc failed\n"); goto out; }
    fill(bufs[0].p, PW, PH, 46, 23);
    fill(bufs[2].p, W, H, 0, 0);
    save("A/CHDK/FILM/DGSHRINK.TXT", logbuf, logn);    /* survives a crash */
    t0 = get_tick_count();
    SM_RESET(0x780);
    MINI_INIT(0xc, 5, 4, 0, 0xc, 5, 0xf, 5, 4, 0);
    MINI_STATE[1] = 1;                                 /* as 0xffab4040 sets before its steps */
    MINI_STATE[2] = 0;
    OPEN(0, 0);
    t1 = get_tick_count();
    /* A: exactly the arguments Miniature passes for level 0 -> 1 */
    RESIZE(bufs[0].p, bufs[1].p, PW, PW, PH, DW, 2, 1, 0, rec, ratio);
    t2 = get_tick_count();
    /* B: unpadded frame -> plain half size */
    RESIZE(bufs[2].p, bufs[3].p, W, W, H, W / 2, 2, 1, 0, rec, ratio);
    t3 = get_tick_count();
    CLOSE(0, 0);
    SM_FREE(0x780);
    sprintf(t, "setup_ms=%d A_ms=%d B_ms=%d sharemem_after=%d\n", t1 - t0, t2 - t1, t3 - t2, SM_CURRENT);
    lg(t);
    extent(&bufs[0], "A_src");
    extent(&bufs[1], "A_dst");
    extent(&bufs[2], "B_src");
    extent(&bufs[3], "B_dst");
    save("A/CHDK/FILM/DGSA.UYV", bufs[1].p, bufs[1].size);
    save("A/CHDK/FILM/DGSB.UYV", bufs[3].p, bufs[3].size);
out:
    save("A/CHDK/FILM/DGSHRINK.TXT", logbuf, logn);
    { int i; for (i = 0; i < 4; i++) if (bufs[i].block) { ufree(bufs[i].block); bufs[i].block = 0; } }
    return 0;
}

int _module_can_unload() { return 1; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgs_sym;
libdgs_sym _libdgs = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"DIGIC shrink probe",
    MTYPE_TOOL,
    &_libdgs.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
