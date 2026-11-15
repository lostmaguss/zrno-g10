/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe R3 (G12 1.00G only): run Canon's JPEG
 * develop from the gallery by calling the shooting sequence's own "create the
 * JPEG" stage:
 *
 *   0xff98f3f0()  ->  0xff98e1cc(*(0x76d8 + 0x20) ? 0xc : 2), then (0xa)
 *
 * 0xff98e1cc runs the develop (0xff98e024) when needed and posts the develop
 * task's messages through the develop API (0xff8e740c with the parameter
 * block 0xb3ed4 and the buffer block 0xb3f54), waiting on the shooting
 * sequence's event flag. Afterwards the shot record (*(0x76d8 + 0x10)) holds
 * the JPEG stream and its header at +0x5c / +0x60 and +0x64 / +0x68 (address,
 * size), as after a capture.
 *
 * Camera in JPEG only, after one normal shot: the raw buffer still holds that
 * shot, so the JPEG should look like it. Gallery only. Every step is logged to
 * A/CHDK/FILM/DGJPEG.TXT before the next. If the shot record's pieces change,
 * the stream goes to A/DGDEV3.JPG, the header to A/DGDEV3H.BIN, and both
 * together to A/DGDEV3F.JPG. Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "module_def.h"
#include "clock.h"
extern char *hook_raw_image_addr(void);

#define CREATE_JPEG ((int (*)(void))0xff98f3f0)
#define SS_STATE    ((volatile unsigned *)0x76d8)
#define PARAMS      ((volatile unsigned *)0xb3ed4)
#define EXTRA       ((volatile unsigned *)0xb3f54)
#define DEV_STATE   ((volatile unsigned *)0x4e40)
#define SHAREMEM    (*(volatile int *)(0x6830 + 4))

static void lg(const char *s)
{
    int fd = open("A/CHDK/FILM/DGJPEG.TXT", O_WRONLY | O_CREAT | O_APPEND, 0777);
    if (fd >= 0) { write(fd, s, strlen(s)); close(fd); }
}
static int ram(unsigned a) { return (a >= 0x1000 && a < 0x00800000) || (a >= 0x40000000 && a < 0x48000000); }
static void words(const char *name, volatile const unsigned *p, int n)
{
    char t[200];
    int i, k = sprintf(t, "%s @%08x:", name, (unsigned)p);
    for (i = 0; i < n; i++) {
        k += sprintf(t + k, " %08x", p[i]);
        if (k > 160 || i == n - 1) { t[k++] = '\n'; t[k] = 0; lg(t); k = sprintf(t, "  "); }
    }
}
static int save(const char *path, unsigned a, unsigned n, unsigned a2, unsigned n2)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777), w = -1;
    if (fd < 0) return -1;
    w = write(fd, (void *)a, n);
    if (n2) w += write(fd, (void *)a2, n2);
    close(fd);
    return w;
}
static void dump_state(const char *when, unsigned shot)
{
    char t[80];
    sprintf(t, "-- %s\n", when);
    lg(t);
    words("ss_state", SS_STATE, 12);
    words("params", PARAMS, 32);
    words("extra", EXTRA, 8);
    words("dev_state", DEV_STATE, 16);
    if (ram(shot)) words("shot", (volatile unsigned *)shot, 48);
}

int _run()
{
    char t[200];
    unsigned shot, before[4], after[4], i, sum = 0;
    unsigned char *raw;
    int t0, rc;
    sprintf(t, "\ndgjpeg R3 tick=%d play=%d rec=%d sharemem=%d\n", get_tick_count(), camera_info.state.mode_play, camera_info.state.mode_rec, SHAREMEM);
    lg(t);
    shot = SS_STATE[4];
    dump_state("before", shot);
    raw = (unsigned char *)hook_raw_image_addr();
    if (ram((unsigned)raw)) for (i = 0; i < 15634944; i += 65536) sum = sum * 31 + raw[i] + (raw[i + 7] << 8);
    sprintf(t, "shot=%08x raw=%08x raw_fingerprint=%08x\n", shot, (unsigned)raw, sum);
    lg(t);
    if (!camera_info.state.mode_play) { lg("refused: run it from the gallery\n"); return 0; }
    if (!ram(shot) || !DEV_STATE[0x2c / 4] || !ram(EXTRA[0]) || !PARAMS[0x50 / 4]) {
        lg("refused: the shot record, develop queue, buffer block or parameter block is not as after a capture\n");
        return 0;
    }
    for (i = 0; i < 4; i++) before[i] = ((volatile unsigned *)shot)[0x5c / 4 + i];
    lg("calling 0xff98f3f0\n");
    t0 = get_tick_count();
    rc = CREATE_JPEG();
    sprintf(t, "returned %08x after %d ms\n", rc, get_tick_count() - t0);
    lg(t);
    dump_state("after", shot);
    for (i = 0; i < 4; i++) after[i] = ((volatile unsigned *)shot)[0x5c / 4 + i];
    sprintf(t, "pieces before %08x/%08x %08x/%08x after %08x/%08x %08x/%08x\n",
            before[0], before[1], before[2], before[3], after[0], after[1], after[2], after[3]);
    lg(t);
    if (!ram(after[0]) || !after[1] || after[1] > 0x2000000 || (after[3] && (!ram(after[2]) || after[3] > 0x100000))) {
        lg("not writing: the stream pieces look wrong\n");
        return 0;
    }
    sprintf(t, "stream starts %02x %02x, header starts %02x %02x\n",
            ((unsigned char *)after[0])[0], ((unsigned char *)after[0])[1],
            after[3] ? ((unsigned char *)after[2])[0] : 0, after[3] ? ((unsigned char *)after[2])[1] : 0);
    lg(t);
    sprintf(t, "wrote A/DGDEV3.JPG %d, A/DGDEV3H.BIN %d, A/DGDEV3F.JPG %d bytes\n",
            save("A/DGDEV3.JPG", after[0], after[1], 0, 0),
            after[3] ? save("A/DGDEV3H.BIN", after[2], after[3], 0, 0) : 0,
            after[3] ? save("A/DGDEV3F.JPG", after[2], after[3], after[0], after[1]) : 0);
    lg(t);
    return 0;
}

int _module_can_unload() { return 1; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgjpeg_sym;
libdgjpeg_sym _libdgjpeg = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"DIGIC JPEG develop probe (R3)",
    MTYPE_TOOL,
    &_libdgjpeg.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
