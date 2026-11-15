/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe R24 (G12 1.00G only): re-develop a
 * saved sensor backup from the gallery with Zrno's look.
 *
 * R23 restored all of the saved settings record (81 of 876 words differed
 * from today's) and the camera died in the type-1 develop at the engine
 * switch. R24 (DRY_RUN) is a dry run to find which words are which: on the
 * photo on screen it finds the backup's .SET as R23 does, allocates a fresh
 * record (today's settings), writes it to A/CHDK/FILM/REC24_n.BIN (the next
 * free n, 1-9), logs every word that differs from the .SET, and frees the
 * record. No backup load, no develop. Running it again after changing a
 * setting (a WB preset) gives a second dump to diff against the first.
 *
 * R25: USB dumps (tools/dg_rec_usb.py) replaced R24's dry run. R25 is R23
 * with restore_ranges(): only the white-balance bytes of the saved record go
 * into the fresh one, not the session pointers that most likely crashed R23.
 * Writes A/REDEV25.JPG.
 *
 * R18 re-developed a backup into a plain Canon JPEG. On a real shot Film
 * Lab's colour pass runs inside the type-2 JPEG encode: film_stage_arm()
 * arms it, and the develop task's hook (film_stage_observe) grades the YUV
 * frame when the descriptor says type 2. R17/R18's gallery type 2 matched
 * the hook's checks. film_stage_arm and film_stage_finish are not exported
 * to modules, so R19 calls them at their addresses in this exact core build
 * (platform/g12/sub/100g/main.elf, DISKBOOT sha1 31b02864),
 * and first checks that its imported film_playback_image_id resolved to
 * that build's address. R19 refused there: the core's Zrno functions are
 * Thumb code (0x003e2485), and nm had printed them without the Thumb bit, so
 * film_stage_arm / film_stage_finish are 0x003e2be9 / 0x003e3271.
 *
 * R20 armed the stage before type 1 and the camera died in the type-1
 * develop, at the layout switch (as R16 did). Nothing in Zrno reacts to
 * an armed stage during type 1, so R21 arms it only after types 1 and 5,
 * right before type 2: type 1 runs exactly as in R18. Polls every 50 ms.
 * The arm id is the backup's number (digits 9-15 of A/FGRAW/Rnnnnnnn.RAW).
 *
 * R21 gave the backup Zrno's look, but the white balance of today's
 * settings: the settings record is a copy of the current settings. R22
 * restores the original shot's record when dgsave (R22a) saved it beside the
 * backup (A/FGRAW/Rnnnnnnn.SET, 0xdb8 bytes): bytes 8.. go into the fresh
 * record (words 0-1 are its semaphore and in-use flag), before type 1.
 *
 * R22's run found Canon alternating capture buffer sets: after some shots
 * the shot record is the ring's second (0x3b868), the raw buffer 0x41269150
 * and the JPEG / header 0x47285200 / 0x47282800 instead of 0x4710d600 /
 * 0x4710ac00. R23 takes the JPEG and header addresses from the shot record
 * (+0x5c, +0x64) and the output space from the end of Canon's JPEG buffer
 * (0x47109800 + 0xef6800), which gives Canon's 0xef2800 / 0xeeec00 for the
 * first set. It arms the stage before type 1 with the backup's
 * number, waits up to 60 s for type 2 (the CPU colour pass takes ~18 s),
 * calls film_stage_finish after type 0xa, and writes A/REDEV23.JPG.
 *
 * R17 developed the last shot's sensor data from the gallery into a
 * 3648 x 2736 JPEG matching the camera's own. R18 first finds the sensor
 * backup of the photo on screen (film_playback_image_id() and the
 * NATIVE.TXT lookup copied from film_redev.c), checks that the raw buffer
 * is the one the develop reads (shot + 0x2c), loads the backup into it in
 * 256 KB pieces (as film_raw.c's load_backup), then runs R17's four steps
 * and writes the main JPEG alone to A/REDEV18.JPG. The develop parameters
 * still come from the last shot, and the settings record is a copy of the
 * camera's current settings.
 *
 * R16 died on the type-1 develop that worked in R15. The one difference:
 * R16 set extra[0..1] = 0 (as the shot's own type 1); R15 left the header
 * area 0x4710ac00 / 0x2000 there. R17 sends type 1 with extra untouched, as
 * R15 did, and logs every 100 ms while each step runs (DevelopPath's module
 * count, the stage markers, Sharemem), so a crash shows how far it got.
 *
 * R15 ran the raw -> YUV develop (mode 1) from the gallery in 710 ms. R16
 * then sends the rest of a JPEG-only shot's develop messages the way the
 * shot's own trace shows them, with the JPEG buffer manager's fixed
 * addresses filled in by hand (the manager itself asserts in playback):
 *   type 5:   params[0x34] = 5, [0x38] = 0, extra[0..1] = 0;
 *   type 2:   main JPEG, [0x38] = 0xeeec00, extra[0] = 0x4710d600 (output),
 *             extra[1] = 0xef2800 (space);
 *   type 0xa: thumbnail into the header, params[0xc] = 0, [0x38] = 0x2000,
 *             extra[0] = 0x4710ac00, extra[1] = 0x1e00.
 * Each step waits for its result block. Before type 2 the first 4 KB of the
 * output are filled with 0xa5, to prove that the encode wrote them. It
 * finds the main JPEG's end (FF D9) and writes header (shot + 0x68 bytes at
 * 0x4710ac00) + main JPEG to A/REDEV16.JPG.
 *
 * R15's description follows.
 *
 * In shooting mode the develop's engine resource groups (3, 6, 7, 19, 20)
 * are held by live view, WBIntegTask and the brightness measurement, and
 * freeing them under the running exposure loop killed the camera (R11,
 * R13, R14). In playback none of those run (Magic Lantern frees LiveView's
 * holds by going to playback), and R2 already ran Canon's CR2 develop from
 * the gallery. R15, in the gallery after one normal JPEG-only shot:
 *   1. logs the five groups' counts (all units), WBInteg's busy flags, live
 *      view's state, Sharemem, the buffer configuration and the last
 *      shot's develop parameters (0xb3ed4 / 0xb3f54);
 *   2. refuses unless all five groups are free, the develop task exists and
 *      the parameters still hold a shot;
 *   3. gives the shot a fresh settings record (0xff895680, as a capture
 *      does; the develop's filter setup reads it) in params + 0x58 and
 *      shot + 0x18;
 *   4. posts the raw -> YUV develop as SsCreateJpeg's 0xff98f518 does:
 *      params[0x34] = 1, params[0] = 0, params[0x38] = 0, then
 *      0xff8e740c(0xb3ed4, 0xb3f54, result). It does not wait on
 *      SsCreateJpeg's event flag (that wait asserts after 60 s);
 *   5. polls every 100 ms for 20 s: the result block, the develop's stage
 *      markers (0x823d8, 0x8231c + 0x6c / 0xb4 / 0xb8), DevelopPath's
 *      module count and unit 11's counts;
 *   6. logs the result, a fingerprint of the YUV frame (0x4238a730) and
 *      frees the settings record if the develop finished.
 * No JPEG yet. Log: A/CHDK/FILM/DGPLAY.TXT. Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "module_def.h"
#include "clock.h"
#include "task.h"

#define DEV_POST     ((void (*)(volatile unsigned *, volatile unsigned *, unsigned *))0xff8e740c)
#define PROP_ALLOC   ((unsigned (*)(void))0xff895680)    /* PropertyCase: new settings record */
#define PROP_FREE    ((void (*)(unsigned))0xff8958b8)    /* PropertyCase: wipe and free */
#define RES_CNT      ((volatile unsigned *)0xaa858)
#define LV_STATE     ((volatile unsigned *)0x20ac)
#define WBI_STATE    ((volatile unsigned *)0x468c)
#define BUF_STATE    ((volatile unsigned *)0x2e88)
#define SS_STATE     ((volatile unsigned *)0x76d8)    /* [4] shot record, [8] RAW */
#define PARAMS       ((volatile unsigned *)0xb3ed4)   /* +0x34 mode, +0x58 settings record, +0x5c file name */
#define EXTRA        ((volatile unsigned *)0xb3f54)
#define DEV_STATE    ((volatile unsigned *)0x4e40)    /* +0x2c queue, +4 done callback */
#define DESC         ((volatile unsigned *)0x8231c)
#define DEV_FLAG     (*(volatile unsigned *)0x823d8)
#define DP_COUNT     (*(volatile unsigned char *)0xaae4)
#define YUV          ((volatile unsigned *)0x4238a730)
#define SHAREMEM     (*(volatile int *)(0x6830 + 4))

extern unsigned film_playback_image_id(void);
extern char *hook_raw_image_addr(void);
/* R27: the camera runs a later core with sharpening
 * (DISKBOOT sha256 c8551241), not the trace
 * core R19-R26 were built for: there film_stage_arm / film_stage_finish are
 * 0x003e2be9 / 0x003e3271 and module_run 0x003ee129; here 0x003e2b11 /
 * 0x003e31bd / 0x003ee211. film_playback_image_id is 0x003e2485 in both, so
 * R27 also checks the first code word of both stage functions. */
#define CORE_PLAYBACK_ID 0x003e2485u                /* film_playback_image_id, to check the build */
#define STAGE_ARM    ((void (*)(unsigned))0x003e2b11)  /* film_stage_arm (Thumb) */
#define STAGE_FINISH ((void (*)(void))0x003e31bd)      /* film_stage_finish (Thumb) */
#define ARM_CODE0    0x9001b5f7u                    /* first word at film_stage_arm & ~1 */
#define FINISH_CODE0 0x4b07b510u                    /* first word at film_stage_finish & ~1 */
#define RAW_SIZE     15634944u
#define DRY_RUN      0                              /* 1: R24, dump and diff the settings record only */

/* From one NATIVE.TXT line, the backup of IMG_<file>.JPG in DCIM folder
 * <dir> (copied from film_redev.c). */
static int backup_match(const char *line, int dir, int file, char *out)
{
    char want[16];
    const char *j = strstr(line, "jpg=A/DCIM/"), *r = strstr(line, "raw=A/FGRAW/R");
    int n = 0;
    if (!j || !r) return 0;
    j += 11;
    sprintf(want, "%03d", dir);
    if (strncmp(j, want, 3)) return 0;
    sprintf(want, "/IMG_%04d.JPG", file);
    if (strncmp(j + 8, want, 13) || (j[21] && j[21] != ' ' && j[21] != '\n')) return 0;
    r += 4;
    while (r[n] && r[n] != ' ' && r[n] != '\n' && n < 23) n++;
    if (n != 20 || strncmp(r + 16, ".RAW", 4)) return 0;
    memcpy(out, r, n);
    out[n] = 0;
    return 1;
}
static int backup_find(int dir, int file, char *out)
{
    static char buf[512 + 256];
    int fd = open("A/CHDK/FILM/NATIVE.TXT", O_RDONLY, 0777), have = 0, found = 0, n;
    if (fd < 0) return 0;
    while ((n = read(fd, buf + have, 512)) > 0) {
        char *p = buf, *e;
        have += n;
        buf[have] = 0;
        while ((e = strchr(p, '\n'))) { *e = 0; if (backup_match(p, dir, file, out)) found = 1; p = e + 1; }
        have -= p - buf;
        if (have > 255) have = 0;
        memcpy(buf, p, have);
    }
    close(fd);
    return found;
}
static unsigned raw_fingerprint(const unsigned char *raw)
{
    unsigned i, s = 0;
    for (i = 0; i < RAW_SIZE; i += 65536) s = s * 31 + raw[i] + (raw[i + 7] << 8);
    return s;
}
static int load_backup(unsigned char *raw, const char *src)
{
    int fd = open(src, O_RDONLY, 0777), rc = 0;
    unsigned off;
    if (fd < 0) return -21;
    for (off = 0; off < RAW_SIZE;) {
        unsigned n = RAW_SIZE - off;
        if (n > 262144) n = 262144;
        if (read(fd, raw + off, n) != (int)n) { rc = -22; break; }
        off += n;
        if (off % 1048576 == 0) msleep(10);
    }
    close(fd);
    return rc;
}

static const unsigned char groups[5] = {3, 6, 7, 19, 20};
static unsigned result[32];

static void lg(const char *s)
{
    int fd = open("A/CHDK/FILM/DGPLAY.TXT", O_WRONLY | O_CREAT | O_APPEND, 0777);
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
static void resources(const char *when)
{
    char t[200];
    int i, u, k;
    sprintf(t, "%s engine resources (group: counts for units 0-15)\n", when);
    lg(t);
    for (i = 0; i < 5; i++) {
        k = sprintf(t, "  g%-2u:", groups[i]);
        for (u = 0; u < 16; u++) k += sprintf(t + k, " %x", RES_CNT[groups[i] * 16 + u]);
        t[k++] = '\n'; t[k] = 0;
        lg(t);
    }
}
static int groups_busy(void)
{
    int i, u;
    for (i = 0; i < 5; i++) for (u = 0; u < 16; u++) if (RES_CNT[groups[i] * 16 + u]) return 1;
    return 0;
}
static unsigned unit11_mask(void)
{
    unsigned m = 0;
    int i;
    for (i = 0; i < 5; i++) if (RES_CNT[groups[i] * 16 + 11]) m |= 1u << i;
    return m;
}
#define JPEG_BUF_END 0x48000000u                    /* Canon's JPEG buffer 0x47109800 + 0xef6800 */
static unsigned jpeg_main, jpeg_hdr;                /* from the shot record (+0x5c, +0x64) */
#define JPEG_MAIN    jpeg_main
#define JPEG_HDR     jpeg_hdr
#define JPEG_OUT     ((volatile unsigned *)jpeg_main)
#define KEEP         0xffffffffu                    /* step(): leave extra[0..1] as they are */
/* One develop message as SsCreateJpeg sends it; waits up to 20 s for its result. */
static unsigned arm_id;
static char backup_src[24];                         /* A/FGRAW/Rnnnnnnn.RAW */
#define REC_SIZE     0xdb8
static unsigned char saved_rec[REC_SIZE];
static int stage_armed;                             /* film_stage_finish still owed */
static int step_wait = 400;                         /* 50 ms polls per step */
static int step(unsigned mode, unsigned space, unsigned out, unsigned out_size, unsigned p0c, const char *what)
{
    char t[220];
    int t0, n, i;
    for (i = 0; i < 32; i++) result[i] = 0xdeadbeef;
    PARAMS[0] = 0;
    PARAMS[0x0c / 4] = p0c;
    PARAMS[0x34 / 4] = mode;
    PARAMS[0x38 / 4] = space;
    if (out != KEEP) { EXTRA[0] = out; EXTRA[1] = out_size; }
    sprintf(t, "type %x (%s): space=%x extra=%08x/%x\n", mode, what, space, EXTRA[0], EXTRA[1]);
    lg(t);
    t0 = get_tick_count();
    DEV_POST(PARAMS, EXTRA, result);
    sprintf(t, "  posted after %d ms\n", get_tick_count() - t0);
    lg(t);
    for (n = 0; n < step_wait && result[0] == 0xdeadbeef; n++) {
        msleep(50);
        sprintf(t, "  t+%5d dp=%u flag=%u 6c=%08x b4=%08x b8=%08x sharemem=%d result0=%08x\n", get_tick_count() - t0,
                DP_COUNT, DEV_FLAG, DESC[0x6c / 4], DESC[0xb4 / 4], DESC[0xb8 / 4], SHAREMEM, result[0]);
        lg(t);
    }
    sprintf(t, "  %s after %d ms: status=%08x dp=%u flag=%u sharemem=%d\n", result[0] == 0xdeadbeef ? "NO RESULT" : "result",
            get_tick_count() - t0, result[0], DP_COUNT, DEV_FLAG, SHAREMEM);
    lg(t);
    words("  result", result, 16);
    return result[0] != 0xdeadbeef && !(result[0] & 1);
}
static unsigned yuv_fingerprint(void)
{
    unsigned i, s = 0;
    for (i = 0; i < 0x100000 / 4; i += 997) s = s * 31 + YUV[i];
    return s;
}

/* R25: copy only these byte ranges of the saved record into the fresh one.
 * USB dumps of real captures (tools/dg_rec_usb.py) put the white-balance
 * levels at +0x78d..+0x7b7 (G R B G as 16-bit values at odd offsets, then
 * more WB results) and the WB mode at +0x5a9; +0xd27..+0xd67 holds pointers
 * of the session that made the record and +0x5a5 an address inside that
 * shot's raw buffer. A/CHDK/FILM/DGPLAY.RNG overrides the default: one
 * "offset length" pair (hex) per line, offsets 8..0xdb8. */
static void hexbytes(char *t, volatile const unsigned char *p, unsigned n)
{
    unsigned i;
    for (i = 0; i < n && i < 48; i++) t += sprintf(t, "%02x", p[i]);
    *t = 0;
}
static void restore_ranges(volatile unsigned char *rec)
{
    static char buf[400];
    unsigned off[16], len[16], k = 0, i, j;
    char t[260], a[100], b[100];
    int fd = open("A/CHDK/FILM/DGPLAY.RNG", O_RDONLY, 0777), n = 0;
    if (fd >= 0) { n = read(fd, buf, sizeof(buf) - 1); close(fd); }
    if (n > 0) {
        char *p = buf;
        buf[n] = 0;
        while (*p && k < 16) {
            unsigned o = strtoul(p, &p, 16), l = strtoul(p, &p, 16);
            if (l && o >= 8 && o + l <= REC_SIZE) { off[k] = o; len[k] = l; k++; }
            while (*p && *p != '\n') p++;
            if (*p) p++;
        }
        sprintf(t, "restore ranges from A/CHDK/FILM/DGPLAY.RNG: %u\n", k);
    } else {
        off[0] = 0x78d; len[0] = 0x2b;
        off[1] = 0x5a9; len[1] = 1;
        k = 2;
        sprintf(t, "restore ranges: default (WB levels +78d..+7b7, WB mode +5a9)\n");
    }
    lg(t);
    for (i = 0; i < k; i++) {
        hexbytes(a, rec + off[i], len[i]);
        hexbytes(b, saved_rec + off[i], len[i]);
        sprintf(t, "  +%03x len %x: today %s\n         saved %s\n", off[i], len[i], a, b);
        lg(t);
        for (j = 0; j < len[i]; j++) rec[off[i] + j] = saved_rec[off[i] + j];
    }
}

/* R24: today's settings record against the backup's saved one. */
static void dry_run(void)
{
    char t[200], set[24], dump[32];
    unsigned rec, i, a, b;
    int fd, n = -1, diff = 0, k;
    strcpy(set, backup_src);
    strcpy(set + 16, ".SET");
    fd = open(set, O_RDONLY, 0777);
    if (fd >= 0) { n = read(fd, saved_rec, REC_SIZE); close(fd); }
    sprintf(t, "dry run: saved record %s read %d bytes\n", set, n);
    lg(t);
    rec = PROP_ALLOC();
    sprintf(t, "settings record: new %08x in_use=%u (params had %08x)\n", rec, rec ? ((volatile unsigned *)rec)[1] : 0,
            PARAMS[0x58 / 4]);
    lg(t);
    if (!rec) { lg("stopped: no settings record\ndone\n"); return; }
    for (k = 1; k <= 9; k++) {
        struct stat st;
        sprintf(dump, "A/CHDK/FILM/REC24_%d.BIN", k);
        if (stat(dump, &st)) break;
    }
    if (k > 9) lg("not written: REC24_1..9.BIN all exist\n");
    else {
        fd = open(dump, O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (fd >= 0) {
            static unsigned char copy[REC_SIZE];
            for (i = 0; i < REC_SIZE; i++) copy[i] = ((volatile unsigned char *)rec)[i];
            write(fd, copy, REC_SIZE);
            close(fd);
        }
        sprintf(t, "today's record -> %s (%s)\n", dump, fd >= 0 ? "written" : "open failed");
        lg(t);
    }
    if (n == REC_SIZE) {
        lg("words that differ (offset [index]: today saved):\n");
        for (i = 8; i < REC_SIZE; i += 4) {
            a = ((volatile unsigned *)rec)[i / 4];
            b = *(unsigned *)(saved_rec + i);
            if (a == b) continue;
            diff++;
            sprintf(t, "  +%03x [%3u]: %08x %08x\n", i, i / 4, a, b);
            lg(t);
        }
        sprintf(t, "%d of %d words differ\n", diff, (REC_SIZE - 8) / 4);
        lg(t);
    }
    PROP_FREE(rec);
    lg("settings record freed (nothing developed)\ndone\n");
}

static int play_main(void)
{
    char t[240];
    unsigned shot, rec, old_rec, i;
    volatile unsigned *s;
    (void)0;
    sprintf(t, "\ndgplay R27 tick=%d play=%d rec=%d sharemem=%d bufcfg=%u\n", get_tick_count(),
            camera_info.state.mode_play, camera_info.state.mode_rec, SHAREMEM, BUF_STATE[1]);
    lg(t);
    sprintf(t, "live view: running174=%u paused48=%u mjpeg60=%u mode138=%u\n", LV_STATE[0x174 / 4], LV_STATE[0x48 / 4],
            LV_STATE[0x60 / 4], LV_STATE[0x138 / 4]);
    lg(t);
    sprintf(t, "WBInteg: init14=%u state10c=%u busy=%u/%u/%u\n", WBI_STATE[0x14 / 4], WBI_STATE[0x10c / 4],
            WBI_STATE[0x110 / 4], WBI_STATE[0x114 / 4], WBI_STATE[0x118 / 4]);
    lg(t);
    resources("at start");
    words("ss_state", SS_STATE, 12);
    words("params", PARAMS, 32);
    words("extra", EXTRA, 8);
    words("dev_state", DEV_STATE, 16);
    shot = SS_STATE[4];
    s = (volatile unsigned *)shot;
    if (ram(shot)) words("shot", s, 48);
    sprintf(t, "yuv fingerprint before %08x\n", yuv_fingerprint());
    lg(t);
    if (!camera_info.state.mode_play) { lg("refused: run it in the gallery (playback), after one JPEG-only shot\ndone\n"); return 0; }
    sprintf(t, "checks: groups_free=%d task_queue=%d shot=%d image_no=%d jpeg_only=%d\n", !groups_busy(),
            DEV_STATE[0x2c / 4] != 0, ram(shot), PARAMS[0x50 / 4] != 0, SS_STATE[8] == 0);
    lg(t);
    if (!DRY_RUN && (groups_busy() || !DEV_STATE[0x2c / 4] || !ram(shot) || !PARAMS[0x50 / 4] || SS_STATE[8])) {
        lg("refused: an engine group is held, or no develop task / shot parameters, or RAW is on\ndone\n");
        return 0;
    }
    {
        unsigned pid = film_playback_image_id();
        int dir = (pid << 4) >> 22, file = (pid << 14) >> 18, rc;
        char src[24];
        struct stat st;
        unsigned char *raw = (unsigned char *)hook_raw_image_addr();
        sprintf(t, "photo on screen: id=%08x -> %03d-%04d; raw buffer %08x, shot's %08x\n", pid, dir, file,
                (unsigned)raw, s[0x2c / 4]);
        lg(t);
        sprintf(t, "core check: film_playback_image_id at %08x (expected %08x)\n", (unsigned)film_playback_image_id,
                CORE_PLAYBACK_ID);
        lg(t);
        if ((unsigned)film_playback_image_id != CORE_PLAYBACK_ID ||
            *(volatile unsigned *)0x003e2b10 != ARM_CODE0 || *(volatile unsigned *)0x003e31bc != FINISH_CODE0) { lg("refused: not the core build R27 was made for (the sharpening core, DISKBOOT sha256 c8551241)\ndone\n"); return 0; }
        if (!pid) { lg("refused: no photo number on screen (press DISP until it shows)\ndone\n"); return 0; }
        if (!backup_find(dir, file, src)) { lg("refused: that photo has no sensor backup in NATIVE.TXT\ndone\n"); return 0; }
        if (stat(src, &st) || st.st_size != RAW_SIZE) {
            sprintf(t, "refused: backup %s missing or not %u bytes\ndone\n", src, RAW_SIZE);
            lg(t);
            return 0;
        }
        strcpy(backup_src, src);
        if (DRY_RUN) { dry_run(); return 0; }
        for (arm_id = 0, i = 9; i < 16; i++) arm_id = arm_id * 10 + (src[i] - '0');   /* A/FGRAW/Rnnnnnnn.RAW */
        if ((unsigned)raw != s[0x2c / 4]) { lg("refused: the raw buffer is not the one the develop reads\ndone\n"); return 0; }
        sprintf(t, "loading %s into %08x (raw fingerprint before %08x)\n", src, (unsigned)raw, raw_fingerprint(raw));
        lg(t);
        i = get_tick_count();
        rc = load_backup(raw, src);
        sprintf(t, "  load rc=%d after %d ms, raw fingerprint after %08x\n", rc, get_tick_count() - i, raw_fingerprint(raw));
        lg(t);
        if (rc) { lg("stopped: backup load failed\ndone\n"); return 0; }
    }
    old_rec = PARAMS[0x58 / 4];
    rec = PROP_ALLOC();
    sprintf(t, "settings record: old %08x, new %08x in_use=%u\n", old_rec, rec, rec ? ((volatile unsigned *)rec)[1] : 0);
    lg(t);
    if (!rec) { lg("stopped: no settings record\ndone\n"); return 0; }
    {
        char set[24];
        int fd, n = -1, diff = 0;
        strcpy(set, backup_src);
        strcpy(set + 16, ".SET");
        fd = open(set, O_RDONLY, 0777);
        if (fd >= 0) { n = read(fd, saved_rec, REC_SIZE); close(fd); }
        if (n == REC_SIZE) {
            restore_ranges((volatile unsigned char *)rec);
            sprintf(t, "restored ranges from %s (not the whole record)\n", set);
        } else sprintf(t, "no saved settings (%s: read %d): developing with today's settings\n", set, n);
        lg(t);
    }
    PARAMS[0x58 / 4] = rec;
    s[0x18 / 4] = rec;
    if (!step(1, 0, KEEP, 0, 1, "raw -> YUV develop, extra as R15 had it")) goto out;
    sprintf(t, "yuv fingerprint after the develop %08x\n", yuv_fingerprint());
    lg(t);
    if (!step(5, 0, KEEP, 0, 1, "develop done, extra kept")) goto out;
    jpeg_main = s[0x5c / 4];
    jpeg_hdr = s[0x64 / 4];
    sprintf(t, "JPEG output from the shot record: main %08x, header %08x\n", jpeg_main, jpeg_hdr);
    lg(t);
    if (jpeg_main < 0x47109800u || jpeg_main >= JPEG_BUF_END - 0x400000u || jpeg_hdr + 0x2000u > jpeg_main) {
        lg("stopped: the shot record's JPEG addresses are not inside Canon's JPEG buffer\n");
        goto out;
    }
    for (i = 0; i < 0x1000 / 4; i++) JPEG_OUT[i] = 0xa5a5a5a5;
    sprintf(t, "arming Zrno's stage (film_stage_arm(%u)), after types 1 and 5\n", arm_id);
    lg(t);
    STAGE_ARM(arm_id);
    stage_armed = 1;
    lg("  armed\n");
    step_wait = 1200;                                 /* the CPU colour pass takes about 18 s */
    if (!step(2, JPEG_BUF_END - jpeg_main - 0x200u - 0x3c00u, JPEG_MAIN, JPEG_BUF_END - jpeg_main - 0x200u, 1,
              "main JPEG, with Zrno's colour pass")) goto out;
    step_wait = 400;
    words("jpeg head", JPEG_OUT, 16);
    if (!step(0xa, 0x2000, JPEG_HDR, 0x1e00, 0, "thumbnail")) goto out;
    words("header head", (volatile unsigned *)JPEG_HDR, 16);
    STAGE_FINISH();
    stage_armed = 0;
    lg("film_stage_finish called (STAGELOG.TXT gets the colour pass line about 1 s later)\n");
    PARAMS[0x0c / 4] = 1;
    {
        unsigned hdr = s[0x68 / 4], len = 0;
        const unsigned char *j = (const unsigned char *)JPEG_MAIN;
        int fd;
        for (i = 0; i + 1 < JPEG_BUF_END - jpeg_main - 0x200u; i++) if (j[i] == 0xff && j[i + 1] == 0xd9) { len = i + 2; break; }
        sprintf(t, "main JPEG: %s, end marker at +%x (%u bytes); header %u bytes; old JPEG size %u\n",
                JPEG_OUT[0] == 0xa5a5a5a5 ? "NOT written (marker still there)" : "written", len, len, hdr, s[0x60 / 4]);
        lg(t);
        if (len && JPEG_OUT[0] != 0xa5a5a5a5) {
            fd = open("A/REDEV27.JPG", O_WRONLY | O_CREAT | O_TRUNC, 0777);
            if (fd >= 0) {
                for (i = 0; i < len; i += 0x40000) write(fd, j + i, len - i < 0x40000 ? len - i : 0x40000);
                close(fd);
                sprintf(t, "wrote A/REDEV27.JPG: %u bytes (main JPEG only)\n", len);
                lg(t);
            } else lg("could not open A/REDEV27.JPG\n");
        }
    }
out:
    if (stage_armed) { STAGE_FINISH(); stage_armed = 0; lg("film_stage_finish called on the way out\n"); }
    resources("at the end");
    sprintf(t, "sharemem at the end %d\n", SHAREMEM);
    lg(t);
    PROP_FREE(rec);
    lg("settings record freed\n");
    lg("done\n");
    return 0;
}

/* R26: run in a task of its own (16 KB stack). Started from Lua over USB
 * (call_func_ptr(module_run, "dgplay.flt")), R25 died before its first log
 * line: Lua's frames, module_load and R25's buffers all sat on PhySw's 8 KB
 * stack. _run now only logs and starts the task, from the menu too. */
static volatile int play_busy;
static void play_task(void)
{
    play_main();
    play_busy = 0;
    ExitTask();
}
int _run()
{
    char t[80];
    if (play_busy) { lg("dgplay R27: already running\n"); return 0; }
    play_busy = 1;
    sprintf(t, "\ndgplay R27 start requested tick=%d\n", get_tick_count());
    lg(t);
    CreateTask("FilmPlay", 0x19, 0x4000, play_task);
    return 0;
}

int _module_can_unload() { return !play_busy; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgplay_sym;
libdgplay_sym _libdgplay = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"Playback develop probe (R27)",
    MTYPE_TOOL,
    &_libdgplay.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
