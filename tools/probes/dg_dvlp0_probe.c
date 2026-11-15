/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe R14 (G12 1.00G only): post Canon's
 * develop-sequence message type 0 for the last shot, from shooting mode,
 * without the shutter.
 *
 * R4 showed that 0xff883f48(shot) always posts type 1, which only prepares
 * the develop parameters and first waits for the shooting controller's "new
 * shot" flag (0x10 on *(0x2df4 + 0xc), raised by 0xff880738). A finished
 * shot has already used that flag, so the wait timed out and asserted
 * (SsShootCtrl.c line 1842). The develop itself, the JPEG encode and the
 * file write are type 0:
 *
 *   0xff884034(shot): take the semaphore *(0x2e70 + 0xc), post {0, shot}
 *   DvlpSeqTask: type 0 -> 0xff98eac0(shot) (SsCreateJpeg's main)
 *
 * The shooting controller posts it on its event 1 (0xff8805cc). The only
 * capture wait in 0xff98eac0 (flag 8, line 0x97a) is on the RAW path, taken
 * when *(0x76d8 + 0x20) != 0, so this probe runs in JPEG-only mode only.
 *
 * R5 posted type 0 and the develop task asserted (PrcssFil.c line 867): the
 * shot's settings record (a PropertyCase, shot + 0x18) had been freed and
 * wiped (0xff8958b8) when the throwaway's JPEG was saved. A capture
 * allocates a fresh one (0xff895680, a copy of the camera's current
 * settings) into shot + 0x18 (SsCaptureCommon.c, 0xffb07058), and the JPEG
 * step frees it (0xff98e00c). R6 did the same before the post.
 *
 * R6 got through the develop's setup, then live view asserted
 * (MotionVector.c line 337, LiveImageTask): the develop took the image
 * hardware from under it. A capture pauses live view around that work
 * (0xff986580 / 0xff9861e8). R7 pauses it (0xff8532e4, what
 * LiveImageTool.Pause calls) before the post and resumes it (0xff8533d8)
 * once the JPEG step has freed the settings record, or after 15 s.
 *
 * R7 paused live view and still hit the same assert. LiveImageTool.Pause is
 * only the first step of the capture's pause: in still modes 1-4 the
 * capture then calls 0xff8531e0, LiveImageTool.StopMjpegMaking. That clears
 * live view's MJPEG flag (*(0x20ac + 0x60)), posts message 12 to the
 * live-image subtask (0xff90ff50) and waits for event flag 8 on
 * *(0x20ac + 0x114), up to 5 s: the side MJPEG encode that the motion
 * vector reads drains off the JPEG hardware. On the way back the capture
 * calls 0xff853268 (LiveImageTool.StartMjpegMaking, which sets the flag
 * again while live view runs) and then resumes. R8 does the same: pause,
 * stop MJPEG making, develop, start MJPEG making, resume. It logs live
 * view's state words around each step.
 *
 * R8 kept live view alive, but the develop task never returned from the
 * type-1 handler (0xff8e59bc), so SsCreateJpeg never posted "develop done".
 * That handler runs six DevelopPath steps and writes stage markers into
 * 0x8231c + 0x6c / 0xb4 / 0xb8 between them. R9 logs
 * those markers, 0x823d8, 0x4e40 + 0x3c and Sharemem every 0.5 s, and
 * DevelopPath's state (0xaae4) and the engine clock counts (0x2858) at
 * set points, for up to 55 s (SsCreateJpeg asserts at 60 s). If the
 * develop is still stuck it leaves live view paused: R8's resume over a
 * busy develop gave a pink screen and a power-off.
 *
 * R9 found the develop stuck in its second DevelopPath step: engine
 * module 18 asks the resource arbiter (0xff94130c) for groups 3, 6, 7, 19
 * and 20 as unit 11, and groups 7 and 6 were still held by unit 0, live
 * view's image path, which pause does not stop. Each request timed out
 * after 10 s and the camera then switched off. Canon's factory develop
 * (InitializeTestRec, 0xff86f91c) stops live view completely first, with
 * LiveImageTool.StopEVF (0xff852f8c), when 0xff854250() says it runs.
 * R10 does that instead of the pause, logs the arbiter's counts for the
 * five groups before and after, and only posts the develop when no other
 * unit holds any of them. Live view stays stopped afterwards, as after
 * TestRec: press Playback and go back to shooting to restart it.
 *
 * R10: StopEVF dropped unit 0's count on groups 6 and 7 from 3 to 2; the
 * other two holds come from WBInteg.c (0xff8bb688) and the brightness
 * measurement (0xff8c2360), the exposure / white balance integration on
 * live frames. R11 then calls the factory eventproc StopAEIntegralService
 * (0xff9575dc), logs its result, its state (0xaefa4) and the counts, and
 * still posts only when the five groups are free.
 *
 * R11 crashed in StopAEIntegralService (a factory service that was never
 * started). R12 watched a real capture: the remaining holds end when
 * WBIntegTask gets message type 1, "release mode", at the full press.
 * WBIntegTask keeps a busy flag per mode (0x468c + 0x110 / 0x114 / 0x118);
 * type 2 reserves a mode, type 1 releases it, and releasing the last one
 * calls 0xff84f1e0 (the path manager). The release API is
 * 0xff8bfe2c(mode, callback, arg); Canon calls it as (0, 0, 0). R13 does
 * StopEVF, then releases every busy mode, logs the flags and the counts,
 * and posts the develop only when the five groups are free. It no longer
 * calls StopAEIntegralService.
 *
 * R13: the three releases worked (busy 1/1/1 -> 0/0/0) but the camera died
 * within 300 ms of the last one, which also stops WBInteg's engine path
 * (0xff84f1e0) while live exposure keeps sending integrations. R14
 * releases modes 0 and 1 only and logs the engine counts after each; if
 * the groups are free it develops with mode 2 still reserved. Only if they
 * are still held does it release mode 2, and then logs WBInteg and the
 * counts every 20 ms for 1 s (each line written at once) without posting.
 *
 * Shooting mode, right after one throwaway photo: the raw buffer and the
 * develop parameters (0xb3ed4) still hold that photo, so the develop should
 * make the same picture again, most likely written over the file named in
 * the parameters. Every step and the state every 0.5 s for 15 s go to
 * A/CHDK/FILM/DGDVLP0.TXT, with that file's size and time. Not part of
 * Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "module_def.h"
#include "clock.h"
extern char *hook_raw_image_addr(void);

#define DVLP_POST0   ((void (*)(unsigned))0xff884034)
#define LV_RUNNING   ((int (*)(void))0xff854250)     /* *(0x20ac + 0x174): live view runs */
#define STOP_EVF     ((void (*)(void))0xff852f8c)    /* LiveImageTool.StopEVF, as InitializeTestRec calls it */
#define RES_SEM      ((volatile unsigned *)0xaa804)   /* engine resource arbiter: semaphore per group */
#define RES_CNT      ((volatile unsigned *)0xaa858)   /* use count per group (0x40 bytes) and unit (4 bytes) */
#define DEV_UNIT     11                               /* the develop's unit in the arbiter */
#define WB_RELEASE   ((void (*)(int, void *, int))0xff8bfe2c)   /* WBInteg: post type 1, release a mode */
#define WBI_STATE    ((volatile unsigned *)0x468c)    /* +0x14 initialized, +0x10c state, +0x110/4/8 busy per mode */
#define AE_INTEG_STOP ((int (*)(void))0xff9575dc)   /* eventproc StopAEIntegralService */
#define AE_INTEG     ((volatile unsigned *)0xaefa4)   /* AE integral service state: +8 mode */
#define LV_PAUSE     ((void (*)(void))0xff8532e4)    /* LiveImage.c: what LiveImageTool.Pause calls */
#define LV_RESUME    ((void (*)(void))0xff8533d8)    /* LiveImage.c: what LiveImageTool.Resume calls */
#define MJPEG_STOP   ((void (*)(void))0xff8531e0)    /* LiveImageTool.StopMjpegMaking: waits for the MJPEG side path */
#define MJPEG_START  ((void (*)(void))0xff853268)    /* LiveImageTool.StartMjpegMaking */
#define LV_STATE     ((volatile unsigned *)0x20ac)    /* live view: [0x60/4] MJPEG on, [0x174/4] running */
#define PROP_ALLOC  ((unsigned (*)(void))0xff895680)    /* PropertyCase: a new settings record */
#define DVLP_STATE   ((volatile unsigned *)0x2e70)    /* [1] queue, [3] semaphore, [5] task created */
#define BUF_STATE    ((volatile unsigned *)0x2e88)    /* [1] buffer configuration */
#define SS_STATE     ((volatile unsigned *)0x76d8)    /* [4] shot record, [8] RAW */
#define PARAMS       ((volatile unsigned *)0xb3ed4)   /* +0x5c: the JPEG's file name */
#define DEV_STATE    ((volatile unsigned *)0x4e40)
#define STAGE_DESC   ((volatile unsigned *)0x8231c)   /* develop descriptor: +0x6c/+0xb4/+0xb8 stage markers */
#define STAGE_FLAG   (*(volatile unsigned *)0x823d8)  /* 5 once 0xff8e59bc starts */
#define DP_STATE     ((volatile unsigned *)0xaae4)    /* DevelopPath.c state */
#define CLK_STATE    ((volatile unsigned *)0x2858)    /* engine clock reference counts (0xff869334) */
#define SHAREMEM     (*(volatile int *)(0x6830 + 4))

static void lg(const char *s)
{
    int fd = open("A/CHDK/FILM/DGDVLP0.TXT", O_WRONLY | O_CREAT | O_APPEND, 0777);
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
static void file_name(char *out)                      /* copy of PARAMS + 0x5c, bounded */
{
    const char *p = (const char *)PARAMS + 0x5c;
    int i;
    for (i = 0; i < 39 && p[i] >= 32 && p[i] < 127; i++) out[i] = p[i];
    out[i] = 0;
}
static void file_state(const char *when)
{
    char name[40], t[120];
    struct stat st;
    file_name(name);
    if (name[0] && !stat(name, &st)) sprintf(t, "%s file %s size=%lu mtime=%lu\n", when, name, st.st_size, st.st_mtime);
    else sprintf(t, "%s file '%s' not found\n", when, name);
    lg(t);
}

static void wbi_state(const char *when)
{
    char t[160];
    sprintf(t, "%s: init14=%u state10c=%u busy=%u/%u/%u 100=%08x 140=%u last_msg=%u/%u\n", when,
            WBI_STATE[0x14 / 4], WBI_STATE[0x10c / 4], WBI_STATE[0x110 / 4], WBI_STATE[0x114 / 4],
            WBI_STATE[0x118 / 4], WBI_STATE[0x100 / 4], WBI_STATE[0x140 / 4],
            ((volatile unsigned *)0x41384)[0], ((volatile unsigned *)0x41384)[1]);
    lg(t);
}
static void lv_state(const char *when)
{
    char t[160];
    sprintf(t, "%s live view: paused48=%u flag50=%u mjpeg60=%u cb_a4=%08x mode138=%u running174=%u\n", when,
            LV_STATE[0x48 / 4], LV_STATE[0x50 / 4], LV_STATE[0x60 / 4], LV_STATE[0xa4 / 4],
            LV_STATE[0x138 / 4], LV_STATE[0x174 / 4]);
    lg(t);
}
static void stage(int dt)
{
    char t[200];
    sprintf(t, "t+%5d stage flag=%u 6c=%08x b4=%08x b8=%08x dev3c=%08x sharemem=%d dp[0]=%08x dp[a]=%08x dp[c]=%08x\n",
            dt, STAGE_FLAG, STAGE_DESC[0x6c / 4], STAGE_DESC[0xb4 / 4], STAGE_DESC[0xb8 / 4], DEV_STATE[0x3c / 4],
            SHAREMEM, DP_STATE[0], DP_STATE[10], DP_STATE[12]);
    lg(t);
}
static void engines(const char *when)
{
    char t[40];
    sprintf(t, "%s\n", when);
    lg(t);
    words("developpath", DP_STATE, 16);
    words("clocks", CLK_STATE, 32);
}
static const unsigned char dev_groups[5] = {3, 6, 7, 19, 20};   /* what engine module 18 requests */
static void resources(const char *when)
{
    char t[200];
    int i, u, k;
    sprintf(t, "%s engine resources (group: semaphore, counts for units 0-15)\n", when);
    lg(t);
    for (i = 0; i < 5; i++) {
        unsigned g = dev_groups[i];
        k = sprintf(t, "  g%-2u sem=%08x:", g, RES_SEM[g]);
        for (u = 0; u < 16; u++) k += sprintf(t + k, " %x", RES_CNT[g * 16 + u]);
        t[k++] = '\n'; t[k] = 0;
        lg(t);
    }
}
static int resources_busy(char *t)                    /* another unit holds one of the develop's groups */
{
    int i, u;
    for (i = 0; i < 5; i++)
        for (u = 0; u < 16; u++)
            if (u != DEV_UNIT && RES_CNT[dev_groups[i] * 16 + u]) {
                sprintf(t, "group %u held by unit %d (count %u)", dev_groups[i], u, RES_CNT[dev_groups[i] * 16 + u]);
                return 1;
            }
    return 0;
}

int _run()
{
    char t[220];
    unsigned shot, rec, i, sum = 0;
    volatile unsigned *s;
    unsigned char *raw;
    int t0, n;
    sprintf(t, "\ndgdvlp0 R14 tick=%d play=%d rec=%d sharemem=%d bufcfg=%u\n", get_tick_count(),
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
    file_state("before");
    if (!camera_info.state.mode_rec) { lg("refused: run it in shooting mode, right after a photo\n"); return 0; }
    sprintf(t, "checks: bufcfg_ok=%d task=%d queue=%d sem=%d shot=%d image_no=%d jpeg_ptr=%d jpeg_only=%d\n",
            BUF_STATE[1] <= 4, DVLP_STATE[5] == 1, DVLP_STATE[1] != 0, DVLP_STATE[3] != 0, ram(shot),
            PARAMS[0x50 / 4] != 0, ram(shot) && ram(s[0x5c / 4]), SS_STATE[8] == 0);
    lg(t);
    if (BUF_STATE[1] > 4 || DVLP_STATE[5] != 1 || !DVLP_STATE[1] || !DVLP_STATE[3] || !ram(shot) ||
        !PARAMS[0x50 / 4] || !ram(s[0x5c / 4]) || SS_STATE[8]) {
        lg("refused: buffer configuration, develop sequence, shot record or RAW setting not as after a JPEG capture\n");
        return 0;
    }
    sprintf(t, "old settings record %08x\n", s[0x18 / 4]);
    lg(t);
    lv_state("before");
    resources("before");
    wbi_state("WBInteg at start");
    if (LV_RUNNING() == 1) {
        lg("stopping live view (0xff852f8c, LiveImageTool.StopEVF)\n");
        t0 = get_tick_count();
        STOP_EVF();
        sprintf(t, "StopEVF returned after %d ms\n", get_tick_count() - t0);
        lg(t);
        msleep(200);
    } else lg("live view was not running\n");
    lv_state("after StopEVF");
    resources("after StopEVF");
    if (resources_busy(t)) {
        lg("still held: ");
        lg(t);
        lg("\n");
        wbi_state("WBInteg before release");
        if (WBI_STATE[0x14 / 4] != 1) lg("WBInteg not initialized: not releasing\n");
        else {
            for (n = 0; n < 2; n++) if (WBI_STATE[(0x110 / 4) + n]) {
                sprintf(t, "releasing WBInteg mode %d (0xff8bfe2c(%d, 0, 0))\n", n, n);
                lg(t);
                WB_RELEASE(n, 0, 0);
                msleep(150);
                wbi_state("  ");
                resources("  after this release");
            }
            if (resources_busy(t) && WBI_STATE[0x118 / 4]) {
                lg("still held after modes 0/1: ");
                lg(t);
                lg("\nreleasing WBInteg mode 2 (0xff8bfe2c(2, 0, 0)), then watching; no develop this run\n");
                t0 = get_tick_count();
                WB_RELEASE(2, 0, 0);
                for (n = 0; n < 50; n++) {
                    msleep(20);
                    sprintf(t, "t+%4d ", get_tick_count() - t0);
                    wbi_state(t);
                    sprintf(t, "t+%4d u0 g6=%u g7=%u  busy:", get_tick_count() - t0, RES_CNT[6 * 16], RES_CNT[7 * 16]);
                    lg(t);
                    resources_busy(t) ? lg(t) : lg("none");
                    lg("\n");
                }
                resources("after watching");
                lg("done\n");
                return 0;
            }
        }
        msleep(100);
        wbi_state("WBInteg after release");
        resources("after WBInteg release");
    }
    if (resources_busy(t)) {
        lg("refused: ");
        lg(t);
        lg("\nlive view is stopped; press Playback and go back to shooting to restart it\ndone\n");
        return 0;
    }
    lg("all five groups free for the develop\n");
    rec = PROP_ALLOC();
    s[0x18 / 4] = rec;
    sprintf(t, "new settings record %08x in_use=%u\n", rec, rec ? ((volatile unsigned *)rec)[1] : 0);
    lg(t);
    if (!rec) { lg("stopped: no settings record\n"); return 0; }
    stage(0);
    engines("engines before the post");
    lg("calling 0xff884034(shot): develop-sequence type 0\n");
    t0 = get_tick_count();
    DVLP_POST0(shot);
    sprintf(t, "post returned after %d ms\n", get_tick_count() - t0);
    lg(t);
    for (n = 0; n < 550; n++) {                       /* until the JPEG step frees the record, 55 s at most */
        msleep(100);
        if (n % 5 == 4) stage(get_tick_count() - t0);
        if (n == 29 || n == 199) engines(n == 29 ? "engines at 3 s" : "engines at 20 s");
        if (n % 50 == 49) {
            sprintf(t, "t+%5d ss_state %08x %08x %08x %08x  shot[0]=%08x jpeg %08x/%08x hdr %08x/%08x  params[0]=%08x  rec_in_use=%u\n",
                    get_tick_count() - t0, SS_STATE[0], SS_STATE[1], SS_STATE[2], SS_STATE[4], s[0],
                    s[0x5c / 4], s[0x60 / 4], s[0x64 / 4], s[0x68 / 4], PARAMS[0], ((volatile unsigned *)rec)[1]);
            lg(t);
        }
        if (!((volatile unsigned *)rec)[1]) break;
    }
    sprintf(t, "%s after %d ms\n",
            ((volatile unsigned *)rec)[1] ? "record still in use" : "record freed: JPEG step finished", get_tick_count() - t0);
    lg(t);
    if (((volatile unsigned *)rec)[1]) {
        stage(get_tick_count() - t0);
        engines("engines while stuck");
        resources("while stuck");
        words("descriptor", STAGE_DESC, 48);
        lg("develop still stuck (SsCreateJpeg asserts at 60 s)\ndone\n");
        return 0;
    }
    resources("after the develop");
    lg("live view left stopped, as after TestRec: press Playback and go back to shooting\n");
    for (n = 0; n < 4; n++) { msleep(1000); file_state("  "); }
    words("shot_after", s, 48);
    words("params_after", PARAMS, 32);
    file_state("after");
    lg("done\n");
    return 0;
}

int _module_can_unload() { return 1; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgdvlp0_sym;
libdgdvlp0_sym _libdgdvlp0 = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"DIGIC develop type-0 probe (R14)",
    MTYPE_TOOL,
    &_libdgdvlp0.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
