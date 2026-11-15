/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Toy lens: argument checks, flat-image and sharp-centre invariance, corner
 * softening growing with level, red/cyan aberration fringes on neutral edges, span
 * equivalence, sanitizer bounds, and colour-pass composition (lens on the
 * input, then grading) with and without halation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../camera/film_toy.h"
#include "../camera/film_colour.h"
static unsigned rng=777;
static unsigned rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static int sb(unsigned char b){return b<128?b:(int)b-256;}
/* Run the lens over a whole image in row order, in spans of `chunk` pixels. */
static void lens(unsigned char *img,unsigned w,unsigned h,unsigned level,unsigned chunk) {
 film_toy t;size_t n=film_toy_work_size(w);void *work=malloc(n);
 assert(film_toy_begin(&t,img,w*2,w,h,level,work,n)==(level?1:0));
 for(unsigned r=0;r<h;r++)for(unsigned c=0;c<w;c+=chunk)film_toy_row(&t,r,img+(size_t)r*w*2+c*2,c,w-c<chunk?w-c:chunk);
 free(work);
}
static double region_variance(const unsigned char *img,unsigned w,unsigned x0,unsigned y0,unsigned x1,unsigned y1) {
 double s=0,s2=0;unsigned n=0;
 for(unsigned y=y0;y<y1;y++)for(unsigned x=x0;x<x1;x++){double v=img[(size_t)y*w*2+x*2+1];s+=v;s2+=v*v;n++;}
 return s2/n-(s/n)*(s/n);
}
int main(int argc,char **argv) {
 assert(argc==2);
 film_toy t;
 /* Arguments. */
 assert(film_toy_work_size(0)==0&&film_toy_work_size(3)==0&&film_toy_work_size(3746)==0);
 { unsigned char img[64*2*8];size_t n=film_toy_work_size(64);unsigned char *w=malloc(n);
   assert(film_toy_begin(&t,img,128,64,8,0,0,0)==0);
   assert(film_toy_begin(&t,img,128,64,8,4,w,n)==-1&&film_toy_begin(&t,img,128,64,8,1,w,n-1)==-1);
   assert(film_toy_begin(&t,0,128,64,8,1,w,n)==-1&&film_toy_begin(&t,img,127,64,8,1,w,n)==-1);
   assert(film_toy_begin(&t,img,128,64,0,1,w,n)==-1);free(w); }

 /* A flat frame stays exactly flat (including negative chroma) at every level. */
 unsigned W=1024,H=768;size_t N=(size_t)W*H*2;
 unsigned char *img=malloc(N),*ref=malloc(N);
 for(unsigned level=1;level<=3;level++) {
  for(size_t i=0;i<N;i+=4){img[i]=(unsigned char)-23;img[i+1]=141;img[i+2]=37;img[i+3]=141;}
  memcpy(ref,img,N);lens(img,W,H,level,W);assert(!memcmp(img,ref,N));
 }

 /* Sharp neutral bars: the centre keeps its contrast (only the aberration,
  * under a pixel there, touches it); the corners lose contrast, more at
  * each level. */
 double prev=1e9;
 for(unsigned level=1;level<=3;level++) {
  for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){unsigned char *p=img+(size_t)y*W*2+x*2;p[1]=((x/12)&1)?220:30;p[0]=0;}
  memcpy(ref,img,N);lens(img,W,H,level,W);
  assert(region_variance(img,W,W/2-60,H/2-40,W/2+60,H/2+40)>0.9*region_variance(ref,W,W/2-60,H/2-40,W/2+60,H/2+40));
  double corner=region_variance(img,W,0,0,120,90),orig=region_variance(ref,W,0,0,120,90);
  assert(corner<orig*0.6&&corner<prev);prev=corner;
 }

 /* Chromatic aberration on a full G12 frame of neutral 128 px bars: edges
  * outside the centre gain red (Cr > 0, Cb < 0) and cyan (Cr < 0, Cb > 0)
  * fringes, which grow from the centre outward (red shifted 6/10/14 px at
  * the corners, in proportion to the radius). */
 { unsigned FW=3648,FH=2736;size_t FN=(size_t)FW*FH*2;unsigned char *f=malloc(FN);
   for(unsigned level=1;level<=3;level++) {
    for(unsigned y=0;y<FH;y++)for(unsigned x=0;x<FW;x++){unsigned char *p=f+(size_t)y*FW*2+x*2;p[1]=((x/128)&1)?220:30;if(!(x&1))p[0]=p[2]=0;}
    lens(f,FW,FH,level,FW);
    int red=0,cyan=0,centre=0,outer=0;
    for(unsigned y=1300;y<1400;y++)for(unsigned x=200;x<900;x+=2) {
     const unsigned char *p=f+(size_t)y*FW*2+x*2;int cb=sb(p[0]),cr=sb(p[2]);
     if(cr>outer)outer=cr;
     if(cr>4){red=1;assert(cb<=0);}
     if(cr<-4){cyan=1;assert(cb>=0);}
    }
    for(unsigned y=FH/2-40;y<FH/2+40;y++)for(unsigned x=FW/2-200;x<FW/2+200;x+=2){int cr=sb(f[(size_t)y*FW*2+x*2+2]);if(cr>centre)centre=cr;}
    assert(red&&cyan&&centre<outer);
   }
   free(f); }

 /* No straight lines: a step edge in a fully blurred zone must
  * blur into a smooth curve. Bilinear grid sampling ended its ramps with a
  * slope break at a grid node, and fringes from the sharp grid drew thin
  * coloured lines over the blur; both show as a large second difference of
  * 8 px window means (old code: luma 4.5-5, chroma 4.25; a Gaussian: 0.7).
  * Edge positions step through every 8 px phase of the 64 px grid. */
 { unsigned FW=3648,FH=2736;unsigned char *f=malloc((size_t)FW*FH*2);double luma=0,chroma=0;
   size_t n=film_toy_work_size(FW);void *work=malloc(n);
   for(int dir=0;dir<2;dir++)for(unsigned e=0;e<64;e+=8) {
    unsigned edge=dir?1300+e:3400+e;double m[3][40];
    for(unsigned y=0;y<FH;y++)for(unsigned x=0;x<FW;x++){unsigned char *p=f+(size_t)y*FW*2+x*2;p[1]=(dir?y:x)<edge?40:220;if(!(x&1))p[0]=p[2]=0;}
    film_toy t2;assert(film_toy_begin(&t2,f,FW*2,FW,FH,3,work,n)==1);
    for(unsigned r=0;r<FH;r++)film_toy_row(&t2,r,f+(size_t)r*FW*2,0,FW);
    for(unsigned i=0;i<30;i++) {                        /* 8 px windows across the edge, 64 px deep */
     double s=0,su=0,sv=0;
     for(unsigned a=0;a<8;a++)for(unsigned b=0;b<64;b++) {
      unsigned x=dir?3500+b:3330+8*i+a,y=dir?1180+8*i+a:40+b;const unsigned char *p=f+(size_t)y*FW*2;
      s+=p[2*x+1];if(!((a|b)&1)){su+=sb(p[(x&~1u)*2]);sv+=sb(p[(x&~1u)*2+2]);}
     }
     m[0][i]=s/512;m[1][i]=su/128;m[2][i]=sv/128;
    }
    for(unsigned i=1;i+1<30;i++)for(unsigned c=0;c<3;c++) {
     double d=m[c][i-1]-2*m[c][i]+m[c][i+1];d=d<0?-d:d;
     if(c)chroma=d>chroma?d:chroma;else luma=d>luma?d:luma;
    }
   }
   assert(luma<2.5&&chroma<2.5);
   free(work);free(f); }

 /* In place equals reading a pristine copy: every row the lens reads after
  * changing it (the red rows above, for the bottom half) must come from its
  * cache of originals. Full G12 frame of random data, every level. */
 { unsigned FW=3648,FH=2736;size_t FN=(size_t)FW*FH*2,n=film_toy_work_size(FW);
   unsigned char *src=malloc(FN),*inplace=malloc(FN),*out=malloc(FN);void *work=malloc(n);
   for(size_t i=0;i<FN;i++)src[i]=(unsigned char)rnd();
   for(unsigned level=1;level<=3;level++) {
    film_toy a,b;
    memcpy(inplace,src,FN);memcpy(out,src,FN);
    assert(film_toy_begin(&a,inplace,FW*2,FW,FH,level,work,n)==1);
    for(unsigned r=0;r<FH;r++)film_toy_row(&a,r,inplace+(size_t)r*FW*2,0,FW);
    assert(film_toy_begin(&b,src,FW*2,FW,FH,level,work,n)==1);
    for(unsigned r=0;r<FH;r++)film_toy_row(&b,r,out+(size_t)r*FW*2,0,FW);
    assert(!memcmp(inplace,out,FN));
   }
   free(src);free(inplace);free(out);free(work); }

 /* Spans give the same result as whole rows; random data at small and
  * full widths exercises every edge clamp under the sanitizers. */
 unsigned widths[]={2,4,6,30,64,1024,3648},heights[]={1,2,3,9,40,130};
 for(unsigned wi=0;wi<7;wi++)for(unsigned hi=0;hi<6;hi++) {
  unsigned w=widths[wi],h=heights[hi];size_t n=(size_t)w*h*2;
  if(w==3648&&h>40)continue;
  unsigned char *a=malloc(n),*b=malloc(n);
  for(size_t i=0;i<n;i++)a[i]=(unsigned char)rnd();
  for(unsigned level=1;level<=3;level++){memcpy(b,a,n);unsigned char *c=malloc(n);memcpy(c,a,n);
   lens(b,w,h,level,w);lens(c,w,h,level,w<512?2:512);assert(!memcmp(b,c,n));free(c);}
  free(a);free(b);
 }

 /* Frame scale (M1, M2, S): cells shrink with the frame (even, any size) and
  * the knots stay at least two per cell; a flat frame stays flat, spans give
  * the same bytes, every geometry stays in bounds, a workspace too small for
  * the scaled layout is refused, and 65536 restores L's cells. */
 { const unsigned q[3]={50589,28744,11498},cells[3][4]={{0,24,50,50},{0,14,28,28},{0,6,12,12}},bsh[3][4]={{0,2,3,3},{0,1,2,2},{0,0,1,1}};
   const unsigned dims[3][2]={{2816,96},{1600,120},{640,480}};
   for(unsigned k=0;k<3;k++){unsigned w=dims[k][0],h=dims[k][1];size_t n=(size_t)w*h*2,tn=film_toy_work_size(3648);
    unsigned char *a=malloc(n),*b=malloc(n),*c=malloc(n);void *work=malloc(tn);film_toy_scale(q[k]);
    for(unsigned level=1;level<=3;level++) {
     assert(film_toy_begin(&t,a,w*2,w,h,level,work,tn)==1&&t.cell==cells[k][level]&&t.bshift==bsh[k][level]);
     assert(film_toy_begin(&t,a,w*2,w,h,level,work,64)==-1);
     for(size_t i=0;i<n;i+=4){a[i]=(unsigned char)-23;a[i+1]=141;a[i+2]=37;a[i+3]=141;}
     memcpy(b,a,n);film_toy_begin(&t,a,w*2,w,h,level,work,tn);
     for(unsigned r=0;r<h;r++)film_toy_row(&t,r,a+(size_t)r*w*2,0,w);assert(!memcmp(a,b,n));
     for(size_t i=0;i<n;i++)a[i]=(unsigned char)rnd();memcpy(b,a,n);memcpy(c,a,n);
     film_toy_begin(&t,b,w*2,w,h,level,work,tn);for(unsigned r=0;r<h;r++)film_toy_row(&t,r,b+(size_t)r*w*2,0,w);
     film_toy_begin(&t,c,w*2,w,h,level,work,tn);
     for(unsigned r=0;r<h;r++)for(unsigned x=0;x<w;x+=6)film_toy_row(&t,r,c+(size_t)r*w*2+x*2,x,w-x<6?w-x:6);
     assert(!memcmp(b,c,n)&&memcmp(a,b,n));
    }
    film_toy_scale(65536);free(a);free(b);free(c);free(work);}
   unsigned char im[64*2*8];size_t n=film_toy_work_size(64);void *w=malloc(n);
   film_toy_scale(0);assert(film_toy_begin(&t,im,128,64,8,3,w,n)==1&&t.cell==64&&t.shift==6&&t.bshift==3);free(w); }

 /* Colour pass: the lens applied to the input and then graded gives exactly
  * the kernel output. With halation, whose mask reads the frame before the
  * lens, it stays within a level on average of the lens followed by the
  * halation pass. Bad workspaces fail before any write. */
 FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 unsigned char *lut=malloc(size);assert(fread(lut,1,size,f)==(size_t)size);fclose(f);assert(!film_lut_prepare(lut,size));
 { unsigned w=1600,h=200,stride=w*2;size_t n=(size_t)stride*h,tn=film_toy_work_size(w),hw=film_halo_level_work_size(w,3);
   unsigned char *src=malloc(n),*out=malloc(n),*exp=malloc(n),*scratch=malloc(16384),*halo=malloc(hw),*tw=malloc(tn);
   for(size_t i=0;i<n;i++)src[i]=(unsigned char)rnd();
   for(unsigned level=1;level<=3;level++) {
    memcpy(exp,src,n);lens(exp,w,h,level,w);
    assert(!film_uyvy_map_cached(exp,n,w,h,stride,0,0,w,h,lut,scratch,16384,0));
    memcpy(out,src,n);
    assert(!film_uyvy_map_cached_retro(out,n,w,h,stride,lut,scratch,16384,0,0,0,1,level,tw,tn,0));
    assert(!memcmp(out,exp,n));
    memcpy(exp,src,n);lens(exp,w,h,level,w);assert(!film_uyvy_map_halo_cached(exp,n,w,h,stride,lut,3,halo,hw,scratch,16384,0));
    memcpy(out,src,n);assert(!film_uyvy_map_halo_cached(out,n,w,h,stride,lut,3,halo,hw,scratch,16384,0));
    double far=0,near=0;for(size_t i=1;i<n;i+=2){int e=(int)out[i]-(int)exp[i];far+=e<0?-e:e;}
    memcpy(out,src,n);assert(!film_uyvy_map_halo_cached_retro(out,n,w,h,stride,lut,3,halo,hw,scratch,16384,0,0,0,1,level,tw,tn,0));
    for(size_t i=1;i<n;i+=2){int e=(int)out[i]-(int)exp[i];near+=e<0?-e:e;}
    assert(near<far/2);
   }
   memcpy(out,src,n);
   assert(film_uyvy_map_cached_retro(out,n,w,h,stride,lut,scratch,16384,0,0,0,1,2,0,tn,0)==-1);
   assert(film_uyvy_map_cached_retro(out,n,w,h,stride,lut,scratch,16384,0,0,0,1,2,out,tn,0)==-1);
   assert(film_uyvy_map_cached_retro(out,n,w,h,stride,lut,scratch,16384,0,0,0,1,2,scratch,tn,0)==-1);
   assert(film_uyvy_map_halo_cached_retro(out,n,w,h,stride,lut,3,halo,hw,scratch,16384,0,0,0,1,2,halo,tn,0)==-1);
   assert(film_uyvy_map_cached_retro(out,n,w,h,stride,lut,scratch,16384,0,0,0,1,4,tw,tn,0)==-1);
   assert(!memcmp(out,src,n));
   free(src);free(out);free(exp);free(scratch);free(halo);free(tw); }
 free(img);free(ref);free(lut);
 puts("Toy lens: argument checks, exact flat invariance, sharp centre, level-graded corner softening, red/cyan aberration fringes on neutral edges, in-place equals pristine source, span equivalence, sanitizer bounds, frame scaling (cells, knots, flat, spans, workspace) and colour-pass composition with/without halation passed");
 return 0;
}
