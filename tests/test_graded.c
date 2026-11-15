/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The colour pass on a frame Canon already graded (DIGIC colour engine):
 * bounds under sanitizers, untouched bytes where no effect applies, and the
 * same effects as today's path through an identity look. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../camera/film_colour.h"
static unsigned char *identity_lut(size_t *size) {
 unsigned n=17,i,count=n*n*n,h=2166136261u;
 unsigned char *p=malloc(16+count*6);assert(p);
 memcpy(p,"G12LUT1\0",8);p[8]=n;p[9]=0;p[10]=1;p[11]=0;
 for(i=0;i<count;i++){unsigned c[3]={i%n,(i/n)%n,i/(n*n)},k;for(k=0;k<3;k++){unsigned v=(c[k]*65535u+8u)/16u;p[16+i*6+k*2]=v&255;p[17+i*6+k*2]=v>>8;}}
 for(i=16;i<16+count*6;i++)h=(h^p[i])*16777619u;
 p[12]=h;p[13]=h>>8;p[14]=h>>16;p[15]=h>>24;*size=16+count*6;
 assert(!film_lut_prepare(p,*size));return p;
}
/* A UYVY test frame: smooth gradients, a bright neutral patch (highlights
 * for the glow) and some colour, all inside the RGB range (today's path clips
 * colours outside it even through an identity look). */
static void fill(unsigned char *img,unsigned w,unsigned h,unsigned stride) {
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x+=2){
  unsigned char *p=img+(size_t)y*stride+x*2;
  int bright=x>w/3&&x<w/2&&y>h/3&&y<h/2;
  p[1]=bright?250:(unsigned char)(50+(x*150)/(w?w:1));p[3]=bright?248:(unsigned char)(p[1]+3);
  p[0]=bright?0:(unsigned char)(signed char)((int)(y%20)-10);p[2]=bright?0:(unsigned char)(signed char)((int)(x%16)-8);
 }
}
int main(void) {
 size_t lut_size;unsigned char *lut=identity_lut(&lut_size);
 film_halo_tint tint={256,51,0,0};
 film_print_settings print;memset(&print,0,sizeof(print));print.dust=2;print.stamp=1;strcpy(print.text,"'26 11 07");
 unsigned widths[]={2,4,16,80,3648},heights[]={1,2,3,13,64,257};
 for(unsigned level=0;level<=3;level++)for(unsigned wi=0;wi<5;wi++)for(unsigned hi=0;hi<6;hi++)for(unsigned cached=0;cached<2;cached++) {
  unsigned w=widths[wi],h=heights[hi],stride=w*2+6;size_t n=(size_t)stride*h,need=level?film_halo_level_work_size(w,level):0,cap=16384;
  unsigned char *image=malloc(n+64),*work=malloc(need+64),*scratch=malloc(cap+64);assert(image&&work&&scratch);
  memset(image,0xa3,n+64);memset(work,0xb7,need+64);memset(scratch,0xc9,cap+64);
  for(size_t i=0;i<n;i++)image[32+i]=(unsigned char)(i*31+(i>>5));
  int rc=film_uyvy_graded_retro(image+32,n,w,h,stride,0,level,&tint,level?work+32:0,need,cached?scratch+32:0,cached?cap:0,0,
                                cached?2:0,cached?1:0,7,0,0,0,&print);
  assert(!rc);
  for(unsigned i=0;i<32;i++){assert(image[i]==0xa3&&image[n+32+i]==0xa3&&scratch[i]==0xc9&&scratch[cap+32+i]==0xc9);if(level)assert(work[i]==0xb7&&work[need+32+i]==0xb7);}
  for(unsigned y=0;y<h;y++)for(unsigned x=w*2;x<stride;x++){size_t i=(size_t)y*stride+x;assert(image[32+i]==(unsigned char)(i*31+(i>>5)));}
  free(image);free(work);free(scratch);
 }
 /* Rejections: a halation level without its tint or workspace. */
 { unsigned char img[64];size_t need=film_halo_level_work_size(16,1);unsigned char *work=malloc(need);
   assert(film_uyvy_graded_retro(img,64,16,2,32,0,1,0,work,need,0,0,0,0,0,0,0,0,0,0)==-1);
   assert(film_uyvy_graded_retro(img,64,16,2,32,0,1,&tint,0,0,0,0,0,0,0,0,0,0,0,0)==-1);free(work); }
 unsigned w=640,h=480,stride=w*2;size_t n=(size_t)stride*h,cap=16384;
 unsigned char *a=malloc(n),*b=malloc(n),*orig=malloc(n),*scratch=malloc(cap),*work=malloc(film_halo_level_work_size(w,3));
 assert(a&&b&&orig&&scratch&&work);
 fill(orig,w,h,stride);
 /* Nothing on: the graded frame is not touched at all. */
 memcpy(a,orig,n);assert(!film_uyvy_graded_retro(a,n,w,h,stride,0,0,0,0,0,scratch,cap,0,0,0,7,0,0,0,0));assert(!memcmp(a,orig,n));
 /* Halation leaves every pixel without glow byte-identical. */
 { unsigned char dark[1280*4];memset(dark,0,sizeof(dark));for(unsigned i=0;i<sizeof(dark);i+=4){dark[i+1]=60;dark[i+3]=61;dark[i]=5;dark[i+2]=250;}
   unsigned char copy[1280*4];memcpy(copy,dark,sizeof(dark));
   assert(!film_uyvy_graded_retro(dark,sizeof(dark),640,4,1280,0,3,&tint,work,film_halo_level_work_size(640,3),scratch,cap,0,0,0,7,0,0,0,0));
   assert(!memcmp(dark,copy,sizeof(dark))); }
 /* Through an identity look, today's path and the graded path agree within
  * the identity look's own rounding: vignette, leaks and print effects, and
  * halation with the identity tint. */
 int worst[2]={0,0};
 for(unsigned level=0;level<=3;level+=3) {
  memcpy(a,orig,n);memcpy(b,orig,n);
  size_t need=level?film_halo_level_work_size(w,level):0;
  if(level){assert(!film_uyvy_map_halo_cached_retro(a,n,w,h,stride,lut,level,work,need,scratch,cap,0,2,1,7,0,0,0,&print));}
  else assert(!film_uyvy_map_cached_retro(a,n,w,h,stride,lut,scratch,cap,0,2,1,7,0,0,0,&print));
  assert(!film_uyvy_graded_retro(b,n,w,h,stride,0,level,&tint,level?work:0,need,scratch,cap,0,2,1,7,0,0,0,&print));
  for(size_t i=0;i<n;i++){int d=(i&1)?(int)a[i]-(int)b[i]:(int)(signed char)a[i]-(int)(signed char)b[i];if(d<0)d=-d;if(d>worst[level?1:0])worst[level?1:0]=d;}
 }
 assert(worst[0]<=2&&worst[1]<=3);
 /* Monochrome looks keep neutral chroma when the glow is added, and on a
  * neutral frame the luma-only glow equals the RGB path's. */
 { memcpy(a,orig,n);for(size_t i=0;i<n;i+=2)a[i]=0;memcpy(b,a,n);film_halo_tint grey={60,60,60,0};
   assert(!film_uyvy_graded_retro(a,n,w,h,stride,1,3,&grey,work,film_halo_level_work_size(w,3),scratch,cap,0,0,0,7,0,0,0,0));
   assert(!film_uyvy_graded_retro(b,n,w,h,stride,0,3,&grey,work,film_halo_level_work_size(w,3),scratch,cap,0,0,0,7,0,0,0,0));
   for(size_t i=0;i<n;i+=2)assert(a[i]==0);
   assert(!memcmp(a,b,n)); }
 /* Monochrome looks keep a coloured frame's chroma: Canon's JPEG step turns
  * it into grey through the look's table, so glowing pixels must not lose it
  * (that gave them a darker grey, in squares where the glow's tail begins).
  * The glow only brightens the luma. */
 { memcpy(a,orig,n);film_halo_tint grey={60,60,60,0};int brighter=0;
   assert(!film_uyvy_graded_retro(a,n,w,h,stride,1,3,&grey,work,film_halo_level_work_size(w,3),scratch,cap,0,0,0,7,0,0,0,0));
   for(size_t i=0;i<n;i++){if(!(i&1))assert(a[i]==orig[i]);else{assert(a[i]>=orig[i]);brighter|=a[i]>orig[i];}}
   assert(brighter); }
 printf("Graded pass (DIGIC engine): 240 geometries/levels/cache modes pass sanitizers and guards; untouched without effects or glow; matches today's path through an identity look (effects max %d, with halation max %d levels); mono stays neutral\n",worst[0],worst[1]);
 free(a);free(b);free(orig);free(scratch);free(work);free(lut);
 return 0;
}
