/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. See film_hwcolour.h. ROM: PFMyColor.c 0xff914f30 copies
 * the selected preset (found through the pointer table at 0x5b4c, entry 0 =
 * My Colors Off, initially the identity at 0xffbcb7c0) to 0xa2e88; then
 * ManageImgTbl.c 0xff940188 copies it to 0xa9864 and loads the hardware. Both
 * the live view path and each shot's develop setup (0xff914710) take it. */
#include "film_hwcolour.h"
#ifndef FILM_HW_PRESETS
#define FILM_HW_PRESETS ((const unsigned *volatile *)0x5b4c)
#define FILM_HW_CANON_OFF ((const unsigned *)0xffbcb7c0)
#define FILM_HW_LOADED ((const volatile unsigned *)0xa9864)
#endif
/* PrcssFil.c's colour reload (0xff855274; its only caller is 0xff885e7c):
 * under PrcssFil's semaphore it copies "My Colors Off" through the pointer
 * table (0xff915148) and uploads it to the colour engine (0xff9401ac:
 * Canon's own BltDmac channel 1 transfer, as at start-up and on a My Colors
 * change); it returns at once while PrcssFil is stopped. Tried over USB: the live
 * view took the new table each time. */
#ifndef FILM_HW_RELOAD
#define FILM_HW_RELOAD ((const unsigned *)0xff855274)
#define FILM_HW_RELOAD_CALL() ((void (*)(void))0xff855274)()
#endif
#ifndef FILM_HW_SHARPEN_WORD
#define FILM_HW_SHARPEN_WORD ((volatile int *)0x5b40)        /* override block 0x5b38, word 2 */
#define FILM_HW_FCREATE ((const unsigned *)0xff914864)
#define FILM_HW_FCREATE_POOL ((const unsigned *)0xff914290)
#define FILM_HW_ANGEL ((const unsigned short *const volatile *)0x674c)  /* ManageImgTbl [0x6748+4] */
#endif
static const unsigned *installed;
int film_hw_table_valid(const unsigned *table) {
    unsigned i;
    if(!table)return 0;
    for(i=0;i<FILM_HW_TABLE_WORDS;i++)if(table[i]>>30)return 0;
    return 1;
}
static int canon_identity(void) {
    const unsigned *t=FILM_HW_CANON_OFF;                     /* greys: Y = 0, 64, ..., 576 */
    return t[0]==0&&t[111]==(64u<<20)&&t[888]==(512u<<20)&&t[999]==(576u<<20);
}
int film_hw_install(const unsigned *table) {
    const unsigned *now=FILM_HW_PRESETS[0];
    if(!film_hw_table_valid(table)||!canon_identity()||(now!=FILM_HW_CANON_OFF&&now!=installed))return -1;
    FILM_HW_PRESETS[0]=table;installed=table;
    return 0;
}
void film_hw_restore(void) {
    if(installed&&FILM_HW_PRESETS[0]==installed)FILM_HW_PRESETS[0]=FILM_HW_CANON_OFF;
    installed=0;
}
/* push {r4,lr}; ldr r4,=0x22a8 (PrcssFil state); bl 0xff915148; bl 0xff9401ac. */
static int reload_is_canons(void) {
    const unsigned *c=FILM_HW_RELOAD;
    return c[0]==0xe92d4010u&&c[1]==0xe51f4bbcu&&c[14]==0xeb02ffa5u&&c[15]==0xeb03abbdu;
}
int film_hw_reload_preview(void) {
    if(!reload_is_canons())return -1;
    FILM_HW_RELOAD_CALL();
    return 0;
}
int film_hw_used(const unsigned *table) {
    unsigned i;
    if(!table||table!=installed||FILM_HW_PRESETS[0]!=table)return 0;
    for(i=0;i<FILM_HW_TABLE_WORDS;i++)if(FILM_HW_LOADED[i]!=table[i])return 0;
    return 1;
}

/* FCreate.c 0xff914864: ldr r0,[r7,#8]; cmp r0,#0; strge r0,[r4,#0x2b4],
 * with r7 = 0x5b38 from the literal pool. */
static int fcreate_reads_override(void) {
    const unsigned *c=FILM_HW_FCREATE;
    return *FILM_HW_FCREATE_POOL==0x5b38u&&c[0]==0xe5970008u&&c[1]==0xe3500000u&&c[2]==0xa58402b4u;
}
static int sharpen_installed=-1;
int film_hw_sharpen(int level) {
    int now;
    if(level<0){film_hw_sharpen_restore();return 0;}
    if(level>FILM_HW_SHARPEN_MAX||!fcreate_reads_override())return -1;
    now=*FILM_HW_SHARPEN_WORD;
    if(now>=0&&now!=sharpen_installed)return -1;
    *FILM_HW_SHARPEN_WORD=level;sharpen_installed=level;
    return 0;
}
void film_hw_sharpen_restore(void) {
    if(sharpen_installed>=0&&*FILM_HW_SHARPEN_WORD==sharpen_installed)*FILM_HW_SHARPEN_WORD=-1;
    sharpen_installed=-1;
}
int film_hw_sharpen_loaded(void) {
    const unsigned short *t=*FILM_HW_ANGEL;
    if(!t||((unsigned long)t&3))return -1;
    return t[183];
}
