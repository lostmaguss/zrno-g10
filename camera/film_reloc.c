// SPDX-License-Identifier: GPL-2.0-or-later
/* Zrno's copy of Canon's develop task (DevelopModule, 0xff8e6f3c) and its
 * encode-step loop (0xff8e5ec4), built in RAM when the task starts, from the
 * camera's own ROM. No Canon code is in Zrno's source or binary: the camera
 * copies its own instructions here, and Zrno's hooks go in between:
 *
 *   0xff8e6f6c  film_develop_trace(msg), right after each message arrives
 *   0xff8e7324  film_stage_observe(1), before the encode-step loop
 *   0xff8e7328  film_stage_observe(2), after it (the JPEG is encoded by then)
 *   0xff8e6050  film_stage_observe(3), in the loop, before each encode step
 *
 * Every instruction is copied as it is, except those that depend on where
 * they run:
 *   - a branch inside the copied code goes to the copy; any other branch
 *     goes through a veneer (ldr pc,[pc,#-4] and the target), because the
 *     ROM is out of a branch's reach from RAM;
 *   - an LDR from the ROM's literal pool, or an ADR, loads the same value
 *     from the copy's own literal pool.
 * The task's table jump (0xff8e6fb8) needs nothing: its table is copied in
 * order, with no hook inside. The ROM words read are hashed, and a ROM
 * other than G12 1.00G is refused. tools/check_reloc.py checks the copy
 * against a ROM dump, instruction by instruction. */
#include "film_reloc.h"

#define TASK_START  0xff8e6f3cu
#define TASK_END    0xff8e73ccu
#define STEPS_START 0xff8e5ec4u
#define STEPS_END   0xff8e60d4u
#define N_TASK  ((TASK_END - TASK_START) >> 2)
#define N_STEPS ((STEPS_END - STEPS_START) >> 2)
#define N_INS   (N_TASK + N_STEPS)

#define HOOK_WORDS 11
#define MAX_VENEERS 64
#define MAX_LITERALS 64

static const unsigned hook_addr[4] = { 0xff8e6f6cu, 0xff8e7324u, 0xff8e7328u, 0xff8e6050u };

static int index_of(unsigned a)
{
    if (a >= TASK_START && a < TASK_END) return (int)((a - TASK_START) >> 2);
    if (a >= STEPS_START && a < STEPS_END) return (int)(N_TASK + ((a - STEPS_START) >> 2));
    return -1;
}

static unsigned addr_of(int i)
{
    return i < (int)N_TASK ? TASK_START + ((unsigned)i << 2)
                           : STEPS_START + ((unsigned)(i - (int)N_TASK) << 2);
}

static int hook_of(unsigned a)
{
    int h;
    for (h = 0; h < 4; h++)
        if (hook_addr[h] == a) return h;
    return -1;
}

static unsigned fnv(unsigned h, unsigned w)
{
    return (h ^ w) * 16777619u;
}

/* Finds or adds v in list; returns its index, or -1 when full. */
static int intern(unsigned *list, int *n, int max, unsigned v)
{
    int i;
    for (i = 0; i < *n; i++)
        if (list[i] == v) return i;
    if (*n == max) return -1;
    list[*n] = v;
    return (*n)++;
}

int film_reloc_build(film_rom_word rom, unsigned *out, unsigned out_addr,
                     unsigned trace_fn, unsigned observe_fn,
                     unsigned *hash, unsigned short *map)
{
    static unsigned short label[N_INS];        /* position of the hook, or of the instruction */
    unsigned veneer[MAX_VENEERS], literal[MAX_LITERALS];
    /* fixups: (position << 8) | (kind << 7) | index; kind 0 veneer, 1 literal */
    static unsigned fix[N_INS + 8];
    int nveneer = 0, nlit = 0, nfix = 0, pos = 0, i, code_end, pool;
    unsigned h = 2166136261u;
    (void)out_addr;

    for (i = 0; i < (int)N_INS; i++) {
        label[i] = (unsigned short)pos;
        pos += 1 + (hook_of(addr_of(i)) >= 0 ? HOOK_WORDS : 0);
    }
    code_end = pos;

    pos = 0;
    for (i = 0; i < (int)N_INS; i++) {
        unsigned a = addr_of(i), w = rom(a), cond = w >> 28;
        int hk = hook_of(a), k;
        h = fnv(h, w);
        if (hk >= 0) {
            out[pos++] = 0xe92d5fffu;                    /* stmdb sp!,{r0-r12,lr} */
            out[pos++] = 0xe10f0000u;                    /* mrs r0,cpsr */
            out[pos++] = 0xe24dd008u;                    /* sub sp,sp,#8 */
            out[pos++] = 0xe58d0000u;                    /* str r0,[sp] */
            /* trace: the received message, [sp,#0x1c] before the 64 bytes pushed */
            out[pos++] = hk == 0 ? 0xe59d005cu           /* ldr r0,[sp,#0x5c] */
                                 : 0xe3a00000u | (unsigned)hk;   /* mov r0,#phase */
            k = intern(literal, &nlit, MAX_LITERALS, hk == 0 ? trace_fn : observe_fn);
            if (k < 0) return FILM_RELOC_FULL;
            fix[nfix++] = ((unsigned)pos << 8) | 0x80u | (unsigned)k;
            out[pos++] = 0xe59fc000u;                    /* ldr ip,[pc,#literal] */
            out[pos++] = 0xe12fff3cu;                    /* blx ip */
            out[pos++] = 0xe59d0000u;                    /* ldr r0,[sp] */
            out[pos++] = 0xe28dd008u;                    /* add sp,sp,#8 */
            out[pos++] = 0xe128f000u;                    /* msr cpsr_f,r0 */
            out[pos++] = 0xe8bd5fffu;                    /* ldmia sp!,{r0-r12,lr} */
        }
        if ((w & 0x0e000000u) == 0x0a000000u) {          /* B, BL */
            int d = (int)(w << 8) >> 6;                  /* signed offset in bytes */
            unsigned t = a + 8u + (unsigned)d;
            int ti = index_of(t);
            if (cond == 15) return FILM_RELOC_BLX;
            if (ti >= 0) {
                out[pos] = (w & 0xff000000u) | ((unsigned)(label[ti] - pos - 2) & 0x00ffffffu);
            } else {
                k = intern(veneer, &nveneer, MAX_VENEERS, t);
                if (k < 0) return FILM_RELOC_FULL;
                fix[nfix++] = ((unsigned)pos << 8) | (unsigned)k;
                out[pos] = w & 0xff000000u;
            }
        } else if ((w & 0x0f7f0000u) == 0x051f0000u) {   /* LDR rd,[pc,#+/-imm12] */
            unsigned t = a + 8u, v;
            t = (w & (1u << 23)) ? t + (w & 4095u) : t - (w & 4095u);
            v = rom(t);
            h = fnv(h, v);
            k = intern(literal, &nlit, MAX_LITERALS, v);
            if (k < 0) return FILM_RELOC_FULL;
            fix[nfix++] = ((unsigned)pos << 8) | 0x80u | (unsigned)k;
            out[pos] = (w & 0xf000f000u) | 0x059f0000u;
        } else if ((w & 0x0e0f0000u) == 0x020f0000u &&
                   (((w >> 21) & 15) == 2 || ((w >> 21) & 15) == 4)) {   /* ADR: add/sub rd,pc,#imm */
            unsigned imm = w & 255, rot = ((w >> 8) & 15) * 2, t;
            if (rot) imm = (imm >> rot) | (imm << (32 - rot));
            t = ((w >> 21) & 15) == 4 ? a + 8u + imm : a + 8u - imm;
            if (index_of(t) >= 0) return FILM_RELOC_ADR;
            k = intern(literal, &nlit, MAX_LITERALS, t);
            if (k < 0) return FILM_RELOC_FULL;
            fix[nfix++] = ((unsigned)pos << 8) | 0x80u | (unsigned)k;
            out[pos] = (w & 0xf000f000u) | 0x059f0000u;
        } else {
            out[pos] = w;
        }
        pos++;
    }

    /* Veneers, then the literal pool. */
    for (i = 0; i < nveneer; i++) {
        out[code_end + 2 * i] = 0xe51ff004u;             /* ldr pc,[pc,#-4] */
        out[code_end + 2 * i + 1] = veneer[i];
    }
    pool = code_end + 2 * nveneer;
    if (pool + nlit > FILM_RELOC_WORDS) return FILM_RELOC_FULL;
    for (i = 0; i < nlit; i++)
        out[pool + i] = literal[i];
    for (i = 0; i < nfix; i++) {
        int at = (int)(fix[i] >> 8), idx = (int)(fix[i] & 0x7f);
        if (fix[i] & 0x80u) {
            int off = (pool + idx - at - 2) * 4;
            if (off < 0 || off > 4095) return FILM_RELOC_REACH;
            out[at] |= (unsigned)off;
        } else {
            out[at] |= (unsigned)(code_end + 2 * idx - at - 2) & 0x00ffffffu;
        }
    }
    if (map)
        for (i = 0; i < (int)N_INS; i++)
            map[i] = label[i];
    *hash = h;
    return pool + nlit;
}

#ifndef FILM_RELOC_HOST
#include "cache.h"
#include "film_stage.h"

int film_reloc_status;
static unsigned develop_copy[FILM_RELOC_WORDS] __attribute__((aligned(32)));

static unsigned rom_word(unsigned addr)
{
    return *(volatile const unsigned *)addr;
}

void *film_develop_relocate(void)
{
    unsigned h;
    int n;
    if (film_reloc_status)
        return film_reloc_status > 0 ? (void *)develop_copy : 0;
    n = film_reloc_build(rom_word, develop_copy, (unsigned)develop_copy,
                         (unsigned)film_develop_trace, (unsigned)film_stage_observe, &h, 0);
    if (n > 0 && h != FILM_RELOC_HASH)
        n = FILM_RELOC_ROM;
    if (n <= 0) {
        film_reloc_status = n;
        return 0;
    }
    /* Write the copy out of the data cache, and drop stale instructions. */
    dcache_clean_all();
    icache_flush_all();
    film_reloc_status = 1;
    return develop_copy;
}
#endif
