/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe (G12 1.00G only): the hardware 1/4
 * shrink on a real capture. Uses the film stage's research hook
 * (film_stage_probe), called in Canon's develop task with the developed
 * 3648 x 2736 frame right before the colour pass -- the task and point where
 * Canon runs its still Miniature.
 *
 * From CHDK's Tools menu:
 *   1st run (shooting mode): takes an exmem pool that is not in use
 *     (EXMEM_FIRMUP, else EXMEM_FAMODE) and arms the hook. Take one photo
 *     with Film processing On.
 *   In that capture the hook (no file access) selects Sharemem layout 3 as
 *     DevelopModule does for Miniature, resets blocks 0x780, inits and opens
 *     the paths, shrinks the frame to 912 x 684 twice (filter arguments 2,1
 *     and 4,2), closes, releases, restores the previous layout, and disarms.
 *   2nd run: writes A/CHDK/FILM/DGCAP.TXT and the two 1/4 images
 *     (DGCQ1.UYV, DGCQ2.UYV), frees the pool. A 2nd run without a shot only
 *     disarms. Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "gui.h"
#include "module_def.h"
#include "clock.h"
#include "exmem.h"
#include "gui_mbox.h"

#define FW 3648
#define FH 2736
#define QW (FW / 4)
#define QH (FH / 4)
#define QBYTES (QW * QH * 2)
#define GAP 8192
#define MARK 0xA5

typedef void (*fn_i)(int);
typedef void (*fn_ii)(int, int);
typedef int (*fn_init)(int, int, int, int, int, int, int, int, int, int);
typedef void (*fn_resize)(void *, void *, int, int, int, int, int, int, int, const int *, const int *);
#define SM_LAYOUT ((fn_i)0xff9483f4)
#define SM_RESET  ((fn_i)0xff948514)
#define SM_FREE   ((fn_i)0xff9487a4)
#define MINI_INIT ((fn_init)0xffab3818)
#define OPEN      ((fn_ii)0xffab3f84)
#define CLOSE     ((fn_ii)0xffab4d34)
#define RESIZE    ((fn_resize)0xffab3cac)
#define SM_CURRENT (*(volatile int *)(0x6830 + 4))
#define MINI_STATE ((volatile int *)0xb19c)

extern void (*film_stage_probe)(unsigned char *frame);

enum { IDLE, ARMED, DONE };
static volatile int state = IDLE;
static unsigned type = 99;
static unsigned char *base, *q1, *q2;
static volatile int t_setup, t_q1, t_q2, sm_before, sm_after, calls;
static char msg[200];
static volatile int showing;                            /* keep loaded while the box shows msg */
static void shown(unsigned int btn) { (void)btn; showing = 0; }

/* In Canon's develop task, during the capture: no file access, no waits
 * beyond the engines' own. */
static void hook(unsigned char *frame)
{
    static const int rec[6] = {0, 2, 1, 0x20, 0x20, 0x40};
    static const int r_quarter[5] = {32, 128, 128, 512, 1};
    int t0 = get_tick_count(), t1, t2;
    film_stage_probe = 0;                                   /* one shot */
    calls++;
    sm_before = SM_CURRENT;
    SM_LAYOUT(3);
    SM_RESET(0x780);
    MINI_INIT(0xc, 5, 4, 0, 0xc, 5, 0xf, 5, 4, 0);
    MINI_STATE[1] = 1;
    MINI_STATE[2] = 0;
    OPEN(0, 0);
    t1 = get_tick_count();
    RESIZE(frame, q1, FW, FW, FH, QW, 2, 1, 0, rec, r_quarter);
    t2 = get_tick_count();
    RESIZE(frame, q2, FW, FW, FH, QW, 4, 2, 0, rec, r_quarter);
    t_q2 = get_tick_count() - t2;
    t_q1 = t2 - t1;
    CLOSE(0, 0);
    SM_FREE(0x780);
    SM_LAYOUT(sm_before);
    sm_after = SM_CURRENT;
    t_setup = t1 - t0;
    state = DONE;
}

static void save(const char *path, const void *p, int n)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd >= 0) { write(fd, p, n); close(fd); }
}
static int extent_last(const unsigned char *p, int size)
{
    int i;
    for (i = size + GAP - 1; i >= -GAP; i--) if (p[i] != MARK) return i;
    return -GAP - 1;
}
static int extent_first(const unsigned char *p, int size)
{
    int i;
    for (i = -GAP; i < size + GAP; i++) if (p[i] != MARK) return i;
    return size + GAP;
}
static void say(const char *s)
{
    if (s != msg) strcpy(msg, s);
    showing = 1;
    gui_mbox_init((int)"Capture probe", (int)msg, MBOX_BTN_OK | MBOX_TEXT_CENTER, shown);
}
static void release(void)
{
    film_stage_probe = 0;
    if (base) exmem_free_uncached(type);
    base = 0;
    state = IDLE;
}

int _run()
{
    char t[200];
    exmem_alloc_info ai;
    unsigned n, i;
    if (state == DONE) {
        char log[600];
        int k = sprintf(log, "dgcap calls=%d sharemem before=%d after=%d setup_ms=%d q1_ms=%d q2_ms=%d\n",
                        calls, sm_before, sm_after, t_setup, t_q1, t_q2);
        k += sprintf(log + k, "q1 size %d touched %d..%d\nq2 size %d touched %d..%d\n",
                     QBYTES, extent_first(q1, QBYTES), extent_last(q1, QBYTES),
                     QBYTES, extent_first(q2, QBYTES), extent_last(q2, QBYTES));
        save("A/CHDK/FILM/DGCAP.TXT", log, k);
        save("A/CHDK/FILM/DGCQ1.UYV", q1, QBYTES);
        save("A/CHDK/FILM/DGCQ2.UYV", q2, QBYTES);
        release();
        sprintf(t, "Saved: 1/4 shrink %d ms and %d ms (setup %d ms). DGCAP.TXT in CHDK/FILM.", t_q1, t_q2, t_setup);
        say(t);
        return 0;
    }
    if (state == ARMED) { release(); say("Disarmed; no capture was probed."); return 0; }
    if (!camera_info.state.mode_rec) { say("Arm it in shooting mode, then take one photo with Film processing On."); return 0; }
    {
        const char *names[] = {"EXMEM_FIRMUP", "EXMEM_FAMODE", 0};
        for (i = 0; names[i] && type == 99; i++)
            for (n = 0; n < exmem_type_count; n++) {
                const char *nm = get_exmem_type_name(n);
                unsigned l = strlen(names[i]);
                if (!strncmp(names[i], nm, l) && (nm[l] == ' ' || !nm[l])) {
                    get_exmem_type_status(n, &ai);
                    if (!ai.len) type = n;
                    break;
                }
            }
    }
    if (type == 99) { say("No free exmem pool in shooting mode."); return 0; }
    base = exmem_alloc_uncached(type, 2 * QBYTES + 3 * GAP + 128, &ai);
    if (!base || ai.len < (unsigned)(2 * QBYTES + 3 * GAP + 128)) {
        if (base) exmem_free_uncached(type);
        base = 0; type = 99;
        say("Could not get the memory for the probe.");
        return 0;
    }
    memset(base, MARK, 2 * QBYTES + 3 * GAP + 128);
    q1 = (unsigned char *)(((unsigned)base + GAP + 63) & ~63u);
    q2 = q1 + QBYTES + GAP;
    calls = 0;
    state = ARMED;
    film_stage_probe = hook;
    say("Armed. Take one photo with Film processing On, then run the probe again to save the result.");
    return 0;
}

int _module_can_unload() { return state == IDLE && !showing; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgc_sym;
libdgc_sym _libdgc = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"DIGIC capture probe",
    MTYPE_TOOL,
    &_libdgc.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
