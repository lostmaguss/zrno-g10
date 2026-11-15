// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef FILM_RELOC_H
#define FILM_RELOC_H

/* Zrno's copy of Canon's develop task, built on the camera from its own ROM
 * (see film_reloc.c). */

/* Room for the copy, in words. */
#define FILM_RELOC_WORDS 1024

/* Results of film_reloc_build below zero. */
#define FILM_RELOC_BLX      -1   /* a branch that switches to Thumb */
#define FILM_RELOC_ADR      -2   /* an address taken inside the copied code */
#define FILM_RELOC_FULL     -3   /* too many veneers or literals */
#define FILM_RELOC_REACH    -4   /* a literal beyond an LDR's reach */
#define FILM_RELOC_ROM      -5   /* the ROM differs from G12 1.00G */

typedef unsigned (*film_rom_word)(unsigned addr);

/* Builds the copy into out (FILM_RELOC_WORDS words), for code that will run
 * at out_addr. trace_fn and observe_fn are the hooks' targets (bit 0 set for
 * Thumb). Returns the number of words, or a FILM_RELOC_ error. hash gets
 * the hash of every ROM word read; map, if not 0, gets each copied
 * instruction's position in words (task first, then the step loop), counting
 * from the hook placed before it. The task starts at word 0. */
int film_reloc_build(film_rom_word rom, unsigned *out, unsigned out_addr,
                     unsigned trace_fn, unsigned observe_fn,
                     unsigned *hash, unsigned short *map);

/* Hash of a G12 1.00G ROM's develop task, as film_reloc_build reports it. */
#define FILM_RELOC_HASH 0xd12f82deu

#ifndef FILM_RELOC_HOST
/* Camera: the copy's entry point, built on the first call; 0 if the ROM
 * differs, and the camera then runs Canon's own task. */
void *film_develop_relocate(void);
/* 1 once the copy is running, a FILM_RELOC_ error, or 0 before the task
 * starts. */
extern int film_reloc_status;
#endif

#endif
