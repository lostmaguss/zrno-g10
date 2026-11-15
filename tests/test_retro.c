/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Vignette and light leaks: effect properties, span equivalence, workspace
 * bounds under sanitizers, and colour-pass integration against a reference
 * composition (grade first, then the effect rows). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../camera/film_retro.h"
#include "../camera/film_colour.h"
static unsigned rng=12345;
static unsigned rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static int sb(unsigned char b){return b<128?b:(int)b-256;}
/* Exact-size heap workspace so the sanitizer catches any overrun. */
static int begin(film_retro *r,unsigned w,unsigned h,unsigned v,unsigned l,unsigned seed,int mono,void **work) {
 size_t n=film_retro_work_size(w);*work=n?malloc(n):0;
 return film_retro_begin(r,w,h,v,l,seed,mono,*work,n);
}
static void fill(unsigned char *p,size_t n,unsigned char u,unsigned char y,unsigned char v) {
 for(size_t i=0;i<n;i+=4){p[i]=u;p[i+1]=y;p[i+2]=v;p[i+3]=y;}
}
static void apply(const film_retro *r,unsigned char *img,unsigned w,unsigned h,unsigned chunk) {
 for(unsigned row=0;row<h;row++)for(unsigned c=0;c<w;c+=chunk)
  film_retro_row(r,row,img+(size_t)row*w*2+c*2,c,w-c<chunk?w-c:chunk);
}
int main(int argc,char **argv) {
 assert(argc==2);
 film_retro r;void *work;
 /* Argument checks. */
 assert(film_retro_work_size(0)==0&&film_retro_work_size(3)==0&&film_retro_work_size(3746)==0);
 assert(film_retro_begin(&r,64,48,0,0,1,0,0,0)==0);                 /* both off: inactive */
 assert(film_retro_begin(&r,64,48,4,0,1,0,0,0)==-1&&film_retro_begin(&r,64,48,0,4,1,0,0,0)==-1);
 { size_t n=film_retro_work_size(64);unsigned char *w=malloc(n+1);
   assert(film_retro_begin(&r,64,48,1,0,1,0,w,n-1)==-1);         /* short workspace */
   assert(film_retro_begin(&r,64,48,1,0,1,0,w+1,n)==-1);         /* misaligned */
   assert(film_retro_begin(&r,64,0,1,0,1,0,w,n)==-1);free(w); }

 /* Vignette: neutral stays neutral, the centre is untouched, corners darken
  * more at each level, and luma never increases. */
 unsigned W=640,H=480,prev_corner=255;unsigned char *img=malloc(W*H*2);
 for(unsigned level=1;level<=3;level++) {
  assert(begin(&r,W,H,level,0,1,0,&work)==1);
  fill(img,W*H*2,0,200,0);apply(&r,img,W,H,W);
  unsigned char *centre=img+(size_t)(H/2)*W*2+(W/2)*2,*corner=img;
  assert(centre[1]==200&&centre[3]==200);
  assert(corner[1]<prev_corner&&corner[1]<200);prev_corner=corner[1];
  for(size_t i=0;i<W*H*2;i+=4)assert(img[i]==0&&img[i+2]==0&&img[i+1]<=200&&img[i+3]<=200);
  free(work);
 }
 assert(prev_corner<=200*42/100+2);                                /* High: about 42% at corners */

 /* Light leaks: deterministic per seed, varied across seeds, only ever
  * brighten, add warm chroma (V>0 or U<0), and stay neutral for mono looks. */
 { unsigned char *a=malloc(W*H*2),*b=malloc(W*H*2);int differs=0;
   for(unsigned seed=1;seed<=24;seed++) {
    assert(begin(&r,W,H,0,2,seed,0,&work)==1);assert(r.count>=1&&r.count<=2);
    fill(a,W*H*2,0,60,0);apply(&r,a,W,H,W);
    void *work2;film_retro r2;assert(begin(&r2,W,H,0,2,seed,0,&work2)==1);
    fill(b,W*H*2,0,60,0);apply(&r2,b,W,H,W);assert(!memcmp(a,b,W*H*2));
    int warm=0;
    for(size_t i=0;i<W*H*2;i+=4){
     assert(a[i+1]>=60&&a[i+3]>=60);
     if(a[i+1]>60){assert(sb(a[i+2])>=0&&sb(a[i])<=0);warm|=sb(a[i+2])>0;}
    }
    assert(warm);
    film_retro m;void *mw;assert(begin(&m,W,H,0,2,seed,1,&mw)==1);
    fill(b,W*H*2,0,60,0);apply(&m,b,W,H,W);
    for(size_t i=0;i<W*H*2;i+=4)assert(b[i]==0&&b[i+2]==0&&b[i+1]==a[i+1]);
    if(seed>1){film_retro p;void *pw;assert(begin(&p,W,H,0,2,seed-1,0,&pw)==1);
     differs|=memcmp(&p.leak,&r.leak,sizeof(r.leak))!=0;free(pw);}
    free(work);free(work2);free(mw);
   }
   assert(differs);
   /* Low has one leak, High two on different edges; strength rises with level. */
   assert(begin(&r,W,H,0,1,7,0,&work)==1&&r.count==1);free(work);
   assert(begin(&r,W,H,0,3,7,0,&work)==1&&r.count==2&&r.leak[0].edge!=r.leak[1].edge);free(work);
   free(a);free(b); }

 /* Spans of any even size give the same row as one call, on random data,
  * at small and full widths. */
 unsigned widths[]={2,4,6,64,1024,3648},heights[]={1,2,3,31,24};
 for(unsigned wi=0;wi<6;wi++)for(unsigned hi=0;hi<5;hi++) {
  unsigned w=widths[wi],h=heights[hi];size_t n=(size_t)w*h*2;
  unsigned char *src=malloc(n),*one=malloc(n),*chunked=malloc(n);
  for(size_t i=0;i<n;i++)src[i]=(unsigned char)rnd();
  for(unsigned v=0;v<=3;v++)for(unsigned l=0;l<=3;l++) {
   int active=begin(&r,w,h,v,l,rnd(),rnd()&1,&work);assert(active==(v||l));
   memcpy(one,src,n);memcpy(chunked,src,n);
   if(active){apply(&r,one,w,h,w);apply(&r,chunked,w,h,w<512?2:512);}
   assert(!memcmp(one,chunked,n));
   film_retro_row(&r,h,one,0,w);                                    /* out-of-range row ignored */
   if(active)film_retro_row(&r,0,one,w,2);                          /* out-of-range span ignored */
   free(work);
  }
  free(src);free(one);free(chunked);
 }

 /* Misaligned rows take the byte path and must match the aligned word path. */
 { unsigned w=640,h=8;size_t n=(size_t)w*h*2;unsigned char *al=malloc(n),*raw=malloc(n+1),*mis=raw+1;
   for(size_t i=0;i<n;i++)al[i]=(unsigned char)rnd();memcpy(mis,al,n);
   assert(begin(&r,w,h,3,3,77,0,&work)==1);apply(&r,al,w,h,w);apply(&r,mis,w,h,w);
   assert(!memcmp(al,mis,n));free(work);free(al);free(raw); }

 /* Colour pass integration: effects off match the plain pass byte for byte,
  * effects on match the plain pass followed by the effect rows, with and
  * without halation; a scratch too small for the tables still grades. */
 FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 unsigned char *lut=malloc(size);assert(fread(lut,1,size,f)==(size_t)size);fclose(f);assert(!film_lut_prepare(lut,size));
 { unsigned w=3648,h=8,stride=w*2;size_t n=(size_t)stride*h;
   unsigned char *src=malloc(n),*ref=malloc(n),*out=malloc(n),*scratch=malloc(16384),*small=malloc(9216);
   size_t hw=film_halo_level_work_size(w,3);unsigned char *halo=malloc(hw);
   for(size_t i=0;i<n;i++)src[i]=(unsigned char)rnd();
   int mono=0;(void)mono;
   for(int with_halo=0;with_halo<2;with_halo++)for(unsigned v=0;v<=3;v+=3)for(unsigned l=0;l<=3;l+=2) {
    memcpy(ref,src,n);memcpy(out,src,n);
    if(with_halo){
     assert(!film_uyvy_map_halo_cached(ref,n,w,h,stride,lut,3,halo,hw,scratch,16384,0));
     assert(!film_uyvy_map_halo_cached_retro(out,n,w,h,stride,lut,3,halo,hw,scratch,16384,0,v,l,99,0,0,0,0));
    } else {
     assert(!film_uyvy_map_cached(ref,n,w,h,stride,0,0,w,h,lut,scratch,16384,0));
     assert(!film_uyvy_map_cached_retro(out,n,w,h,stride,lut,scratch,16384,0,v,l,99,0,0,0,0));
    }
    if(v||l){assert(begin(&r,w,h,v,l,99,0,&work)==1);apply(&r,ref,w,h,w);free(work);assert(memcmp(ref,src,n));}
    assert(!memcmp(ref,out,n));
   }
   memcpy(ref,src,n);memcpy(out,src,n);
   assert(!film_uyvy_map_cached(ref,n,w,h,stride,0,0,w,h,lut,small,9216,0));
   assert(!film_uyvy_map_cached_retro(out,n,w,h,stride,lut,small,9216,0,3,3,99,0,0,0,0));
   assert(!memcmp(ref,out,n));                                        /* no room: grade only */
   assert(film_uyvy_map_cached_retro(out,n,w,h,stride,lut,scratch,16384,0,4,0,99,0,0,0,0)==-1);
   free(src);free(ref);free(out);free(scratch);free(small);free(halo); }
 free(img);free(lut);
 puts("Vignette/leaks: argument checks, neutral/centre/monotonic vignette, deterministic varied warm leaks, mono neutrality, span equivalence, sanitizer bounds and colour-pass composition with/without halation passed");
 return 0;
}
