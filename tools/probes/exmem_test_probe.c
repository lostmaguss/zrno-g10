/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe (G12 1.00G): is the EXMEM block that
 * CHDK would load into with OPT_CHDK_IN_EXMEM left alone by Canon and by
 * Zrno? (The core has outgrown its 200 KB AgentRAM block.)
 *
 * With OPT_CHDK_IN_EXMEM, CHDK takes exmem pool 0 at boot, EXMEM_HEAP_SKIP +
 * EXMEM_BUFFER_SIZE bytes (0x2f7600 + 0x400000), and loads itself into the
 * first 4 MB; the top 0x2f7600 bytes are skipped because the movie buffers
 * live there. This module does the same allocation at run time, without
 * moving CHDK:
 *   first run:  allocates pool 0 the same size and fills it with a pattern
 *               that depends on each word's address;
 *   later runs: count the words that changed, separately in the CHDK part
 *               (first 4 MB) and in the skipped movie part, with the first
 *               and last changed address of each.
 * The pool stays allocated until the camera is switched off. Every run also
 * lists each exmem type's address and length. Log: A/CHDK/FILM/EXMTEST.TXT.
 * Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "module_def.h"
#include "clock.h"
#include "exmem.h"

#define POOL       0
#define CHDK_PART  0x400000u                  /* EXMEM_BUFFER_SIZE */
#define SKIP_PART  0x2f7600u                  /* EXMEM_HEAP_SKIP */
#define MAGIC      0xe7e57001u

static void lg(const char *s)
{
    int fd = open("A/CHDK/FILM/EXMTEST.TXT", O_WRONLY | O_CREAT | O_APPEND, 0777);
    if (fd >= 0) { write(fd, s, strlen(s)); close(fd); }
}

static unsigned pattern(unsigned a) { return (a ^ 0xc0ffee00u) * 2654435761u; }

static void types(void)
{
    char t[120];
    unsigned i;
    for (i = 0; i < exmem_type_count; i++) {
        exmem_alloc_info ai;
        if (get_exmem_type_status(i, &ai) && ai.len) {
            char *n = get_exmem_type_name(i);
            sprintf(t, "  type %u %s: %08x len %08x\n", i, n ? n : "?", (unsigned)ai.addr, ai.len);
            lg(t);
        }
    }
}

/* Words 0-1 hold MAGIC and the fill tick; the rest the pattern. */
static void check_part(const char *name, volatile unsigned *w, unsigned from, unsigned to)
{
    char t[160];
    unsigned i, n = 0, first = 0, last = 0, firstval = 0;
    for (i = from; i < to; i++) {
        unsigned want = pattern((unsigned)&w[i]);
        if (w[i] != want) {
            if (!n) { first = (unsigned)&w[i]; firstval = w[i]; }
            last = (unsigned)&w[i];
            n++;
        }
    }
    if (n) sprintf(t, "  %s: %u words changed, first %08x (now %08x), last %08x\n", name, n, first, firstval, last);
    else sprintf(t, "  %s: unchanged\n", name);
    lg(t);
}

int _run()
{
    char t[160];
    exmem_alloc_info ai;
    volatile unsigned *w;
    unsigned i, t0 = get_tick_count();
    sprintf(t, "\nexmtest tick=%u rec=%d play=%d\n", t0, camera_info.state.mode_rec, camera_info.state.mode_play);
    lg(t);
    types();
    if (!get_exmem_type_status(POOL, &ai)) { lg("  no pool 0\n"); return 0; }
    if (ai.len) {
        w = (volatile unsigned *)ai.addr;
        if (ai.len < CHDK_PART + SKIP_PART || w[0] != MAGIC) {
            sprintf(t, "  pool 0 is in use by someone else (%08x len %08x, word0 %08x): nothing done\n", (unsigned)ai.addr, ai.len, w[0]);
            lg(t);
            return 0;
        }
        sprintf(t, "  check: filled at tick %u, %u s ago\n", w[1], (t0 - w[1]) / 1000);
        lg(t);
        check_part("chdk 4 MB", w, 2, CHDK_PART / 4);
        check_part("movie skip", w, CHDK_PART / 4, (CHDK_PART + SKIP_PART) / 4);
    } else {
        w = (volatile unsigned *)exmem_alloc_uncached(POOL, CHDK_PART + SKIP_PART, &ai);
        if (!w) { lg("  allocation failed\n"); return 0; }
        for (i = 2; i < (CHDK_PART + SKIP_PART) / 4; i++) w[i] = pattern((unsigned)&w[i]);
        w[0] = MAGIC;
        w[1] = t0;
        sprintf(t, "  filled pool 0: %08x len %08x (CHDK would load at %08x) in %u ms\n",
                (unsigned)w, ai.len, (unsigned)w & 0x0fffffff, get_tick_count() - t0);
        lg(t);
    }
    sprintf(t, "  done in %u ms\n", get_tick_count() - t0);
    lg(t);
    return 0;
}

int _module_can_unload() { return 1; }        /* _run keeps no state: the pool holds it */
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libexmtest_sym;
libexmtest_sym _libexmtest = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"EXMEM test (fill / check)",
    MTYPE_TOOL,
    &_libexmtest.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
