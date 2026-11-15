/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe (G12 1.00G only): hardware shrink
 * throughput and a single 1/4 step, through Canon's FilteringResizePath
 * (GradateComposite's resize step 0xffab3cac), from CHDK's Tools menu in the
 * gallery. Buffers come from an exmem pool that is not in use (EXMEM_FIRMUP,
 * else EXMEM_FAMODE, as CHDK's file_shelter does) and are freed at once.
 *
 * Source: an unpadded W x H frame (1824 x 1368, or 912 x 684 if the pool is
 * small). Runs, five times each for timing:
 *   H:  1/2 (ratio {64,128,256,512}, args 2,1) into W/2 x H/2
 *   Q1: 1/4 in one step (ratio {32,128,128,512}, args 2,1) into W/4 x H/4
 *   Q2: the same with args 4,2 (what Miniature passes for its last level)
 * Each output sits between marker zones; the log gives how far each was
 * written. Saves the 1/4 outputs and a 64-row crop of the 1/2 output.
 * Only in the gallery with Sharemem already in layout 3. Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "gui.h"
#include "module_def.h"
#include "clock.h"
#include "exmem.h"

#define GAP 8192
#define MARK 0xA5
#define REPS 5

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

static char logbuf[3072];
static int logn;
static void lg(const char *s) { int n = strlen(s); if (logn + n < (int)sizeof(logbuf)) { memcpy(logbuf + logn, s, n); logn += n; } }
static void save(const char *path, const void *p, int n)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd >= 0) { write(fd, p, n); close(fd); }
}
/* first/last non-marker byte of [p - GAP, p + size + GAP), relative to p */
static void extent(const unsigned char *p, int size, const char *name)
{
    char t[120];
    int first = -1, last = -1, i;
    for (i = -GAP; i < size + GAP; i++) if (p[i] != MARK) { first = i; break; }
    for (i = size + GAP - 1; i >= -GAP; i--) if (p[i] != MARK) { last = i; break; }
    if (first == -1 && last == -1) sprintf(t, "%s untouched\n", name);
    else sprintf(t, "%s size %d touched %d..%d\n", name, size, first, last);
    lg(t);
}
static int pattern(int x, int y, int w, int h)
{
    int cx = w / 2, cy = h / 2, s = w / 8;
    if (x > cx - s && x < cx + s && y > cy - s && y < cy + s) return 250;
    if (x < cx) return ((x >> 4) & 1) ? 220 : 40;        /* 16-px stripes */
    return 16 + (((((x - cx) >> 2) & 0xff) * 200) >> 8);  /* repeating ramps; no division */
}

int _run()
{
    static const int rec[6] = {0, 2, 1, 0x20, 0x20, 0x40};
    static const int r_half[5] = {64, 128, 256, 512, 1}, r_quarter[5] = {32, 128, 128, 512, 1};
    char t[160];
    exmem_alloc_info ai;
    unsigned n, type = 99, i;
    int w = 1824, h = 1368, need, ok = 0, k, x, y;
    unsigned char *base = 0, *src, *half, *q1, *q2;
    int th, tq1, tq2, t0;
    logn = 0;
    sprintf(t, "dgquarter play=%d sharemem=%d types=%d\n", camera_info.state.mode_play, SM_CURRENT, exmem_type_count);
    lg(t);
    for (n = 0; n < exmem_type_count; n++) {
        get_exmem_type_status(n, &ai);
        sprintf(t, "exmem %d %s addr %08x len %d\n", n, get_exmem_type_name(n), (unsigned)ai.addr, ai.len);
        lg(t);
    }
    if (!camera_info.state.mode_play || SM_CURRENT != 3) { lg("refused: run it from the gallery (Sharemem layout 3)\n"); goto out; }
    /* pick a pool not in use */
    {
        const char *names[] = {"EXMEM_FIRMUP", "EXMEM_FAMODE", 0};
        for (i = 0; names[i] && type == 99; i++)
            for (n = 0; n < exmem_type_count; n++)
                /* Canon pads the names with spaces: match the prefix, then a space or the end */
                if (!strncmp(names[i], get_exmem_type_name(n), strlen(names[i])) &&
                    (get_exmem_type_name(n)[strlen(names[i])] == ' ' || !get_exmem_type_name(n)[strlen(names[i])])) {
                    get_exmem_type_status(n, &ai);
                    if (!ai.len) type = n;
                    break;
                }
    }
    if (type == 99) { lg("no free pool\n"); goto out; }
    for (k = 0; k < 2 && !ok; k++, w /= 2, h /= 2) {
        need = w * h * 2 + w / 2 * h / 2 * 2 + 2 * (w / 4 * h / 4 * 2) + 5 * GAP + 256;
        base = exmem_alloc_uncached(type, need, &ai);
        sprintf(t, "alloc %s %d bytes for %dx%d -> %08x len %d\n", get_exmem_type_name(type), need, w, h, (unsigned)base, ai.len);
        lg(t);
        if (base && ai.len >= (unsigned)need) ok = 1;
        else if (base) { exmem_free_uncached(type); base = 0; }
    }
    if (!ok) { lg("alloc failed\n"); goto out; }
    w *= 2; h *= 2;                                         /* undo the loop step */
    memset(base, MARK, need);
    src = (unsigned char *)(((unsigned)base + GAP + 63) & ~63u);
    half = src + w * h * 2 + GAP;
    q1 = half + w / 2 * h / 2 * 2 + GAP;
    q2 = q1 + w / 4 * h / 4 * 2 + GAP;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x += 2) {
            unsigned char *q = src + (y * w + x) * 2;
            q[0] = 0; q[1] = pattern(x, y, w, h); q[2] = 0; q[3] = pattern(x + 1, y, w, h);
        }
    save("A/CHDK/FILM/DGQUARTER.TXT", logbuf, logn);        /* survives a crash */
    SM_RESET(0x780);
    MINI_INIT(0xc, 5, 4, 0, 0xc, 5, 0xf, 5, 4, 0);
    MINI_STATE[1] = 1;
    MINI_STATE[2] = 0;
    OPEN(0, 0);
    t0 = get_tick_count();
    for (k = 0; k < REPS; k++) RESIZE(src, half, w, w, h, w / 2, 2, 1, 0, rec, r_half);
    th = get_tick_count() - t0;
    t0 = get_tick_count();
    for (k = 0; k < REPS; k++) RESIZE(src, q1, w, w, h, w / 4, 2, 1, 0, rec, r_quarter);
    tq1 = get_tick_count() - t0;
    t0 = get_tick_count();
    for (k = 0; k < REPS; k++) RESIZE(src, q2, w, w, h, w / 4, 4, 2, 0, rec, r_quarter);
    tq2 = get_tick_count() - t0;
    CLOSE(0, 0);
    SM_FREE(0x780);
    sprintf(t, "W=%d H=%d reps=%d half_ms=%d q1_ms=%d q2_ms=%d (totals)\n", w, h, REPS, th, tq1, tq2);
    lg(t);
    extent(src, w * h * 2, "src");
    extent(half, w / 2 * h / 2 * 2, "half");
    extent(q1, w / 4 * h / 4 * 2, "q1");
    extent(q2, w / 4 * h / 4 * 2, "q2");
    save("A/CHDK/FILM/DGQ1.UYV", q1, w / 4 * h / 4 * 2);
    save("A/CHDK/FILM/DGQ2.UYV", q2, w / 4 * h / 4 * 2);
    save("A/CHDK/FILM/DGHALF.UYV", half, w / 2 * 64 * 2);
out:
    if (base) exmem_free_uncached(type);
    save("A/CHDK/FILM/DGQUARTER.TXT", logbuf, logn);
    return 0;
}

int _module_can_unload() { return 1; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgq_sym;
libdgq_sym _libdgq = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"DIGIC quarter probe",
    MTYPE_TOOL,
    &_libdgq.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
