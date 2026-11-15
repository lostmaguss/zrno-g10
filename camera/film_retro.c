/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Independently written vignette and light-leak effects.
 * Integer only; all tables live in caller-provided workspace. Operates on
 * graded signed UYVY422 (U,Y0,V,Y1) one pixel pair at a time. */
#include "film_compact.h"
#include "film_retro.h"
#include <string.h>

/* Vignette: gain = 1 - s*smoothstep over normalised r^2 from VIGNETTE_FLAT
 * (a flat centre about 0.4 of the half-diagonal) to the corners. */
#define VIGNETTE_FLAT 40u
static const unsigned vignette_strength[4]={0,56,102,148};   /* Q8: 22%, 40%, 58% at the corners */
/* Light leaks: warm palette (RGB amber, red-orange, magenta-red, gold) in
 * signed full-range YCbCr, and the peak strength (0..255) per level. */
static const int leak_colours[4][3]={{163,-69,66},{127,-54,91},{112,-12,92},{198,-61,40}};
static const unsigned leak_strength[4]={0,90,150,210};

size_t film_retro_work_size(unsigned width) {
 size_t pairs=width/2;
 if(!width||width>3744||width%2)return 0;
 return ((pairs+1u)&~(size_t)1u)+256u*2u+FILM_RETRO_MAX_LEAKS*pairs;
}
static unsigned next_random(unsigned *state) {
 unsigned x=*state;x^=x<<13;x^=x>>17;x^=x<<5;*state=x;return x;
}
static unsigned pick(unsigned *state,unsigned lo,unsigned hi){return lo+next_random(state)%(hi-lo+1);}
/* Smooth 0..255 falloff: (1-(d/extent)^2)^2 for |d|<extent, else 0. */
static unsigned bump(int d,unsigned extent) {
 unsigned a=(unsigned)(d<0?-d:d),t;
 if(a>=extent)return 0;
 t=255u-(unsigned)((unsigned long long)a*a*255u/((unsigned long long)extent*extent));
 return (t*t+127u)/255u;
}
/* Across-edge falloff: (1-d/depth)^2 for 0<=d<depth. */
static unsigned ramp(unsigned d,unsigned depth) {
 unsigned t;
 if(d>=depth)return 0;
 t=255u-d*255u/depth;
 return (t*t+127u)/255u;
}
FILM_COMPACT int film_retro_begin(film_retro *r,unsigned width,unsigned height,unsigned vignette,
 unsigned leaks,unsigned seed,int mono,void *work,size_t work_size) {
 size_t need=film_retro_work_size(width);
 unsigned i,c,state=seed?seed:0x2545f491u;
 unsigned char *p=work;
 if(!r||!need||!height||vignette>3||leaks>3)return -1;
 memset(r,0,sizeof(*r));
 if(!vignette&&!leaks)return 0;
 if(!work||work_size<need||((size_t)work&1u))return -1;
 r->width=width;r->height=height;r->pairs=width/2;r->vignette=vignette;r->leaks=leaks;r->mono=mono!=0;
 r->diag2=(unsigned long long)width*width/4u+(unsigned long long)height*height/4u;
 r->nx2=p;p+=(r->pairs+1u)&~1u;
 r->gain=(unsigned short*)p;p+=256u*2u;
 for(i=0;i<FILM_RETRO_MAX_LEAKS;i++){r->column[i]=p;p+=r->pairs;}
 for(c=0;c<r->pairs;c++) {
  /* Pixels 2c and 2c+1 centre on 2c+1; half-pixel units avoid fractions. */
  long long dx=(long long)(4u*c+2u)-(long long)width;
  r->nx2[c]=(unsigned char)((unsigned long long)(dx*dx)*255u/(4u*r->diag2));
 }
 for(i=0;i<256;i++) {
  unsigned t=i<=VIGNETTE_FLAT?0:(i-VIGNETTE_FLAT)*4096u/(255u-VIGNETTE_FLAT);
  unsigned smooth=(unsigned)((unsigned long long)t*t*(3u*4096u-2u*t)>>24);   /* Q12 */
  r->gain[i]=(unsigned short)(4096u-((vignette_strength[vignette]*smooth+128u)>>8));
 }
 if(leaks) {
  r->count=leaks==1?1:leaks==3?2:1+next_random(&state)%2u;
  for(i=0;i<r->count;i++) {
   film_retro_leak *l=&r->leak[i];
   unsigned along,across,colour=next_random(&state)%4u;
   l->edge=next_random(&state)%4u;
   if(i&&l->edge==r->leak[0].edge)l->edge^=1u; /* a second leak uses the opposite edge */
   along=l->edge<2?height:width;across=l->edge<2?width:height;
   l->center=along*pick(&state,15,85)/100u;
   l->half=along*pick(&state,25,50)/100u;
   l->depth=across*pick(&state,18,35)/100u;
   if(!l->half)l->half=1;
   if(!l->depth)l->depth=1;
   l->strength=leak_strength[leaks]*pick(&state,75,100)/100u;
   l->y=leak_colours[colour][0];
   l->u=r->mono?0:leak_colours[colour][1];
   l->v=r->mono?0:leak_colours[colour][2];
   for(c=0;c<r->pairs;c++) {
    unsigned x=2u*c;
    if(l->edge==0)r->column[i][c]=(unsigned char)ramp(x,l->depth);
    else if(l->edge==1)r->column[i][c]=(unsigned char)ramp(width-2u-x,l->depth);
    else r->column[i][c]=(unsigned char)bump((int)x-(int)l->center,l->half);
   }
   r->lo[i]=r->pairs;r->hi[i]=0;
   for(c=0;c<r->pairs;c++)if(r->column[i][c]){if(c<r->lo[i])r->lo[i]=c;r->hi[i]=c+1;}
  }
 }
 return 1;
}
static unsigned char clamp_chroma(int v){return (unsigned char)(v<-128?-128:v>127?127:v);}
static int retro_signed(unsigned char b){return b<128?b:(int)b-256;}
/* Vignette over pairs [first,end): the flat centre (gain exactly 1) is skipped.
 * Gains never exceed 1, so luma and chroma stay in range without clamping. */
static void vignette_span(unsigned char *restrict p,const unsigned char *restrict nx2,
 const unsigned short *restrict gain,unsigned ny2,unsigned first,unsigned end) {
 if(((size_t)p&3u)==0) {
  /* Aligned rows: one 32-bit load/store per pair (U,Y0,V,Y1 little-endian). */
  unsigned *restrict w=(unsigned*)p;
  for(unsigned c=first;c<end;c++,w++) {
   unsigned q=nx2[c]+ny2;
   if(q<=VIGNETTE_FLAT)continue;
   unsigned g=gain[q>255u?255u:q],x=*w;
   int u=(int)(signed char)(x&255u),v=(int)(signed char)((x>>16)&255u);
   unsigned y0=(((x>>8)&255u)*g+2048u)>>12,y1=((x>>24)*g+2048u)>>12;
   u=(u*(int)g+2048)>>12;v=(v*(int)g+2048)>>12;
   *w=((unsigned)u&255u)|(y0<<8)|(((unsigned)v&255u)<<16)|(y1<<24);
  }
  return;
 }
 for(unsigned c=first;c<end;c++,p+=4) {
  unsigned q=nx2[c]+ny2;
  if(q<=VIGNETTE_FLAT)continue;
  unsigned g=gain[q>255u?255u:q];
  p[1]=(unsigned char)((p[1]*g+2048u)>>12);p[3]=(unsigned char)((p[3]*g+2048u)>>12);
  p[0]=(unsigned char)((retro_signed(p[0])*(int)g+2048)>>12);
  p[2]=(unsigned char)((retro_signed(p[2])*(int)g+2048)>>12);
 }
}
/* One light leak over pairs [first,end): screen its luma over the image and
 * add its chroma, fading chroma as the pixel nears white so blown leaks stay
 * bright rather than garish. The screen keeps luma within 0..255. */
static void leak_span(unsigned char *restrict p,const unsigned char *restrict column,
 unsigned rowv,unsigned ly,int lu,int lv,unsigned first,unsigned end) {
 for(unsigned c=first;c<end;c++,p+=4) {
  unsigned a=(column[c]*rowv+32768u)>>16;   /* 0..255 */
  if(!a)continue;
  unsigned y0=p[1],y1=p[3];
  y0+=((255u-y0)*a*ly)>>16;y1+=((255u-y1)*a*ly)>>16;
  p[1]=(unsigned char)y0;p[3]=(unsigned char)y1;
  if(lu|lv) {
   int fade=255-(int)((y0+y1)>>2);                 /* 128..255 */
   p[0]=clamp_chroma(retro_signed(p[0])+((((int)a*lu)*fade)>>16));
   p[2]=clamp_chroma(retro_signed(p[2])+((((int)a*lv)*fade)>>16));
  }
 }
}
void film_retro_row(const film_retro *r,unsigned row,unsigned char *pixels,
 unsigned start_col,unsigned span) {
 unsigned i,first=start_col/2,end=first+span/2;
 if(!r||!r->pairs||row>=r->height||end>r->pairs)return;
 if(r->vignette) {
  long long dy=2ll*row+1ll-(long long)r->height;
  unsigned ny2=(unsigned)((unsigned long long)(dy*dy)*255u/(4u*r->diag2));
  vignette_span(pixels,r->nx2,r->gain,ny2,first,end);
 }
 for(i=0;i<r->count;i++) {
  const film_retro_leak *l=&r->leak[i];
  unsigned v=l->edge==2?ramp(row,l->depth):l->edge==3?ramp(r->height-1u-row,l->depth):bump((int)row-(int)l->center,l->half);
  unsigned lo=r->lo[i]>first?r->lo[i]:first,hi=r->hi[i]<end?r->hi[i]:end;
  if(!v||lo>=hi)continue;
  leak_span(pixels+(lo-first)*4u,r->column[i],v*l->strength,(unsigned)l->y,l->u,l->v,lo,hi);
 }
}
