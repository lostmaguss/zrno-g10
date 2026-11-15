/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise real spatial kernel under address/undefined-behavior sanitizers. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../camera/film_colour.h"
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 unsigned char *lut=malloc(size);assert(lut&&fread(lut,1,size,f)==(size_t)size);fclose(f);assert(!film_lut_validate(lut,size));assert(!film_lut_prepare(lut,size));
 unsigned widths[]={2,4,8,16,80,3648},heights[]={1,2,3,7,13,24,25,26,64,257};
 /* Each frame scale (L; M1, M2, S; the smallest, radius 1) passes the same
  * checks: the scaled windows divide through their reciprocal. */
 const unsigned scales[5]={65536,50589,28744,11498,1};
 for(unsigned si=0;si<5;si++){film_halo_scale(scales[si]);
 for(unsigned level=1;level<=3;level++)for(unsigned wi=0;wi<sizeof(widths)/sizeof(widths[0]);wi++)for(unsigned hi=0;hi<sizeof(heights)/sizeof(heights[0]);hi++) {
  unsigned w=widths[wi],h=heights[hi],stride=w*2+6;size_t n=(size_t)stride*h,need=film_halo_level_work_size(w,level);
  unsigned char *image=malloc(n+64),*work=malloc(need+64);assert(image&&work);memset(image,0xa3,n+64);memset(work,0xb7,need+64);
  for(size_t i=0;i<n;i++)image[32+i]=(unsigned char)(i*31+(i>>5));
  size_t capacity=16384;
  unsigned char *cached=malloc(n),*scratch=malloc(capacity+64);assert(cached&&scratch);
  memcpy(cached,image+32,n);memset(scratch,0xc9,capacity+64);
  assert(film_uyvy_map_halo_cached(cached,n,w,h,stride,lut,level,work+32,need,scratch+32,w*2-1,0)==-1);
  assert(film_uyvy_map_halo_cached(cached,n,w,h,stride,lut,level,work+32,need,work+32,w*2,0)==-1);
  assert(!memcmp(cached,image+32,n));
  assert(!film_uyvy_map_halo_level(image+32,n,w,h,stride,lut,level,work+32,need,0));
  for(unsigned i=0;i<32;i++){assert(image[i]==0xa3&&image[n+32+i]==0xa3);assert(work[i]==0xb7&&work[need+32+i]==0xb7);}
  for(unsigned y=0;y<h;y++)for(unsigned x=w*2;x<stride;x++){size_t i=(size_t)y*stride+x;assert(image[32+i]==(unsigned char)(i*31+(i>>5)));}
  assert(!film_uyvy_map_halo_cached(cached,n,w,h,stride,lut,level,work+32,need,scratch+32,capacity,0));
  assert(!memcmp(cached,image+32,n));
  for(unsigned i=0;i<32;i++)assert(scratch[i]==0xc9&&scratch[capacity+32+i]==0xc9);
  /* An overlap confined to the table area must fall back to a row copy. */
  if(w==3648&&h==7) {
   unsigned char *shared=malloc(w*2+need);assert(shared);
   for(size_t i=0;i<n;i++)cached[i]=(unsigned char)(i*31+(i>>5));
   assert(!film_uyvy_map_halo_cached(cached,n,w,h,stride,lut,level,
       shared+w*2,need,shared,w*2+need,0));
   assert(!memcmp(cached,image+32,n));
   free(shared);
  }
  free(cached);free(scratch);free(image);free(work);
 }}
 film_halo_scale(65536);
 free(lut);puts("Spatial kernel: 180 level/tiny/border/ring-wrap/full-width geometries at 5 frame scales passed direct/cached byte equality, address/UB sanitizers and scratch guards");
}
