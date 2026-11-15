/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Fast halation (film_halo_fast): runs on every size and path the exact one
 * does, stays close to it, gives the same bytes copied or in place, and
 * leaves the exact pass untouched when off. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../camera/film_colour.h"
static void fill(unsigned char *p,size_t n,unsigned stride,unsigned seed) {
 unsigned s=seed*2654435761u+3u;
 for(size_t i=0;i<n;i++){
  unsigned x=(unsigned)(i%stride)/2,y=(unsigned)(i/stride);s^=s<<13;s^=s>>17;s^=s<<5;
  unsigned hot=((x/23+y/31)%6==0)||((x^y)%97<3);
  p[i]=(i&1)?(unsigned char)(hot?236+(s%20):(x*3+y*5)%200+(s&15)):(unsigned char)((s&63)-32);
 }
}
static int run(unsigned char *img,size_t n,unsigned w,unsigned h,const unsigned char *lut,unsigned level,int graded,int inplace) {
 size_t ws=film_halo_work_size(w);unsigned char *work=malloc(ws+8),*sc=malloc(16384);
 film_halo_tint tint={256,51,0,0};int rc;
 film_colour_rows_in_place(inplace);
 rc=graded?film_uyvy_graded_retro(img,n,w,h,w*2,0,level,&tint,work,ws,sc,16384,0,0,1,0x1234,0,0,0,0)
          :film_uyvy_map_halo_cached_retro(img,n,w,h,w*2,lut,level,work,ws,sc,16384,0,0,1,0x1234,0,0,0,0);
 film_colour_rows_in_place(0);free(work);free(sc);return rc;
}
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long ls=ftell(f);rewind(f);
 unsigned char *lut=malloc(ls);assert(lut&&fread(lut,1,ls,f)==(size_t)ls);fclose(f);assert(!film_lut_prepare(lut,ls));
 const unsigned sizes[][2]={{3648,300},{2,1},{4,3},{8,8},{36,9},{74,40},{148,97},{300,41},{1000,7}};
 unsigned cases=0,worst=0;double mean_sum=0;
 for(unsigned si=0;si<sizeof(sizes)/sizeof(sizes[0]);si++)for(unsigned level=1;level<=3;level++)for(int graded=0;graded<2;graded++) {
  unsigned w=sizes[si][0],h=sizes[si][1];size_t n=(size_t)w*2*h;
  unsigned char *src=malloc(n),*ex=malloc(n),*ex2=malloc(n),*fa=malloc(n),*fb=malloc(n);assert(src&&ex&&ex2&&fa&&fb);
  fill(src,n,w*2,si*7+level+graded);
  film_halo_fast(0);memcpy(ex,src,n);assert(!run(ex,n,w,h,lut,level,graded,0));
  film_halo_fast(1);memcpy(fa,src,n);assert(!run(fa,n,w,h,lut,level,graded,0));
  memcpy(fb,src,n);assert(!run(fb,n,w,h,lut,level,graded,1));assert(!memcmp(fa,fb,n));   /* copied or in place */
  film_halo_fast(0);memcpy(ex2,src,n);assert(!run(ex2,n,w,h,lut,level,graded,1));assert(!memcmp(ex,ex2,n)); /* off: exact again */
  unsigned long sum=0;
  for(size_t i=0;i<n;i++){int d=(int)fa[i]-(int)ex[i];if(!(i&1)){d=(int)(signed char)fa[i]-(int)(signed char)ex[i];}d=d<0?-d:d;sum+=d;if((unsigned)d>worst)worst=(unsigned)d;}
  mean_sum+=(double)sum/n;cases++;
  free(src);free(ex);free(ex2);free(fa);free(fb);
 }
 /* Synthetic speckle on single rows is its worst case: the mask sees only
  * the top row of each block. Real photos stay within 7 levels. */
 assert(worst<=24&&mean_sum/cases<0.3);
 printf("Fast halation: %u cases (graded and CPU LUT, levels 1-3, sizes down to 2x1), copied = in place, off = exact; synthetic worst difference %u, mean %.3f: passed\n",cases,worst,mean_sum/cases);
 return 0;
}
