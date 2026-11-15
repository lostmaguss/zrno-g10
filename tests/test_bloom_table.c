/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Bloom and soft focus give the same bytes with the chroma fade table as
 * without: all levels, odd sizes and strides, a fused row pass, and frames
 * from flat to saturated chroma. Removing the table restores the arithmetic. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../camera/film_bloom.h"
static void fill(unsigned char *p,size_t n,unsigned stride,unsigned seed) {
 unsigned s=seed*2654435761u+7u;
 for(size_t i=0;i<n;i++){
  unsigned x=(unsigned)(i%stride)/2,y=(unsigned)(i/stride);s^=s<<13;s^=s>>17;s^=s<<5;
  unsigned bright=((x/29+y/17)%4==0)||(y%53<11);
  p[i]=(i&1)?(unsigned char)(bright?170+(s%86):(x*5+y*3)%200+(s&31)):(unsigned char)(seed%3==0?(s&255):(s&63)-32);
 }
}
static void finish(unsigned char *row,void *ctx){(void)row;(void)ctx;}
int main(void) {
 const unsigned sizes[][2]={{3648,240},{2,1},{6,5},{14,10},{130,67},{402,301},{1000,7}};
 unsigned char *table=malloc(film_bloom_fade_table_size());assert(table&&film_bloom_fade_table_size()==65536);
 unsigned cases=0;
 for(unsigned si=0;si<sizeof(sizes)/sizeof(sizes[0]);si++)for(unsigned level=1;level<=3;level++)for(int kind=0;kind<3;kind++) {
  unsigned w=sizes[si][0],h=sizes[si][1],stride=w*2+(si&1?6:0);
  size_t n=(size_t)stride*h,bw=film_bloom_work_size(w);
  unsigned char *a=malloc(n),*b=malloc(n),*work=malloc(bw),*sa=malloc(w*2),*sb=malloc(w*2);assert(a&&b&&work&&sa&&sb);
  fill(a,n,stride,si*11+level*3+kind);memcpy(b,a,n);
  film_bloom_row_pass pa={sa,w*2,finish,0},pb={sb,w*2,finish,0};
  film_bloom_fade_table(0);
  int ra=kind==0?film_uyvy_bloom(a,n,w,h,stride,level,work,bw,0):kind==1?film_uyvy_bloom_rows(a,n,w,h,stride,level,work,bw,&pa,0):film_uyvy_soften(a,n,w,h,stride,level,work,bw,0);
  film_bloom_fade_table(table);
  int rb=kind==0?film_uyvy_bloom(b,n,w,h,stride,level,work,bw,0):kind==1?film_uyvy_bloom_rows(b,n,w,h,stride,level,work,bw,&pb,0):film_uyvy_soften(b,n,w,h,stride,level,work,bw,0);
  assert(ra==0&&rb==0&&!memcmp(a,b,n));cases++;
  free(a);free(b);free(work);free(sa);free(sb);
 }
 /* Fast Bloom: the same bytes with or without the table and on aligned or
  * unaligned rows; close to Bloom; soft focus ignores it; off is exact. */
 unsigned fast_cases=0,worst_y=0,worst_c=0;
 for(unsigned si=0;si<sizeof(sizes)/sizeof(sizes[0]);si++)for(unsigned level=1;level<=3;level++) {
  unsigned w=sizes[si][0],h=sizes[si][1],stride=w*2;
  size_t n=(size_t)stride*h,bw=film_bloom_work_size(w);
  unsigned char *src=malloc(n+4),*ex=malloc(n),*f1=malloc(n),*f2=malloc(n),*f3=malloc(n+4),*work=malloc(bw);assert(src&&ex&&f1&&f2&&f3&&work);
  fill(src,n,stride,si*5+level);
  memcpy(ex,src,n);film_bloom_fade_table(table);film_bloom_fast(0);assert(!film_uyvy_bloom(ex,n,w,h,stride,level,work,bw,0));
  film_bloom_fast(1);
  memcpy(f1,src,n);assert(!film_uyvy_bloom(f1,n,w,h,stride,level,work,bw,0));                  /* table, aligned: word path */
  memcpy(f3+2,src,n);assert(!film_uyvy_bloom(f3+2,n,w,h,stride,level,work,bw,0));              /* table, unaligned rows */
  film_bloom_fade_table(0);memcpy(f2,src,n);assert(!film_uyvy_bloom(f2,n,w,h,stride,level,work,bw,0));   /* no table */
  assert(!memcmp(f1,f2,n)&&!memcmp(f1,f3+2,n));
  for(size_t i=0;i<n;i++){
   int d=(int)f1[i]-(int)ex[i];
   if(i&1){d=d<0?-d:d;if((unsigned)d>worst_y)worst_y=(unsigned)d;}
   else{int a=(signed char)f1[i],b2=(signed char)ex[i];d=a-b2;d=d<0?-d:d;if((unsigned)d>worst_c)worst_c=(unsigned)d;}
  }
  /* soft focus is unchanged by the fast flag */
  memcpy(f1,src,n);film_bloom_fast(0);assert(!film_uyvy_soften(f1,n,w,h,stride,level,work,bw,0));
  memcpy(f2,src,n);film_bloom_fast(1);assert(!film_uyvy_soften(f2,n,w,h,stride,level,work,bw,0));assert(!memcmp(f1,f2,n));
  film_bloom_fast(0);fast_cases++;
  free(src);free(ex);free(f1);free(f2);free(f3);free(work);
 }
 /* Synthetic stripes and speckle are its worst case (the mask skips rows 1
  * and 3); real photos stay within 1-2 levels. */
 assert(worst_y<=6&&worst_c<=6);
 printf("Fast Bloom: %u cases, same bytes with or without the table and on unaligned rows, soft focus unchanged; worst difference from Bloom on synthetic frames: luma %u, chroma %u\n",fast_cases,worst_y,worst_c);
 film_bloom_fade_table(table);
 /* The table is faded() itself: spot-check the sign handling at the edges. */
 assert(table[0]==0&&table[127]==127&&table[128]==128&&table[255*256+127]==0&&table[255*256+128]==0&&table[255*256+129]==0);
 film_bloom_fade_table(0);free(table);
 printf("Bloom fade table: Bloom, fused Bloom rows and soft focus give the same bytes with the table in %u cases (all levels, odd sizes and strides, flat to saturated chroma): passed\n",cases);
 return 0;
}
