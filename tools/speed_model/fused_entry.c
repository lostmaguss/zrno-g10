/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Emulator entry for the fused Bloom + grain pass, as film_stage.c calls it. */
#include "film_bloom.c"
#include "film_grain.c"
int bench_fused(unsigned char *image,unsigned size,unsigned width,unsigned height,
 unsigned level,void *work,unsigned char *scratch,unsigned grain_level) {
 film_grain_rows grain;
 static unsigned char grain_work[4096];
 if(film_grain_begin_rows(&grain,width,height,grain_level,0x1234567u,grain_work,sizeof grain_work))return -1;
 film_bloom_row_pass pass={scratch,16384,film_grain_finish_row,&grain};
 return film_uyvy_bloom_rows(image,size,width,height,width*2,level,work,film_bloom_work_size(width),&pass,0);
}
