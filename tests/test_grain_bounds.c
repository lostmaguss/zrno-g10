/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual grain kernel with address/UB sanitizers; no camera APIs. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../camera/film_grain.h"
int main(void) {
 unsigned widths[]={2,4,16,80,3648},heights[]={1,2,3,31,32,33,257};
 /* Each frame scale (L, M2, S, and the smallest) passes the same checks. */
 const unsigned scales[4]={65536,28744,11498,1};
 for(unsigned si=0;si<4;si++){film_grain_scale(scales[si]);
 for(unsigned level=0;level<=3;level++)for(unsigned wi=0;wi<5;wi++)for(unsigned hi=0;hi<7;hi++) {
  unsigned w=widths[wi],h=heights[hi],pitch=w*2+6;size_t n=(size_t)pitch*h,need=film_grain_work_size(w);
  unsigned char *a=malloc(n+64),*b=malloc(n+64),*work=malloc(need+64);assert(a&&b&&work);
  memset(a,0xa5,n+64);memset(work,0xb7,need+64);
  for(size_t i=0;i<n;i++)a[32+i]=(unsigned char)(i*31+(i>>4));memcpy(b,a,n+64);
  assert(!film_uyvy_grain(a+32,n,w,h,pitch,level,0x13579bdf,work+32,need,0));
  assert(!film_uyvy_grain(b+32,n,w,h,pitch,level,0x13579bdf,work+32,need,0));
  assert(!memcmp(a,b,n+64));
  for(unsigned i=0;i<32;i++){assert(a[i]==0xa5&&a[n+32+i]==0xa5);assert(work[i]==0xb7&&work[need+32+i]==0xb7);}
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<pitch;x++) {
   size_t i=(size_t)y*pitch+x;
   if(!level||!(x&1)||x>=w*2)assert(a[32+i]==(unsigned char)(i*31+(i>>4)));
  }
  free(a);free(b);free(work);
 }}
 film_grain_scale(65536);
 /* A smaller frame's grain is weaker (it stands for L's shrunk), in order;
  * 65536 and 0 are L. */
 { unsigned w=640,h=64,n=w*2*h;double sd[4];size_t need=film_grain_work_size(w);unsigned char *img=malloc(n),*work=malloc(need);
   const unsigned q[4]={65536,50589,28744,11498};
   for(unsigned k=0;k<4;k++){film_grain_scale(q[k]);memset(img,0,n);for(unsigned i=1;i<n;i+=2)img[i]=128;
    assert(!film_uyvy_grain(img,n,w,h,w*2,3,99,work,need,0));double s=0,s2=0;for(unsigned i=1;i<n;i+=2){s+=img[i];s2+=(double)img[i]*img[i];}
    sd[k]=s2/(n/2)-(s/(n/2))*(s/(n/2));}
   assert(sd[0]>sd[1]&&sd[1]>sd[2]&&sd[2]>sd[3]&&sd[3]>0);
   film_grain_scale(0);memset(img,0,n);for(unsigned i=1;i<n;i+=2)img[i]=128;assert(!film_uyvy_grain(img,n,w,h,w*2,3,99,work,need,0));
   double s=0,s2=0;for(unsigned i=1;i<n;i+=2){s+=img[i];s2+=(double)img[i]*img[i];}assert(s2/(n/2)-(s/(n/2))*(s/(n/2))==sd[0]);
   free(img);free(work); }
 unsigned char data[64],saved[64],work[32];memset(data,128,64);memcpy(saved,data,64);
 assert(film_uyvy_grain(data,64,8,4,16,4,1,work,32,0)==-1);
 assert(film_uyvy_grain(data,64,8,4,16,1,1,work,film_grain_work_size(8)-1,0)==-1);
 assert(film_uyvy_grain(data,64,8,4,16,1,1,data,32,0)==-1);
 assert(film_uyvy_grain(data,64,7,4,16,1,1,work,32,0)==-1);
 assert(!memcmp(data,saved,64));
 puts("Grain kernel: 140 level/tiny/full-width geometries at 4 frame scales pass ASan/UBSan, seed determinism, chroma/padding/canary invariance and pre-write rejection; smaller frames weaker in order, L unchanged");
}
