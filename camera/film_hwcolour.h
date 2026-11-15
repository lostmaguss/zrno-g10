/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Film looks through the DIGIC 4 hardware colour table
 * (G12 1.00G): the 9x9x9 RGB -> YCbCr table with tetrahedral interpolation
 * that My Colors uses. A look's table (.HWT, made by tools/hw_table.py) is
 * 1000 words, index r + 10g + 100b, Y in bits 20-29 and signed U/V in bits
 * 10-19 and 0-9 on a 9-bit scale. Canon finds the "My Colors Off" preset
 * through a pointer in RAM; pointing it at our table makes Canon's develop
 * apply the look itself. */
#ifndef FILM_HWCOLOUR_H
#define FILM_HWCOLOUR_H
#define FILM_HW_TABLE_WORDS 1000
#define FILM_HW_TABLE_BYTES 4000
/* 1 if the 1000 words look like a table (the top two bits of each clear). */
int film_hw_table_valid(const unsigned *table);
/* Points "My Colors Off" at table (kept by the caller until restored or
 * replaced). Refuses (-1) unless the ROM identity table is where 1.00G keeps
 * it and the pointer holds Canon's table or one we installed. */
int film_hw_install(const unsigned *table);
/* Canon's own "My Colors Off" table again. */
void film_hw_restore(void);
/* Makes the live view take the "My Colors Off" table now: Canon copies it
 * only when My Colors is set, so a new look reached the next shot but not the
 * preview. Only with My Colors Off (the reload always loads Off) and in
 * shooting mode. Refuses (-1) unless the ROM routine is 1.00G's. */
int film_hw_reload_preview(void);
/* The table Canon loaded last (for the shot being developed, when called from
 * the develop task) is exactly this one. */
int film_hw_used(const unsigned *table);

/* Canon's edge enhancement (sharpening) in the same develop. FCreate.c
 * (0xff914864) copies a debug override word, 0x5b40, into the develop's
 * sharpening gain when it is not negative; ManageImgTbl then loads it as
 * entry 183 of the 504-entry table at 0xC0F21000 (the word Magic Lantern
 * knows as 0xC0F2116C on the DSLRs). Canon's own gain is 64 at My Colors
 * Off, 24 per Custom sharpness step; 0 turns sharpening off, 255 is about
 * twice the default. Negative gains do not blur. */
#define FILM_HW_SHARPEN_MAX 255
/* Sets the gain for every later develop (0..255), or Canon's own for a
 * negative level. Refuses (-1) unless FCreate is 1.00G's and the override
 * is unset or ours. */
int film_hw_sharpen(int level);
/* Canon's own gain again (the override unset, if it is still ours). */
void film_hw_sharpen_restore(void);
/* The gain in the table Canon loaded last, or -1 if it cannot be read. */
int film_hw_sharpen_loaded(void);
#endif
