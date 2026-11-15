/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe R22a (G12 1.00G only): save each
 * processed shot's develop settings next to its sensor backup.
 *
 * A capture allocates a settings record (PropertyCase entry, 0xff895680:
 * 0xdb8 bytes; word 0 its semaphore, word 1 "in use", the camera's settings
 * from +8) and keeps its handle at shot + 0x18; the develop reads white
 * balance, My Colors and the rest from it, and SsCreateJpeg frees it once the
 * JPEG is saved. The gallery re-develop (dgplay) has to allocate a fresh one,
 * a copy of the *current* settings, so it develops an old backup with today's
 * white balance. This module saves the original record instead.
 *
 * Run once in shooting mode: it installs Zrno's research hook
 * (film_stage_probe, called in Canon's develop task right before the colour
 * pass, on Film-processing shots only, which are the ones with a backup) and
 * a background task. The hook copies the shot's record into memory (no card
 * I/O in Canon's task). When NATIVE.TXT's last line says that shot's JPEG
 * was saved, the task writes the copy as A/FGRAW/Rnnnnnnn.SET beside that
 * line's backup. Run it again to stop. Log: A/CHDK/FILM/DGSAVE.TXT.
 * Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "module_def.h"
#include "clock.h"
#include "task.h"

extern void (*film_stage_probe)(unsigned char *frame);
extern int film_ui_busy(void);

#define REC_SIZE   0xdb8
#define SS_STATE   ((volatile unsigned *)0x76d8)    /* [4] the shot record */

static unsigned char rec_copy[REC_SIZE];
static volatile int running, captured;
static volatile unsigned cap_handle, cap_shot, cap_image_no, cap_tick;

static void lg(const char *s)
{
    int fd = open("A/CHDK/FILM/DGSAVE.TXT", O_WRONLY | O_CREAT | O_APPEND, 0777);
    if (fd >= 0) { write(fd, s, strlen(s)); close(fd); }
}
static int ram(unsigned a) { return a >= 0x1000 && a < 0x00800000; }

/* In Canon's develop task: copy only, no calls into Canon, no card I/O. */
static void hook(unsigned char *frame)
{
    unsigned shot = SS_STATE[4], h, i;
    (void)frame;
    if (!running || captured || !camera_info.state.mode_rec || !ram(shot)) return;
    h = ((volatile unsigned *)shot)[0x18 / 4];
    if (!ram(h) || ((volatile unsigned *)h)[1] != 1) return;
    for (i = 0; i < REC_SIZE; i++) rec_copy[i] = ((volatile unsigned char *)h)[i];
    cap_handle = h;
    cap_shot = shot;
    cap_image_no = ((volatile unsigned *)shot)[0x20 / 4];
    cap_tick = get_tick_count();
    captured = 1;
}

/* The raw= name of NATIVE.TXT's last "jpeg_saved" line, into out (24 bytes). */
static int last_saved_backup(char *out)
{
    static char buf[512 + 256];
    int fd = open("A/CHDK/FILM/NATIVE.TXT", O_RDONLY, 0777), have = 0, n, found = 0;
    if (fd < 0) return 0;
    while ((n = read(fd, buf + have, 512)) > 0) {
        char *p = buf, *e;
        have += n;
        buf[have] = 0;
        while ((e = strchr(p, '\n'))) {
            char *r;
            *e = 0;
            found = 0;
            if (strstr(p, "stage=jpeg_saved") && (r = strstr(p, "raw=A/FGRAW/R"))) {
                int k = 0;
                r += 4;
                while (r[k] && r[k] != ' ' && k < 23) k++;
                if (k == 20) { memcpy(out, r, 20); out[20] = 0; found = 1; }
            }
            p = e + 1;
        }
        have -= p - buf;
        if (have > 255) have = 0;
        memcpy(buf, p, have);
    }
    close(fd);
    return found;
}

static void save_task(void)
{
    char t[200], raw[24], set[24];
    int fd, waited;
    while (running) {
        msleep(200);
        if (!captured) continue;
        for (waited = 0; waited < 300 && (film_ui_busy() || (int)(get_tick_count() - cap_tick) < 1500); waited++) msleep(200);
        msleep(1500);                                 /* NATIVE.TXT gets jpeg_saved from SpyTask */
        if (!last_saved_backup(raw)) {
            lg("no jpeg_saved line in NATIVE.TXT: nothing written\n");
        } else {
            strcpy(set, raw);
            strcpy(set + 16, ".SET");
            fd = open(set, O_WRONLY | O_CREAT | O_TRUNC, 0777);
            if (fd >= 0) {
                write(fd, rec_copy, REC_SIZE);
                close(fd);
            }
            sprintf(t, "shot %08x image %08x record %08x -> %s (%s); words 2-9: %08x %08x %08x %08x %08x %08x %08x %08x\n",
                    cap_shot, cap_image_no, cap_handle, set, fd >= 0 ? "written" : "open failed",
                    ((unsigned *)rec_copy)[2], ((unsigned *)rec_copy)[3], ((unsigned *)rec_copy)[4], ((unsigned *)rec_copy)[5],
                    ((unsigned *)rec_copy)[6], ((unsigned *)rec_copy)[7], ((unsigned *)rec_copy)[8], ((unsigned *)rec_copy)[9]);
            lg(t);
        }
        captured = 0;
    }
    if (film_stage_probe == hook) film_stage_probe = 0;
    lg("stopped\n");
    ExitTask();
}

int _run()
{
    char t[80];
    if (running) {
        running = 0;                                  /* the task removes the hook and exits */
        lg("stop requested\n");
        return 0;
    }
    if (film_stage_probe && film_stage_probe != hook) {
        lg("refused: another research hook (film_stage_probe) is installed\n");
        return 0;
    }
    captured = 0;
    running = 1;
    film_stage_probe = hook;
    CreateTask("FilmSaveSet", 0x1A, 0x800, save_task);
    sprintf(t, "\ndgsave R22a started tick=%d rec=%d\n", get_tick_count(), camera_info.state.mode_rec);
    lg(t);
    return 0;
}

int _module_can_unload() { return !running && film_stage_probe != hook; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgsave_sym;
libdgsave_sym _libdgsave = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"Save develop settings (R22a)",
    MTYPE_TOOL,
    &_libdgsave.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
