/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual kernel under ASan/UBSan, including overlap rejection and row padding. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../camera/film_bloom.h"
#include "../camera/film_grain.h"

static void compare_fused(unsigned width,unsigned height,unsigned level,unsigned seed) {
 unsigned stride=width*2+6;
 size_t size=(size_t)stride*height;
 size_t glow_size=film_bloom_work_size(width),grain_size=film_grain_work_size(width);
 unsigned char *separate=malloc(size),*fused=malloc(size);
 unsigned char *scratch=malloc(width*2+64);
 void *glow=malloc(glow_size),*noise=malloc(grain_size);
 assert(separate&&fused&&scratch&&glow&&noise);
 for(size_t i=0;i<size;i++)separate[i]=(unsigned char)(i*29+(i>>8));
 memcpy(fused,separate,size);
 memset(scratch,0xa7,width*2+64);
 assert(!film_uyvy_bloom(separate,size,width,height,stride,level,glow,glow_size,0));
 assert(!film_uyvy_grain(separate,size,width,height,stride,level,seed,noise,grain_size,0));
 film_grain_rows state;
 assert(!film_grain_begin_rows(&state,width,height,level,seed,noise,grain_size));
 film_bloom_row_pass pass={scratch+32,width*2-1,film_grain_finish_row,&state};
 assert(film_uyvy_bloom_rows(fused,size,width,height,stride,level,glow,glow_size,&pass,0)==-1);
 assert(!state.row);
 pass.capacity=width*2;
 pass.scratch=fused;
 assert(film_uyvy_bloom_rows(fused,size,width,height,stride,level,glow,glow_size,&pass,0)==-1);
 assert(!state.row);
 pass.scratch=scratch+32;
 assert(!film_uyvy_bloom_rows(fused,size,width,height,stride,level,glow,glow_size,&pass,0));
 assert(state.row==height&&!memcmp(separate,fused,size));
 for(unsigned i=0;i<32;i++)assert(scratch[i]==0xa7&&scratch[width*2+32+i]==0xa7);
 free(separate);free(fused);free(scratch);free(glow);free(noise);
}

int main(void) {
 for(unsigned level=1;level<=3;level++) {
  compare_fused(2,1,level,0);
  compare_fused(6,3,level,1234);
  compare_fused(80,79,level,0x13579bdf);
 }
 compare_fused(3648,2736,3,0xcafebabe);
 puts("Fused effects: exact sequential bytes, full G12 geometry, scratch canaries and invalid scratch rejection passed");
 unsigned widths[]={2,4,6,10,80,3648},heights[]={1,2,3,4,7,25,97,257};
 for(unsigned level=1;level<=3;level++)for(unsigned wi=0;wi<6;wi++)for(unsigned hi=0;hi<8;hi++) {
  unsigned w=widths[wi],h=heights[hi],stride=w*2+6;size_t n=(size_t)stride*h,need=film_bloom_work_size(w);
  unsigned char *image=malloc(n+64),*work=malloc(need+64);assert(image&&work);
  memset(image,0xa3,n+64);memset(work,0xb7,need+64);
  for(size_t i=0;i<n;i++)image[32+i]=(unsigned char)(i*31+(i>>5));
  assert(!film_uyvy_bloom(image+32,n,w,h,stride,level,work+32,need,0));
  for(unsigned i=0;i<32;i++){assert(image[i]==0xa3&&image[n+32+i]==0xa3);assert(work[i]==0xb7&&work[need+32+i]==0xb7);}
  for(unsigned y=0;y<h;y++)for(unsigned x=w*2;x<stride;x++){size_t i=(size_t)y*stride+x;assert(image[32+i]==(unsigned char)(i*31+(i>>5)));}
  free(image);free(work);
 }
 size_t need=film_bloom_work_size(80);unsigned char image[640],saved[640],*work=malloc(need+4);assert(work);memset(image,128,sizeof(image));memcpy(saved,image,sizeof(image));
 assert(film_uyvy_bloom(image,640,80,4,160,1,work,need-1,0)==-1);
 assert(film_uyvy_bloom(image,640,80,4,160,1,work+1,need,0)==-1);
 assert(film_uyvy_bloom(image,640,80,4,160,1,image,need,0)==-1);
 assert(film_uyvy_bloom(image,640,80,4,160,4,work,need,0)==-1);
 assert(film_uyvy_bloom(image,640,79,4,160,1,work,need,0)==-1);
 assert(film_uyvy_bloom(image,639,80,4,160,1,work,need,0)==-1);
 assert(film_uyvy_bloom(image,640,80,4,159,1,work,need,0)==-1);
 assert(!memcmp(image,saved,sizeof(image)));free(work);
 /* Each frame scale (M1, M2, S, the smallest: radius 1) passes the same
  * bounds for Bloom and soft focus; L (65536) and 0 give the unscaled bytes,
  * and the scaled radius divides exactly (reciprocal equals division). */
 { const unsigned scales[4]={50589,28744,11498,1};
   for(unsigned si=0;si<4;si++)for(unsigned soft=0;soft<2;soft++)for(unsigned level=1;level<=3;level++)for(unsigned wi=0;wi<6;wi++)for(unsigned hi=0;hi<8;hi++) {
    unsigned w=widths[wi],h=heights[hi],stride=w*2+6;size_t n=(size_t)stride*h,need=film_bloom_work_size(w);
    unsigned char *image=malloc(n+64),*work=malloc(need+64);assert(image&&work);
    memset(image,0xa3,n+64);memset(work,0xb7,need+64);
    for(size_t i=0;i<n;i++)image[32+i]=(unsigned char)(i*31+(i>>5));
    film_bloom_scale(scales[si]);
    assert(!(soft?film_uyvy_soften:film_uyvy_bloom)(image+32,n,w,h,stride,level,work+32,need,0));
    film_bloom_scale(65536);
    for(unsigned i=0;i<32;i++){assert(image[i]==0xa3&&image[n+32+i]==0xa3);assert(work[i]==0xb7&&work[need+32+i]==0xb7);}
    for(unsigned y=0;y<h;y++)for(unsigned x=w*2;x<stride;x++){size_t i=(size_t)y*stride+x;assert(image[32+i]==(unsigned char)(i*31+(i>>5)));}
    free(image);free(work);
   }
   for(unsigned d=2;d<=625;d++){unsigned inv=(unsigned)((0x100000000ull+d-1u)/d);
    for(unsigned v=0;v<256u*d+d;v++)assert((unsigned)(((unsigned long long)v*inv)>>32)==v/d);}
   unsigned w=640,h=97;size_t n=(size_t)w*2*h,need=film_bloom_work_size(w);unsigned char *a=malloc(n),*b=malloc(n),*work=malloc(need);
   for(size_t i=0;i<n;i++)a[i]=(unsigned char)(i*37+(i>>3));memcpy(b,a,n);
   film_bloom_scale(0);assert(!film_uyvy_bloom(a,n,w,h,w*2,3,work,need,0));film_bloom_scale(65536);assert(!film_uyvy_bloom(b,n,w,h,w*2,3,work,need,0));
   assert(!memcmp(a,b,n));free(a);free(b);free(work); }
 puts("Bloom: 144 geometries/levels pass ASan/UBSan, also at 4 frame scales for Bloom and soft focus; exact reciprocal division; image/work guards, padding and pre-write size/alignment/overlap/level rejection pass");
 /* Hardware quarter mask: read in bounds (exact-size buffer between canaries),
  * used once, and ignored when its size does not match the frame. */
 for(unsigned level=1;level<=3;level++)for(unsigned wi=0;wi<6;wi++)for(unsigned hi=0;hi<8;hi++) {
  unsigned w=widths[wi],h=heights[hi],stride=w*2,qw=(w+3)/4,qh=(h+3)/4,qs=qw*2;size_t n=(size_t)stride*h,qn=(size_t)qs*qh,need=film_bloom_work_size(w);
  unsigned char *a=malloc(n),*b=malloc(n),*c=malloc(n),*q=malloc(qn+64),*work=malloc(need);assert(a&&b&&c&&q&&work);
  for(size_t i=0;i<n;i++)a[i]=(unsigned char)(i*37+(i>>3));
  memcpy(b,a,n);memcpy(c,a,n);memset(q,0xc5,qn+64);
  for(size_t i=0;i<qn;i++)q[32+i]=(unsigned char)(i*53);
  film_bloom_use_quarter(q+32,qs,qw,qh);assert(!film_uyvy_bloom(a,n,w,h,stride,level,work,need,0));
  for(unsigned i=0;i<32;i++)assert(q[i]==0xc5&&q[qn+32+i]==0xc5);
  assert(!film_uyvy_bloom(b,n,w,h,stride,level,work,need,0));                 /* used once: CPU mask now */
  film_bloom_use_quarter(q+32,qs,qw+1,qh);assert(!film_uyvy_bloom(c,n,w,h,stride,level,work,need,0));
  assert(!memcmp(b,c,n));
  film_bloom_use_quarter(q+32,qs-1,qw,qh);
  for(size_t i=0;i<n;i++)c[i]=(unsigned char)(i*37+(i>>3));
  assert(!film_uyvy_bloom(c,n,w,h,stride,level,work,need,0));assert(!memcmp(b,c,n));
  free(a);free(b);free(c);free(q);free(work);
 }
 puts("Bloom hardware quarter: 144 geometries read in bounds, used once, mismatched size or stride ignored");
}
