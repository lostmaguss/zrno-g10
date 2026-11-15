/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exhaustive host check: every UYVY pair (U,Y0,V,Y1), all 2^32, through the
 * reference and a variant LUT kernel. Build twice-renamed objects via -D. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int ref_map(unsigned char*,size_t,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,const void*,unsigned char*,size_t,void(*)(unsigned,unsigned));
int var_map(unsigned char*,size_t,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,const void*,unsigned char*,size_t,void(*)(unsigned,unsigned));
int main(int argc,char **argv){
 FILE *f=fopen(argv[1],"rb");static unsigned char lut[300000],blut[300000];size_t n=fread(lut,1,sizeof lut,f);fclose(f);
 memcpy(blut,lut,n);
 if(argc>2&&!strcmp(argv[2],"bias"))for(size_t i=16;i+1<n;i+=2)blut[i+1]^=0x80;
 const unsigned W=2048,H=32,bytes=W*2*H; /* 65536 pairs = all (U,V) for one (Y0,Y1) */
 unsigned char *a=malloc(bytes),*b=malloc(bytes),*sa=aligned_alloc(4,16384),*sb=aligned_alloc(4,16384);
 unsigned long long bad=0;
 for(unsigned y=0;y<65536;y++){
  for(unsigned i=0;i<65536;i++){unsigned char *p=a+i*4;p[0]=i&255;p[1]=y&255;p[2]=i>>8;p[3]=y>>8;}
  memcpy(b,a,bytes);
  if(ref_map(a,bytes,W,H,W*2,0,0,W,H,lut,sa,16384,0)||var_map(b,bytes,W,H,W*2,0,0,W,H,blut,sb,16384,0)){puts("rc");return 2;}
  if(memcmp(a,b,bytes)){bad++;if(bad<5)printf("mismatch y0=%u y1=%u\n",y&255,y>>8);}
 }
 printf("%s %s: %llu mismatching (Y0,Y1) groups of 65536\n",argv[1],argc>2?argv[2]:"",bad);return bad!=0;
}
