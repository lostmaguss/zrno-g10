/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe R2 (G12 1.00G only): develop from the
 * gallery by calling Canon's develop job the way the shooting sequence does
 * SsCreateJpeg.c's develop step (0xff98e808) calls
 *
 *   0xff8e7438(job = {pass 0, mode 0xb, 0, handle}, out = 0xb3f74, result)
 *
 * with handle = *(*(0x76d8 + 0x10) + 0x18), the shot record's image handle.
 * The job builds the develop descriptor, runs DevelopPathForCR2 (which sends
 * the develop task its six messages and waits) and returns the JPEG as two
 * pieces: result[0xc] / [0x10] and result[0x14] / [0x18] (address, size).
 *
 * This first run develops whatever sensor data is still in the raw buffer
 * from the last shot (no backup is loaded), so its JPEG should look like the
 * last photo taken. Gallery only. Every step is logged to
 * A/CHDK/FILM/DGDEV.TXT before the next; the JPEG stream (without Canon's
 * EXIF header) goes to A/DGDEV.JPG. It refuses to call the develop job unless
 * the shot record, handle, output descriptor and develop task look as they do
 * after a capture. Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "module_def.h"
#include "raw.h"
#include "clock.h"
extern char *hook_raw_image_addr(void);

#define DEVELOP_JOB ((void (*)(unsigned *, void *, unsigned *))0xff8e7438)
#define SS_STATE    ((volatile unsigned *)0x76d8)     /* SsCreateJpeg: [4] = shot record */
#define OUT_DESC    ((volatile unsigned *)0xb3f74)
#define DEV_STATE   ((volatile unsigned *)0x4e40)     /* [0x2c/4] = develop queue */
#define SHAREMEM    (*(volatile int *)(0x6830 + 4))

static void lg(const char *s)
{
    int fd = open("A/CHDK/FILM/DGDEV.TXT", O_WRONLY | O_CREAT | O_APPEND, 0777);
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

int _run()
{
    char t[200];
    unsigned job[8], result[16], shot, handle, i, sum = 0;
    unsigned char *raw;
    int t0, fd;
    memset(job, 0, sizeof(job));
    memset(result, 0, sizeof(result));
    sprintf(t, "\ndgdev R2 tick=%d play=%d rec=%d sharemem=%d\n", get_tick_count(), camera_info.state.mode_play, camera_info.state.mode_rec, SHAREMEM);
    lg(t);
    words("ss_state", SS_STATE, 12);
    words("out_desc", OUT_DESC, 12);
    words("dev_state", DEV_STATE, 16);
    shot = SS_STATE[4];
    handle = ram(shot) ? *(volatile unsigned *)(shot + 0x18) : 0;
    if (ram(shot)) words("shot", (volatile unsigned *)shot, 48);
    raw = (unsigned char *)hook_raw_image_addr();
    if (ram((unsigned)raw)) for (i = 0; i < 15634944; i += 65536) sum = sum * 31 + raw[i] + (raw[i + 7] << 8);
    sprintf(t, "shot=%08x handle=%08x raw=%08x raw_fingerprint=%08x\n", shot, handle, (unsigned)raw, sum);
    lg(t);
    if (!camera_info.state.mode_play) { lg("refused: run it from the gallery\n"); return 0; }
    if (!ram(shot) || !handle || !ram(OUT_DESC[0]) || !OUT_DESC[2] || !DEV_STATE[0x2c / 4]) {
        lg("refused: the shot record, handle, output descriptor or develop queue is not as after a capture\n");
        return 0;
    }
    job[1] = 0xb;                                     /* first pass, as SsCreateJpeg */
    job[3] = handle;
    lg("calling 0xff8e7438\n");
    t0 = get_tick_count();
    DEVELOP_JOB(job, (void *)OUT_DESC, result);
    sprintf(t, "returned after %d ms\n", get_tick_count() - t0);
    lg(t);
    words("result", result, 16);
    words("out_desc_after", OUT_DESC, 12);
    {
        unsigned a1 = result[3], n1 = result[4], a2 = result[5], n2 = result[6];
        if (!ram(a1) || n1 > 0x2000000 || (n2 && (!ram(a2) || n2 > 0x2000000))) { lg("not writing: result pieces look wrong\n"); return 0; }
        fd = open("A/DGDEV.JPG", O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (fd >= 0) {
            int w1 = write(fd, (void *)a1, n1), w2 = n2 ? write(fd, (void *)a2, n2) : 0;
            close(fd);
            sprintf(t, "wrote A/DGDEV.JPG: %d + %d bytes (first bytes %02x %02x)\n", w1, w2, ((unsigned char *)a1)[0], ((unsigned char *)a1)[1]);
            lg(t);
        } else lg("could not open A/DGDEV.JPG\n");
    }
    return 0;
}

int _module_can_unload() { return 1; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgdev_sym;
libdgdev_sym _libdgdev = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"DIGIC develop probe (R2)",
    MTYPE_TOOL,
    &_libdgdev.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
