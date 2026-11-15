/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Re-develop module (redev.flt), run from the Film
 * simulation menu's "Re-develop photo".
 *
 * In the gallery it finds the photo on screen, looks up that JPEG's sensor
 * backup in CHDK/FILM/NATIVE.TXT, and has the film code load the backup as
 * the next capture's sensor data (film_raw_redevelop). It then does the rest itself:
 * closes the menu into a quiet screen of its own while CHDK keeps the keys
 * (as in <ALT> mode, so its simulated presses reach Canon, as scripts do):
 * no <ALT> help, buttons ignored, and the developing panel shows as for any
 * shot. It switches to shooting mode, presses the shutter, waits for the
 * film processing, leaves <ALT> mode and returns to the gallery on the new
 * JPEG (the original is kept). Each step is logged. Each run is logged to CHDK/FILM/REDEV.TXT.
 *
 * G12 1.00G only: the gallery's photo is read from Canon's playback
 * information text (playback_image_id below). film_raw_redevelop() (grade
 * without a second backup) comes from the core; the rest uses CHDK's
 * module exports. */
#ifndef FILM_REDEV_HOST
#include "camera_info.h"
#include "stdlib.h"
#include "gui.h"
#include "gui_mbox.h"
#include "raw.h"
#include "shooting.h"
#include "properties.h"
#include "module_def.h"
#include "keyboard.h"
#include "action_stack.h"
#include "conf.h"
#include "film_ui.h"
#include "clock.h"
#include "film_raw.h"
#endif

/* From one NATIVE.TXT line, the backup of IMG_<file>.JPG in DCIM folder
 * <dir>: copies "A/FGRAW/Rnnnnnnn.RAW" to out (24 bytes) and returns 1. */
int film_redev_match(const char *line, int dir, int file, char *out)
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

/* The last backup NATIVE.TXT lists for that photo, read line by line. */
int film_redev_find(int dir, int file, char *out)
{
    static char buf[512 + 256];
    int fd = open("A/CHDK/FILM/NATIVE.TXT", O_RDONLY, 0777), have = 0, found = 0, n;
    if (fd < 0) return 0;
    while ((n = read(fd, buf + have, 512)) > 0) {
        char *s = buf, *e;
        have += n;
        buf[have] = 0;
        while ((e = strchr(s, '\n'))) {
            *e = 0;
            if (film_redev_match(s, dir, file, out)) found = 1;
            s = e + 1;
        }
        have -= s - buf;
        if (have > 255) have = 0;                  /* an over-long line: drop it */
        memcpy(buf, s, have);
    }
    close(fd);
    return found;
}

#ifndef FILM_REDEV_HOST
/* The photo the G12 gallery shows. Canon's playback information display
 * (PBInformation.c, ROM 0xff9fdf30) prints the shown photo's number with
 * "%03d-%04d" into the text buffer at 0xb7a58, which it draws on screen; it
 * stays put while a CHDK menu covers the gallery. Returned packed as Canon
 * does (folder in bits 18-27, file number in bits 4-17), or 0 outside
 * playback or when the buffer holds no number. (A core export until the
 * white-balance restore needed its bytes in the core.) */
static unsigned playback_image_id(void)
{
    volatile const char *t = (volatile const char *)0xb7a58;
    int i, dir = 0, file = 0;
    if (!camera_info.state.mode_play) return 0;
    for (i = 0; i < 8; i++) {
        char c = t[i];
        if (i == 3) { if (c != '-') return 0; continue; }
        if (c < '0' || c > '9') return 0;
        if (i < 3) dir = dir * 10 + c - '0'; else file = file * 10 + c - '0';
    }
    if (t[8] || dir < 100 || dir > 999 || file < 1 || file > 9999) return 0;
    return (unsigned)dir << 18 | (unsigned)file << 4;
}

static int running, dir, file, scan;
static unsigned id;
static char msg[160], src[24];

/* One line per run, with what the gallery lookup saw: the playback info
 * display's two number texts, and where in low RAM the ID also appears. */
static void log_line(const char *result)
{
    char line[400];
    int fd = open("A/CHDK/FILM/REDEV.TXT", O_WRONLY | O_CREAT | O_APPEND, 0777), n, k;
    if (fd < 0) return;
    n = sprintf(line, "id=0x%08x photo=%03d-%04d play=%d src=%s result=%s", id, dir, file, camera_info.state.mode_play, src, result);
    for (k = 0; k < 2; k++) {                      /* the info display's texts, printable bytes only */
        const volatile char *t = (const volatile char *)(k ? 0xb8bec : 0xb7a58);
        int i;
        n += sprintf(line + n, k ? " txt_b8bec=" : " txt_b7a58=");
        for (i = 0; i < 12 && t[i]; i++) line[n++] = (t[i] >= 32 && t[i] < 127) ? t[i] : '?';
    }
    n += sprintf(line + n, " ram=");
    if (id && scan) {                              /* only while looking the photo up */
        unsigned a, hits = 0;
        for (a = 0x1000; a < 0x400000 && hits < 12; a += 4)
            if (*(volatile unsigned *)a == id) { n += sprintf(line + n, "%x;", a); hits++; }
    }
    line[n++] = '\n';
    write(fd, line, n);
    close(fd);
}

static void done(unsigned int btn) { (void)btn; running = 0; }

static void say(const char *text, int buttons, void (*on)(unsigned int))
{
    if (text != msg) strcpy(msg, text);
    running = 1;                                    /* stay loaded while the box shows msg */
    gui_mbox_init((int)"Re-develop", (int)msg, buttons | MBOX_TEXT_CENTER, on);
}

/* The screen while re-developing: draws nothing itself (the developing
 * panel draws over it, as on the shooting screen) and ignores buttons. */
static void quiet_draw(int enforce) { (void)enforce; }
static int quiet_kbd(void) { return 0; }
static gui_handler quiet_gui = {GUI_MODE_NONE, quiet_draw, quiet_kbd, 0, 0, 0};

/* The shot, as an action stack (each step returns 1 when done, 0 to wait):
 * wait for shooting mode, half-press until metered, full-press, release,
 * wait for the film processing, then back to the gallery. */
/* Steps are noted in memory and written with the final line: no file access
 * from the keyboard task while Canon captures. */
static int t0;
static char trail[96];
static void note(const char *step)
{
    if (strlen(trail) + strlen(step) + 2 < sizeof(trail)) {
        if (trail[0]) strcat(trail, ",");
        strcat(trail, step);
    }
}
static void cancel(void)                              /* nothing captured: don't leave the develop armed */
{
    film_raw_redevelop(0);
}
/* The shot only carries the backup's sensor data, so the flash must not
 * fire: it is turned off for the shot (Canon's flash mode: 0 auto, 1 on,
 * 2 off) and the photographer's setting put back afterwards. On the G12
 * that alone still fires: the half-press decides from Canon's own setting
 * and leaves the decision in the flash-fire property, which the capture
 * reads (CHDK's flash override sets it the same way), so fire_off() clears
 * it once metered. */
static int flash_saved = -1;
static void flash_off(void)
{
    int m = shooting_get_prop(camera_info.props.flash_mode);
    if (m == 2) return;
    flash_saved = m;
    shooting_set_prop(camera_info.props.flash_mode, 2);
    note("flash_off");
}
static void flash_restore(void)
{
    if (flash_saved < 0) return;
    shooting_set_prop(camera_info.props.flash_mode, flash_saved);
    flash_saved = -1;
}
static void fire_off(void)
{
    char s[16];
    int f = shooting_get_prop(camera_info.props.flash_fire);
    sprintf(s, "fire=%d", f);                          /* what metering decided */
    note(s);
    if (f) shooting_set_prop(camera_info.props.flash_fire, 0);
}
static int step_finish(void)
{
    int t = get_tick_count();
    if (film_ui_busy() && t - t0 < 180000) return 0;
    if (t - t0 < 900) return 0;                       /* let "saved" show a moment */
    action_pop_func(0);
    flash_restore();
    note(film_ui_busy() ? "timeout" : "done");
    log_line(trail);
    shooting_set_playrec_mode(0);                     /* the gallery opens on the new photo */
    gui_set_mode(&altGuiHandler);                     /* off the module's screen before it unloads */
    exit_alt();
    running = 0;
    return 1;
}
/* After the full press: wait until the film processing has the frame (CHDK's
 * RAW develop path never marks the capture as processing, so its own
 * "shot done" state doesn't come). */
static int step_wait_shot(void)
{
    if (!film_ui_busy() && get_tick_count() - t0 < 15000) return 0;
    action_pop_func(0);
    if (film_ui_busy()) note("shot");
    else { note("no_shot"); cancel(); }
    t0 = get_tick_count();
    return 1;
}
static int step_wait_metered(void)
{
    if (!shooting_in_progress() && get_tick_count() - t0 < 5000) return 0;
    action_pop_func(0);
    if (shooting_in_progress()) {
        note("metered");
        fire_off();                                   /* before the full press */
        t0 = get_tick_count();
        action_push_func(step_wait_shot);
        action_push_click(KEY_SHOOT_FULL);
    } else {
        note("no_half_press");
        cancel();
    }
    return 1;
}
static int step_wait_rec(void)
{
    int t = get_tick_count() - t0;
    if ((!camera_info.state.mode_rec || t < 1500) && t < 10000) return 0;   /* lens out, screen ready */
    action_pop_func(0);
    if (!camera_info.state.mode_rec) {
        note("no_shooting_mode");
        log_line(trail);
        cancel();
        gui_set_mode(&altGuiHandler);
        exit_alt();
        running = 0;
        return 1;
    }
    note("shooting_mode");
    flash_off();                                      /* before the half-press meters */
    t0 = get_tick_count();
    action_push_func(step_finish);
    action_push_release(KEY_SHOOT_HALF);
    action_push_func(step_wait_metered);
    action_push_press(KEY_SHOOT_HALF);
    return 1;
}

int _run()
{
    struct stat st;
    if (running) return 0;                            /* one re-develop at a time */
    /* Busy before anything slow: SpyTask's module_tick_unloader polls
     * _module_can_unload every tick, and the NATIVE.TXT lookup and log line
     * below take long enough (a 66 KB file, a 4 MB RAM scan) for it to
     * unload this module mid-_run. quiet_gui was then installed from freed
     * memory, and PhySw jumped to 0x10000 when the shot started. The early
     * returns leave it set: say() shows a box and done() clears it. */
    running = 1;
    scan = 1;
    id = playback_image_id();
    dir = (id << 4) >> 22;
    file = (id << 14) >> 18;
    src[0] = 0;
    if (!id) {
        log_line(camera_info.state.mode_play ? "no_photo_id" : "not_playback");
        say("The photo on screen could not be identified. Press DISP until the photo number (like 100-0123) shows, then try again.", MBOX_BTN_OK, done);
        return 0;
    }
    if (!film_redev_find(dir, file, src)) {
        log_line("no_backup");
        sprintf(msg, "%03d-%04d has no sensor backup (only photos taken with Processing On have one).", dir, file);
        say(msg, MBOX_BTN_OK, done);
        return 0;
    }
    if (stat(src, &st) || st.st_size != camera_sensor.raw_size) {
        log_line("backup_missing");
        sprintf(msg, "The backup for %03d-%04d (%s) is missing or incomplete.", dir, file, src);
        say(msg, MBOX_BTN_OK, done);
        return 0;
    }
    film_raw_redevelop(src);
    log_line("started");
    scan = 0;
    trail[0] = 0;
    gui_set_mode(&quiet_gui);                        /* close the menu; keys stay with CHDK */
    shooting_set_playrec_mode(1);
    t0 = get_tick_count();
    action_stack_create(step_wait_rec);
    return 0;
}

int _module_can_unload() { return running == 0; }
int _module_exit_alt() { return 0; }

typedef struct { base_interface_t base; } libredev_sym;
libredev_sym _libredev = {{0, 0, _module_can_unload, _module_exit_alt, _run}};

ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM,
    sizeof(ModuleInfo),
    {1, 0},                     // Module version
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE,
    ANY_PLATFORM_ALLOWED,       // the camera-specific parts are core exports
    (int)"Re-develop photo",
    MTYPE_EXTENSION,
    &_libredev.base,
    ANY_VERSION, ANY_VERSION, CAM_SENSOR_VERSION, CAM_INFO_VERSION,
    0x28,
};
#endif
