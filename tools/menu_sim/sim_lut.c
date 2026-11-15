/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Mac menu simulator, Zrno side: the camera's film_lut.c (the menu's
 * values, settings files and LUT loading) on the host stubs the tests use,
 * against a copy of the card's A/CHDK/FILM in the working directory. */
#define camera_info sim_lut_camera_info
#include "camera_info.h"
#include "film_clock.h"
/* Canon's colour table pointers, in host memory (the Colour engine item). */
static unsigned canon_off[1000],canon_loaded[1000];
static const unsigned *canon_presets[14];
#define FILM_HW_PRESETS ((const unsigned *volatile *)canon_presets)
#define FILM_HW_CANON_OFF ((const unsigned *)canon_off)
#define FILM_HW_LOADED ((const volatile unsigned *)canon_loaded)
static unsigned fcreate_code[3]={0xe5970008u,0xe3500000u,0xa58402b4u},fcreate_pool=0x5b38u;
static int canon_sharpen=-1;
static unsigned short canon_angel[504] __attribute__((aligned(4)));
static const unsigned short *canon_angel_ptr=canon_angel;
#define FILM_HW_SHARPEN_WORD ((volatile int *)&canon_sharpen)
#define FILM_HW_FCREATE ((const unsigned *)fcreate_code)
#define FILM_HW_FCREATE_POOL ((const unsigned *)&fcreate_pool)
#define FILM_HW_ANGEL ((const unsigned short *const volatile *)&canon_angel_ptr)
static unsigned reload_code[16];
#define FILM_HW_RELOAD ((const unsigned *)reload_code)
#define FILM_HW_RELOAD_CALL() ((void)0)
int film_preview_reloadable(void){return 1;}   /* shooting mode, My Colors Off */
#include "../../camera/film_hwcolour.c"
#include "../../camera/film_lut.c"
#undef camera_info
int shooting_in_progress(void){return 0;}
/* Start up as the camera does, and let SpyTask's poll load the LUT. */
void film_sim_lut_init(void){for(int k=0;k<10;k++)canon_off[111*k]=(64u*k)<<20;canon_presets[0]=canon_off;film_lut_init();for(int i=0;i<4;i++)film_lut_poll(0);}
/* Poll once per GUI step, as SpyTask would between key presses. */
void film_gbui_poll(void);
void film_sim_lut_poll(void){film_lut_poll(0);film_gbui_poll();}
