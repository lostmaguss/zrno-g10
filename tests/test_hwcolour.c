/* SPDX-License-Identifier: GPL-2.0-or-later */
/* film_hwcolour with Canon's RAM and ROM mocked. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned canon_off[1000],loaded[1000];
static const unsigned *presets[14];
#define FILM_HW_PRESETS ((const unsigned *volatile *)presets)
#define FILM_HW_CANON_OFF ((const unsigned *)canon_off)
#define FILM_HW_LOADED ((const volatile unsigned *)loaded)
static unsigned fcreate_code[3]={0xe5970008u,0xe3500000u,0xa58402b4u},fcreate_pool=0x5b38u;
static int canon_sharpen=-1;
static unsigned short canon_angel[504] __attribute__((aligned(4)));
static const unsigned short *canon_angel_ptr=canon_angel;
#define FILM_HW_SHARPEN_WORD ((volatile int *)&canon_sharpen)
#define FILM_HW_FCREATE ((const unsigned *)fcreate_code)
#define FILM_HW_FCREATE_POOL ((const unsigned *)&fcreate_pool)
#define FILM_HW_ANGEL ((const unsigned short *const volatile *)&canon_angel_ptr)
static unsigned reload_code[16]={0xe92d4010u,0xe51f4bbcu,0,0,0,0,0,0,0,0,0,0,0,0,0xeb02ffa5u,0xeb03abbdu};
static int reloads;
#define FILM_HW_RELOAD ((const unsigned *)reload_code)
#define FILM_HW_RELOAD_CALL() (reloads++)
#include "../camera/film_hwcolour.c"
int main(void) {
    static unsigned a[1000],b[1000],bad[1000];
    unsigned k;
    /* Preview reload: Canon's routine is called only when its code is 1.00G's. */
    assert(!film_hw_reload_preview()&&reloads==1);
    reload_code[15]=0;assert(film_hw_reload_preview()==-1&&reloads==1);reload_code[15]=0xeb03abbdu;
    reload_code[1]^=1;assert(film_hw_reload_preview()==-1&&reloads==1);reload_code[1]^=1;
    reloads=0;
    for(k=0;k<10;k++)canon_off[111*k]=(64u*k)<<20;
    for(k=0;k<1000;k++){a[k]=(k*7u)&0x3fffffffu;b[k]=a[k]^1u;bad[k]=a[k];}
    bad[500]|=1u<<31;
    presets[0]=canon_off;
    assert(film_hw_table_valid(a)&&!film_hw_table_valid(bad)&&!film_hw_table_valid(0));
    /* Install, swap to another look, restore. */
    assert(!film_hw_install(a)&&presets[0]==a);
    assert(!film_hw_used(a));                                  /* Canon has not loaded it yet */
    memcpy(loaded,a,sizeof(loaded));assert(film_hw_used(a)&&!film_hw_used(b));
    assert(!film_hw_install(b)&&presets[0]==b&&!film_hw_used(b));
    memcpy(loaded,b,sizeof(loaded));assert(film_hw_used(b));
    loaded[999]^=1;assert(!film_hw_used(b));                   /* My Colors not Off: another table */
    film_hw_restore();assert(presets[0]==canon_off&&!film_hw_used(b));
    film_hw_restore();assert(presets[0]==canon_off);
    /* Refusals leave everything as it is. */
    assert(film_hw_install(bad)==-1&&presets[0]==canon_off);
    presets[0]=b;assert(film_hw_install(a)==-1&&presets[0]==b);   /* someone else's pointer */
    presets[0]=canon_off;canon_off[888]=0;assert(film_hw_install(a)==-1&&presets[0]==canon_off);  /* not 1.00G's ROM */
    canon_off[888]=512u<<20;canon_off[999]=576u<<20;canon_off[888]=512u<<20;
    for(k=0;k<10;k++)canon_off[111*k]=(64u*k)<<20;
    assert(!film_hw_install(a)&&presets[0]==a);
    /* Restore only takes back our own pointer. */
    presets[0]=b;film_hw_restore();assert(presets[0]==b);
    /* Sharpening override: set, change, restore; refusals. */
    assert(film_hw_sharpen_loaded()==0);canon_angel[183]=64;assert(film_hw_sharpen_loaded()==64);
    assert(!film_hw_sharpen(0)&&canon_sharpen==0);
    assert(!film_hw_sharpen(200)&&canon_sharpen==200);
    assert(film_hw_sharpen(256)==-1&&canon_sharpen==200);
    assert(!film_hw_sharpen(-1)&&canon_sharpen==-1);           /* negative: Canon's own */
    film_hw_sharpen_restore();assert(canon_sharpen==-1);
    canon_sharpen=30;assert(film_hw_sharpen(40)==-1&&canon_sharpen==30);  /* someone else's override */
    film_hw_sharpen_restore();assert(canon_sharpen==30);
    canon_sharpen=-1;fcreate_code[2]^=1;assert(film_hw_sharpen(40)==-1&&canon_sharpen==-1);  /* not 1.00G's FCreate */
    fcreate_code[2]^=1;fcreate_pool=0x5b3cu;assert(film_hw_sharpen(40)==-1);fcreate_pool=0x5b38u;
    assert(!film_hw_sharpen(40)&&canon_sharpen==40);
    canon_sharpen=99;film_hw_sharpen_restore();assert(canon_sharpen==99);  /* changed behind us: left alone */
    canon_angel_ptr=0;assert(film_hw_sharpen_loaded()==-1);canon_angel_ptr=canon_angel;
    puts("Hardware colour table: install/swap/restore, used only when Canon loaded exactly ours, refusals on bad tables, foreign pointers and an unexpected ROM; sharpening override set/restore with the same refusals");
    return 0;
}
