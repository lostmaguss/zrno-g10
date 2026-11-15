/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe R12 (G12 1.00G only): watch a normal
 * capture, read only.
 *
 * R9-R11 found that the develop's engine module 18 needs resource groups
 * 3, 6, 7, 19 and 20, and that in shooting mode unit 0 holds groups 6 and
 * 7 with count 3: live view's path (released by StopEVF) and two holds from
 * WBIntegTask, driven by the live exposure / white balance loop. A real
 * capture must end those holds before its develop. This probe calls no
 * Canon function. It starts a background task that samples, every 10 ms
 * for 30 s:
 *   - for groups 3, 6, 7, 19, 20: a mask of the units with a nonzero count,
 *     and unit 0's count in groups 6 and 7;
 *   - live view: running (0x20ac + 0x174), paused (+0x48), MJPEG (+0x60),
 *     mode (+0x138);
 *   - WBIntegTask state (0x41384, 0x468c: 4 words each);
 *   - SsCreateJpeg state (0x76d8: 3 words), the develop flag (0x823d8),
 *     DevelopPath's module count (byte at 0xaae4) and the stage marker
 *     0x8231c + 0x6c.
 * It keeps a sample only when something other than the time changed, in a
 * ring of the last 800, and ends 8 s after the capture starts (live view
 * stops or unit 0's count changes), or after 40 s. It then writes them to
 * A/CHDK/FILM/DGWATCH.TXT (no card writes during the capture). Ticks match
 * DEVTRACE.BIN.
 *
 * Use: shooting mode, Film processing off. Start "Watch capture (R12)"
 * from the Tools menu, leave the menu, take one normal photo within 30 s,
 * wait until the file is written (about 30 s after the start). Not part of
 * Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "module_def.h"
#include "clock.h"
#include "task.h"

#define RES_CNT   ((volatile unsigned *)0xaa858)   /* engine arbiter: count per group (0x40) and unit (4) */
#define LV_STATE  ((volatile unsigned *)0x20ac)
#define WBI_A     ((volatile unsigned *)0x41384)
#define WBI_B     ((volatile unsigned *)0x468c)
#define SS_STATE  ((volatile unsigned *)0x76d8)
#define DESC      ((volatile unsigned *)0x8231c)
#define DEV_FLAG  (*(volatile unsigned *)0x823d8)
#define DP_COUNT  (*(volatile unsigned char *)0xaae4)

#define SAMPLE_MS  10
#define RUN_MS     40000
#define AFTER_MS   8000                             /* keep watching this long after the capture starts */
#define MAX_KEPT   800
#define NW         25                               /* words per sample after the tick */

static const unsigned char groups[5] = {3, 6, 7, 19, 20};
static volatile int running;
static unsigned *kept;
static int n_kept, n_samples, ring_start, trigger_tick;

static void sample(unsigned *w)
{
    int i, u, k = 0;
    for (i = 0; i < 5; i++) {
        unsigned m = 0;
        for (u = 0; u < 16; u++) if (RES_CNT[groups[i] * 16 + u]) m |= 1u << u;
        w[k++] = m;
    }
    w[k++] = RES_CNT[6 * 16];
    w[k++] = RES_CNT[7 * 16];
    w[k++] = LV_STATE[0x174 / 4];
    w[k++] = LV_STATE[0x48 / 4];
    w[k++] = LV_STATE[0x60 / 4];
    w[k++] = LV_STATE[0x138 / 4];
    for (i = 0; i < 4; i++) w[k++] = WBI_A[i];
    for (i = 0; i < 4; i++) w[k++] = WBI_B[i];
    for (i = 0; i < 3; i++) w[k++] = SS_STATE[i];
    w[k++] = DEV_FLAG;
    w[k++] = DP_COUNT;
    w[k++] = DESC[0x6c / 4];
}

static void lg(int fd, const char *s) { write(fd, s, strlen(s)); }

static void write_log(int t_start)
{
    char t[260];
    int i, j, k, fd = open("A/CHDK/FILM/DGWATCH.TXT", O_WRONLY | O_CREAT | O_APPEND, 0777);
    if (fd < 0) return;
    sprintf(t, "\ndgwatch R12 start=%d capture_seen=%d samples=%d kept=%d%s\n", t_start, trigger_tick, n_samples,
            n_kept, n_kept >= MAX_KEPT ? " (ring wrapped: oldest dropped)" : "");
    lg(fd, t);
    lg(fd, "tick | units g3 g6 g7 g19 g20 | u0 g6 g7 | lv run pause mjpeg mode | wbi_a[4] | wbi_b[4] | ss[3] | devflag dp 6c\n");
    for (i = 0; i < n_kept; i++) {
        unsigned *w = kept + ((ring_start + i) % MAX_KEPT) * (NW + 1);
        k = sprintf(t, "%6u | %04x %04x %04x %04x %04x | %u %u | %u %u %u %u |", w[0], w[1], w[2], w[3], w[4], w[5],
                    w[6], w[7], w[8], w[9], w[10], w[11]);
        for (j = 12; j < 16; j++) k += sprintf(t + k, " %08x", w[j]);
        k += sprintf(t + k, " |");
        for (j = 16; j < 20; j++) k += sprintf(t + k, " %08x", w[j]);
        k += sprintf(t + k, " | %08x %08x %08x | %u %u %08x\n", w[20], w[21], w[22], w[23], w[24], w[25]);
        lg(fd, t);
    }
    lg(fd, "done\n");
    close(fd);
}

static void watch_task(void)
{
    unsigned cur[NW], prev[NW], u0_start = 0;
    int t0 = get_tick_count(), first = 1, i, same, slot;
    while ((int)(get_tick_count() - t0) < RUN_MS &&
           !(trigger_tick && (int)(get_tick_count() - trigger_tick) > AFTER_MS)) {
        sample(cur);
        n_samples++;
        if (first) u0_start = cur[5];
        else if (!trigger_tick && (cur[5] != u0_start || cur[7] == 0)) trigger_tick = get_tick_count();
        same = !first;
        for (i = 0; i < NW && same; i++) if (cur[i] != prev[i]) same = 0;
        if (!same) {
            if (n_kept < MAX_KEPT) slot = n_kept++;
            else { slot = ring_start; ring_start = (ring_start + 1) % MAX_KEPT; }
            {
                unsigned *w = kept + slot * (NW + 1);
                w[0] = get_tick_count();
                for (i = 0; i < NW; i++) w[i + 1] = cur[i];
            }
        }
        for (i = 0; i < NW; i++) prev[i] = cur[i];
        first = 0;
        msleep(SAMPLE_MS);
    }
    write_log(t0);
    free(kept);
    kept = 0;
    running = 0;
    ExitTask();
}

int _run()
{
    if (running) return 0;
    kept = malloc(MAX_KEPT * (NW + 1) * sizeof(unsigned));
    if (!kept) return 0;
    n_kept = n_samples = ring_start = trigger_tick = 0;
    running = 1;
    CreateTask("FilmWatch", 0x18, 0x800, watch_task);
    return 0;
}

int _module_can_unload() { return !running; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgwatch_sym;
libdgwatch_sym _libdgwatch = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"Watch capture (R12)",
    MTYPE_TOOL,
    &_libdgwatch.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
