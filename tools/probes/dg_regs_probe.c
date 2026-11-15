/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe (G12 1.00G only), read only: snapshot
 * the develop's engine-register state on real captures, to find which
 * registers and tables carry Canon's contrast, sharpness, saturation,
 * i-Contrast and noise reduction.
 *
 * What it reads (ROM 1.00G):
 *  - The register shadow. EngDrvOut (0xff8358a4), EngDrvBits and the
 *    Cartridge writes (0xff94e0a0) store every value they write to an engine
 *    register 0xC0Fxxxxx also at 0x400000 + (reg & 0x3FFFF). The shadow
 *    holds the last value Canon wrote to each register of 0xC0F00000 ..
 *    0xC0F3FFFF (256 KB).
 *  - ManageImgTbl's state at 0x6748 and its two hardware tables, which are
 *    loaded by DMA (BltDmac, 0xC0A10000) and so are not in the shadow:
 *      [0x6748+8]  the colour-engine ("Lucky", 0xC0F20000) buffer: the 4 KB
 *                  colour table (kind 1) or a 5,104-byte table (kind 3);
 *      [0x6748+4]  1,008 bytes = 504 16-bit entries for the "angel" block at
 *                  0xC0F21000 (kind 2);
 *      [0x6748+16] 2,016 bytes: the angel base Canon reads back from the
 *                  hardware at start-up, to which 0xff93f8f8 adds the
 *                  per-shot entries.
 *  - The develop parameter blocks around 0xb3ed4 / 0xb3f54.
 *
 * It uses the film stage's research hook (film_stage_probe), called in
 * Canon's develop task with the developed frame right before the colour
 * pass. The hook only copies memory; it calls no Canon function.
 *
 * From CHDK's Tools menu:
 *   1st run (shooting mode): takes an exmem pool that is not in use
 *     (EXMEM_FIRMUP, else EXMEM_FAMODE) and arms the hook for up to 8 shots
 *     with Film processing On. Change one Canon setting between shots.
 *   2nd run: writes A/CHDK/FILM/DGREG0.BIN .. DGREG7.BIN (one per shot) and
 *     DGREG.TXT, frees the pool and disarms. Compare them on the Mac with
 *     tools/dg_regs_diff.py. Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "gui.h"
#include "module_def.h"
#include "clock.h"
#include "exmem.h"
#include "gui_mbox.h"

#define SLOTS 8
#define SHADOW_ADDR 0x400000u
#define SHADOW_SIZE 0x40000u
#define STATE_ADDR 0x6748u
#define STATE_SIZE 0x400u
#define PARAM_ADDR 0xb3e00u
#define PARAM_SIZE 0x300u
#define LUCKY_SIZE 0x1400u                      /* covers the 5,104-byte kind-3 table */
#define ANGEL_SIZE 0x400u                       /* 1,008 bytes used */
#define BASE_SIZE  0x800u                       /* 2,016 bytes used */

/* File layout: header, then the sections in this order. */
enum { OFF_HDR = 0, HDR_SIZE = 0x80,
       OFF_SHADOW = HDR_SIZE,
       OFF_STATE = OFF_SHADOW + SHADOW_SIZE,
       OFF_PARAM = OFF_STATE + STATE_SIZE,
       OFF_LUCKY = OFF_PARAM + PARAM_SIZE,
       OFF_ANGEL = OFF_LUCKY + LUCKY_SIZE,
       OFF_BASE = OFF_ANGEL + ANGEL_SIZE,
       SLOT_SIZE = OFF_BASE + BASE_SIZE };

extern void (*film_stage_probe)(unsigned char *frame);

enum { IDLE, ARMED };
static volatile int state = IDLE;
static unsigned type = 99;
static unsigned char *pool;
static volatile int used;
static char msg[200];
static volatile int showing;
static void shown(unsigned int btn) { (void)btn; showing = 0; }

/* A pointer Canon keeps to one of its buffers: accept only RAM (cached or
 * uncached alias) below 128 MB, word aligned. */
static int ram_ok(unsigned p, unsigned n)
{
    unsigned a = p & ~0x40000000u;
    return p && !(p & 3) && a >= 0x1000u && a + n <= 0x08000000u && (p >> 30) <= 1;
}

static void copy_or_zero(unsigned char *dst, unsigned src, unsigned n)
{
    if (ram_ok(src, n)) memcpy(dst, (const void *)src, n);
    else memset(dst, 0, n);
}

/* In Canon's develop task: copies only, no file access, no waits. */
static void hook(unsigned char *frame)
{
    unsigned char *s;
    unsigned *h, angel, lucky, base;
    (void)frame;
    if (used >= SLOTS) { film_stage_probe = 0; return; }
    s = pool + used * SLOT_SIZE;
    h = (unsigned *)s;
    angel = *(volatile unsigned *)(STATE_ADDR + 4);
    lucky = *(volatile unsigned *)(STATE_ADDR + 8);
    base = *(volatile unsigned *)(STATE_ADDR + 16);
    memcpy(s + OFF_SHADOW, (const void *)SHADOW_ADDR, SHADOW_SIZE);
    memcpy(s + OFF_STATE, (const void *)STATE_ADDR, STATE_SIZE);
    memcpy(s + OFF_PARAM, (const void *)PARAM_ADDR, PARAM_SIZE);
    copy_or_zero(s + OFF_LUCKY, lucky, LUCKY_SIZE);
    copy_or_zero(s + OFF_ANGEL, angel, ANGEL_SIZE);
    copy_or_zero(s + OFF_BASE, base, BASE_SIZE);
    memset(h, 0, HDR_SIZE);
    h[0] = 0x31524744u;                         /* "DGR1" */
    h[1] = 1;                                   /* layout version */
    h[2] = used;
    h[3] = get_tick_count();
    h[4] = SHADOW_ADDR; h[5] = SHADOW_SIZE;
    h[6] = STATE_ADDR;  h[7] = STATE_SIZE;
    h[8] = PARAM_ADDR;  h[9] = PARAM_SIZE;
    h[10] = lucky;      h[11] = LUCKY_SIZE;
    h[12] = angel;      h[13] = ANGEL_SIZE;
    h[14] = base;       h[15] = BASE_SIZE;
    h[16] = ram_ok(lucky, LUCKY_SIZE) | ram_ok(angel, ANGEL_SIZE) << 1 | ram_ok(base, BASE_SIZE) << 2;
    used++;
    if (used >= SLOTS) film_stage_probe = 0;
}

static void save(const char *path, const void *p, int n)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd >= 0) { write(fd, p, n); close(fd); }
}
static void say(const char *s)
{
    if (s != msg) strcpy(msg, s);
    showing = 1;
    gui_mbox_init((int)"Register probe", (int)msg, MBOX_BTN_OK | MBOX_TEXT_CENTER, shown);
}
static void release(void)
{
    film_stage_probe = 0;
    if (pool) exmem_free_uncached(type);
    pool = 0;
    type = 99;
    state = IDLE;
}

int _run()
{
    char t[200];
    exmem_alloc_info ai;
    unsigned n, i;
    if (state == ARMED) {
        char log[600], path[32];
        int k, slots;
        film_stage_probe = 0;                   /* no further snapshots while saving */
        slots = used;
        k = sprintf(log, "dgreg slots=%d slot_bytes=%d\n", slots, SLOT_SIZE);
        for (i = 0; i < (unsigned)slots; i++) {
            const unsigned *h = (const unsigned *)(pool + i * SLOT_SIZE);
            k += sprintf(log + k, "slot %u tick=%u lucky=%08x angel=%08x base=%08x ok=%u\n",
                         i, h[3], h[10], h[12], h[14], h[16]);
            sprintf(path, "A/CHDK/FILM/DGREG%u.BIN", i);
            save(path, pool + i * SLOT_SIZE, SLOT_SIZE);
        }
        save("A/CHDK/FILM/DGREG.TXT", log, k);
        release();
        sprintf(t, "Saved %d snapshot(s): DGREG0.BIN... and DGREG.TXT in CHDK/FILM.", slots);
        say(t);
        return 0;
    }
    if (!camera_info.state.mode_rec) { say("Arm it in shooting mode, then take up to 8 photos with Film processing On."); return 0; }
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
    pool = exmem_alloc_uncached(type, SLOTS * SLOT_SIZE + 64, &ai);
    if (!pool || ai.len < (unsigned)(SLOTS * SLOT_SIZE + 64)) {
        if (pool) exmem_free_uncached(type);
        pool = 0; type = 99;
        say("Could not get the memory for the probe.");
        return 0;
    }
    pool = (unsigned char *)(((unsigned)pool + 31) & ~31u);
    used = 0;
    state = ARMED;
    film_stage_probe = hook;
    say("Armed for 8 photos (Film processing On). Change one setting per photo, then run the probe again to save.");
    return 0;
}

int _module_can_unload() { return state == IDLE && !showing; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgr_sym;
libdgr_sym _libdgr = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"DIGIC register probe",
    MTYPE_TOOL,
    &_libdgr.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
