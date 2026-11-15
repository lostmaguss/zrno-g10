/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Soft focus: argument and workspace guards, Off unchanged, a flat frame
 * unchanged within rounding, light spilling from a bright square into the
 * dark around it (more at each level, nothing far away), the square's inside
 * softened at most 20 levels, lifted pixels losing colour, sanitizer bounds. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../camera/film_bloom.h"
static int sb(unsigned char b){return b<128?b:(int)b-256;}
int main(void) {
 const unsigned W=1024,H=768,S=W*2;const size_t N=(size_t)S*H,n=film_bloom_work_size(W);
 unsigned char *img=malloc(N),*ref=malloc(N);void *work=malloc(n);
 /* Guards. */
 memset(img,100,N);
 assert(film_uyvy_soften(img,N,W,H,S,4,work,n,0)==-1);
 assert(film_uyvy_soften(img,N,W,H,S,1,work,n-1,0)==-1);
 assert(film_uyvy_soften(img,N,W,H,S,1,img,n,0)==-1);           /* workspace inside the image */
 assert(film_uyvy_soften(0,N,W,H,S,1,work,n,0)==-1);
 memcpy(ref,img,N);assert(film_uyvy_soften(img,N,W,H,S,0,0,0,0)==0&&!memcmp(img,ref,N));
 /* A flat frame stays flat within rounding at every level. */
 for(unsigned level=1;level<=3;level++) {
  for(size_t i=0;i<N;i+=4){img[i]=(unsigned char)-12;img[i+1]=img[i+3]=120;img[i+2]=9;}
  assert(film_uyvy_soften(img,N,W,H,S,level,work,n,0)==0);
  for(size_t i=0;i<N;i+=2){int d=(int)img[i+1]-120;assert(d>=-1&&d<=1);}
 }
 /* A bright square on dark: light spills outward, more at each level. */
 int prev_near=0;
 for(unsigned level=1;level<=3;level++) {
  for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++) {
   unsigned char *p=img+(size_t)y*S+x*2;int in=x>=448&&x<576&&y>=320&&y<448;
   p[1]=in?240:20;if(!(x&1)){p[0]=(unsigned char)-30;p[2]=40;}
  }
  memcpy(ref,img,N);
  assert(film_uyvy_soften(img,N,W,H,S,level,work,n,0)==0);
  int near=img[(size_t)384*S+(576+8)*2+1]-20;                    /* 8 px right of the square */
  int far=img[(size_t)40*S+40*2+1]-20;                            /* the far corner */
  int centre=240-img[(size_t)384*S+512*2+1];                      /* centre of the square */
  assert(near>prev_near&&near>4&&far==0&&centre>=0&&centre<=20);prev_near=near;   /* High reaches into a 128 px square */
  for(size_t i=0;i<N;i+=4) {                                      /* lifted pairs never gain colour */
   if(img[i+1]>ref[i+1]||img[i+3]>ref[i+3]) {
    int u0=sb(ref[i]),u1=sb(img[i]),v0=sb(ref[i+2]),v1=sb(img[i+2]);
    assert(abs(u1)<=abs(u0)&&abs(v1)<=abs(v0)&&(u1==0||(u1<0)==(u0<0))&&(v1==0||(v1<0)==(v0<0)));
   }
  }
 }
 /* Odd geometry and a padded stride under the sanitizers. */
 { unsigned w=642,h=37,stride=w*2+12;size_t m=(size_t)stride*h,k=film_bloom_work_size(w);
   unsigned char *a=malloc(m);void *wk=malloc(k);
   for(size_t i=0;i<m;i++)a[i]=(unsigned char)(i*2654435761u>>13);
   for(unsigned level=1;level<=3;level++)assert(film_uyvy_soften(a,m,w,h,stride,level,wk,k,0)==0);
   free(a);free(wk); }
 free(img);free(ref);free(work);
 puts("Soft focus: guards, Off unchanged, flat frame within 1 level, outward spill growing with level and none far away, square centre softened at most 20 levels, lifted pixels never gain colour, odd geometry and padded stride under sanitizers passed");
 return 0;
}
