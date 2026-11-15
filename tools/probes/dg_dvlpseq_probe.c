/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe R4 (G12 1.00G only): ask Canon's develop
 * sequence to develop a shot again, from shooting mode, without the shutter
 * At the end of a capture SsCaptureCommon.c (0xffb072ac)
 * calls 0xff883f48(shot): the develop-sequence request, which posts
 * {type, shot} to DvlpSeqTask (queue *(0x2e70 + 4)); that task runs
 * SsCreateJpeg (develop, JPEG encode, header, file write) for the shot record.
 *
 * Shooting mode only, right after one throwaway photo: the raw buffer still
 * holds that photo, so the develop should make the same picture again, most
 * likely written over the throwaway photo's own file (the shot record carries
 * its file name). It refuses unless the image-buffer configuration is a
 * shooting one (*(0x2e88 + 4) <= 4, the check that failed in the gallery,
 * R3), DvlpSeqTask exists and the shot record looks like a capture's. Every
 * step and the state every 0.5 s for 15 s go to A/CHDK/FILM/DGDVLP.TXT. Not
 * part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "module_def.h"
#include "clock.h"
extern char *hook_raw_image_addr(void);

#define DVLP_REQUEST ((void (*)(unsigned))0xff883f48)
#define DVLP_STATE   ((volatile unsigned *)0x2e70)    /* [1] queue, [5] task created */
#define BUF_STATE    ((volatile unsigned *)0x2e88)    /* [1] buffer configuration */
#define SS_STATE     ((volatile unsigned *)0x76d8)    /* [4] shot record */
#define PARAMS       ((volatile unsigned *)0xb3ed4)
#define DEV_STATE    ((volatile unsigned *)0x4e40)
#define SHAREMEM     (*(volatile int *)(0x6830 + 4))

static void lg(const char *s)
{
    int fd = open("A/CHDK/FILM/DGDVLP.TXT", O_WRONLY | O_CREAT | O_APPEND, 0777);
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
    char t[220];
    unsigned shot, i, sum = 0;
    volatile unsigned *s;
    unsigned char *raw;
    int t0, n;
    sprintf(t, "\ndgdvlp R4 tick=%d play=%d rec=%d sharemem=%d bufcfg=%u\n", get_tick_count(),
            camera_info.state.mode_play, camera_info.state.mode_rec, SHAREMEM, BUF_STATE[1]);
    lg(t);
    words("dvlp_state", DVLP_STATE, 8);
    words("buf_state", BUF_STATE, 12);
    words("ss_state", SS_STATE, 12);
    words("params", PARAMS, 32);
    words("dev_state", DEV_STATE, 12);
    shot = SS_STATE[4];
    s = (volatile unsigned *)shot;
    if (ram(shot)) words("shot", s, 48);
    raw = (unsigned char *)hook_raw_image_addr();
    if (ram((unsigned)raw)) for (i = 0; i < 15634944; i += 65536) sum = sum * 31 + raw[i] + (raw[i + 7] << 8);
    sprintf(t, "shot=%08x raw=%08x raw_fingerprint=%08x\n", shot, (unsigned)raw, sum);
    lg(t);
    if (!camera_info.state.mode_rec) { lg("refused: run it in shooting mode, right after a photo\n"); return 0; }
    sprintf(t, "checks: bufcfg_ok=%d task=%d queue=%d shot=%d image_no=%d jpeg_ptr=%d\n", BUF_STATE[1] <= 4, DVLP_STATE[5] == 1,
            DVLP_STATE[1] != 0, ram(shot), PARAMS[0x50 / 4] != 0, ram(shot) && ram(s[0x5c / 4]));
    lg(t);
    if (BUF_STATE[1] > 4 || DVLP_STATE[5] != 1 || !DVLP_STATE[1] || !ram(shot) || !PARAMS[0x50 / 4] || !ram(s[0x5c / 4])) {
        lg("refused: buffer configuration, develop sequence or shot record not as after a capture\n");
        return 0;
    }
    lg("calling 0xff883f48(shot)\n");
    t0 = get_tick_count();
    DVLP_REQUEST(shot);
    sprintf(t, "request returned after %d ms\n", get_tick_count() - t0);
    lg(t);
    for (n = 0; n < 30; n++) {                        /* 15 s of state while DvlpSeqTask works */
        msleep(500);
        sprintf(t, "t+%5d ss_state %08x %08x %08x  shot jpeg %08x/%08x hdr %08x/%08x  params[0]=%08x [0x34]=%08x  bufcfg=%u sharemem=%d\n",
                get_tick_count() - t0, SS_STATE[0], SS_STATE[1], SS_STATE[2],
                s[0x5c / 4], s[0x60 / 4], s[0x64 / 4], s[0x68 / 4], PARAMS[0], PARAMS[0x34 / 4], BUF_STATE[1], SHAREMEM);
        lg(t);
    }
    words("shot_after", s, 48);
    lg("done\n");
    return 0;
}

int _module_can_unload() { return 1; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgdvlp_sym;
libdgdvlp_sym _libdgdvlp = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"DIGIC develop-sequence probe (R4)",
    MTYPE_TOOL,
    &_libdgdvlp.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
