/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Print effects: argument checks, Off byte-identical, dust varied per
 * seed with the right polarity and growing with level, the date stamp inside
 * its corner box in orange (neutral on black-and-white), every printable
 * character rendered and others shown as '?', long text kept in the frame,
 * span equivalence and sanitizer bounds, and the colour-pass hook. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../camera/film_print.h"
#include "../camera/film_colour.h"
static int sb(unsigned char b){return b<128?b:(int)b-256;}
static void fill(unsigned char *p,size_t n,unsigned char u,unsigned char y,unsigned char v){for(size_t i=0;i<n;i+=4){p[i]=u;p[i+1]=y;p[i+2]=v;p[i+3]=y;}}
static int apply(unsigned char *img,unsigned w,unsigned h,const film_print_settings *s,unsigned seed,int mono,unsigned chunk) {
 film_print p;int r=film_print_begin(&p,w,h,s,seed,mono);
 if(r>0)for(unsigned row=0;row<h;row++)for(unsigned c=0;c<w;c+=chunk)film_print_row(&p,row,img+(size_t)row*w*2+c*2,c,w-c<chunk?w-c:chunk);
 return r;
}
static film_print_settings S(unsigned dust,unsigned dark,unsigned stamp,const char *text) {
 film_print_settings s;memset(&s,0,sizeof(s));s.dust=dust;s.dust_dark=dark;s.stamp=stamp;
 strncpy(s.text,text,FILM_PRINT_TEXT);return s;
}
int main(int argc,char **argv) {
 assert(argc==2);
 const unsigned W=3648,H=2736;const size_t N=(size_t)W*H*2;
 unsigned char *a=malloc(N),*b=malloc(N),*ref=malloc(N);
 film_print p;film_print_settings s;

 /* Arguments. */
 assert(film_print_begin(&p,W,H,0,1,0)==0);
 s=S(0,0,0,"");assert(film_print_begin(&p,W,H,&s,1,0)==0);
 s=S(4,0,0,"");assert(film_print_begin(&p,W,H,&s,1,0)==-1);
 s=S(0,0,5,"x");assert(film_print_begin(&p,W,H,&s,1,0)==-1);
 s=S(0,0,1,"x");assert(film_print_begin(&p,3,H,&s,1,0)==-1&&film_print_begin(&p,0,H,&s,1,0)==-1);
 s=S(0,0,1,"   ");assert(film_print_begin(&p,W,H,&s,1,0)==0);   /* blank text: nothing to stamp */

 /* Dust: light marks on prints, dark on slides; deterministic per seed,
  * varied across seeds, growing with level, only a small share of the frame. */
 { size_t prev=0;
   for(unsigned level=1;level<=3;level++){
    s=S(level,0,0,"");fill(a,N,0,100,0);apply(a,W,H,&s,7,0,W);
    size_t k=0;for(size_t i=1;i<N;i+=2){assert(a[i]>=100);k+=a[i]>100;}
    assert(k>prev&&k<N/2/200);prev=k;
   }
   s=S(3,1,0,"");fill(a,N,0,100,0);apply(a,W,H,&s,7,0,W);
   for(size_t i=1;i<N;i+=2)assert(a[i]<=100);
   fill(b,N,0,100,0);apply(b,W,H,&s,7,0,W);assert(!memcmp(a,b,N));
   fill(b,N,0,100,0);apply(b,W,H,&s,8,0,W);assert(memcmp(a,b,N)); }

 /* Date stamp: only inside its corner box; orange light on colour looks,
  * neutral on black-and-white; each corner lands in its quadrant. */
 for(unsigned corner=1;corner<=4;corner++){
  s=S(0,0,corner,"'26 11 05");fill(a,N,0,20,0);assert(apply(a,W,H,&s,1,0,W)==1);
  film_print_begin(&p,W,H,&s,1,0);
  unsigned lit=0;
  for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
   const unsigned char *px=a+(size_t)y*W*2+x*2;
   if(px[1]==20)continue;
   assert(x>=p.x0&&x<p.x1&&y>=p.y0&&y<p.y1&&px[1]>20);lit++;
   const unsigned char *q=a+(size_t)y*W*2+(x&~1u)*2;
   if(px[1]>120)assert(sb(q[0])<0&&sb(q[2])>0);
  }
  assert(lit>2000);
  /* The glow lights pixels between and around the dots, fading outward. */
  { unsigned dim=0;for(unsigned y=p.y0;y<p.y1;y++)for(unsigned x=p.x0;x<p.x1;x++){unsigned l=a[(size_t)y*W*2+x*2+1];dim+=l>21&&l<60;}
    assert(dim>lit/4); }
  int right=corner==FILM_STAMP_BOTTOM_RIGHT||corner==FILM_STAMP_TOP_RIGHT,bottom=corner<=FILM_STAMP_BOTTOM_LEFT;
  assert(right?p.x0>W/2:p.x1<W/2);assert(bottom?p.y0>H/2:p.y1<H/2);
  assert(p.y1-p.y0<H/8);                                         /* about 4 % of the height plus glow */
 }
 s=S(0,0,1,"'26 11 05");fill(a,N,0,20,0);apply(a,W,H,&s,1,1,W);
 for(size_t i=0;i<N;i+=4)assert(a[i]==0&&a[i+2]==0);            /* neutral on black-and-white */

 /* Every printable character draws dots (except space); non-printable
  * bytes draw exactly like '?'; different characters differ. */
 { char t[2]={0,0};unsigned char *g=malloc(N);
   t[0]=' ';s=S(0,0,4,t);fill(a,N,0,0,0);assert(apply(a,W,H,&s,1,0,W)==0);
   for(int c=33;c<=126;c++){
    t[0]=(char)c;s=S(0,0,4,t);fill(a,N,0,0,0);assert(apply(a,W,H,&s,1,0,W)==1);
    size_t k=0;for(size_t i=1;i<N;i+=2)k+=a[i]!=0;assert(k>100);
    if(c=='0')memcpy(g,a,N);
    if(c=='8')assert(memcmp(g,a,N));
   }
   t[0]='?';s=S(0,0,4,t);fill(a,N,0,0,0);apply(a,W,H,&s,1,0,W);
   t[0]=(char)0x80;s=S(0,0,4,t);fill(b,N,0,0,0);apply(b,W,H,&s,1,0,W);assert(!memcmp(a,b,N));
   t[0]='\t';s=S(0,0,4,t);fill(b,N,0,0,0);apply(b,W,H,&s,1,0,W);assert(!memcmp(a,b,N));
   free(g); }

 /* A full 24-character text keeps inside the frame margins. */
 s=S(0,0,2,"WWWWWWWWWWWWWWWWWWWWWWWW");assert(film_print_begin(&p,W,H,&s,1,0)==1);
 assert(p.len==24&&p.x0>0&&p.x1<W&&p.pitch>=2);
 s=S(0,0,1,"WWWWWWWWWWWWWWWWWWWWWWWW");fill(a,N,0,20,0);apply(a,W,H,&s,1,0,W);

 /* Everything at once: spans of any even size equal whole rows, at small and
  * full sizes (sanitizers watch the bounds). */
 { unsigned ws[]={2,64,1000,3648},hs[]={1,7,300,2736};
   for(unsigned i=0;i<4;i++){
    unsigned w=ws[i],h=hs[i];size_t n=(size_t)w*h*2;
    unsigned char *x=malloc(n),*y=malloc(n);
    for(size_t j=0;j<n;j++)x[j]=(unsigned char)(j*2654435761u>>13);
    memcpy(y,x,n);
    s=S(3,0,3,"Zrno ~ {G12} *\\|");
    apply(x,w,h,&s,99,1,w);apply(y,w,h,&s,99,1,w<512?2:512);assert(!memcmp(x,y,n));
    memcpy(y,x,n);apply(x,w,h,&s,5,0,w);apply(y,w,h,&s,5,0,2);assert(!memcmp(x,y,n));
    free(x);free(y);
   } }

 /* Colour pass: no settings or all Off match the plain pass byte for byte;
  * with effects the pass equals grading followed by the print rows. */
 FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 unsigned char *lut=malloc(size);assert(fread(lut,1,size,f)==(size_t)size);fclose(f);assert(!film_lut_prepare(lut,size));
 { unsigned w=1600,h=300,stride=w*2;size_t n=(size_t)stride*h;unsigned char *src=malloc(n),*x=malloc(n),*y=malloc(n),*scratch=malloc(16384);
   for(size_t j=0;j<n;j++)src[j]=(unsigned char)(j*2246822519u>>11);
   memcpy(x,src,n);assert(!film_uyvy_map_cached(x,n,w,h,stride,0,0,w,h,lut,scratch,16384,0));
   memcpy(y,src,n);assert(!film_uyvy_map_cached_retro(y,n,w,h,stride,lut,scratch,16384,0,0,0,1,0,0,0,0));assert(!memcmp(x,y,n));
   s=S(0,0,0,"");memcpy(y,src,n);assert(!film_uyvy_map_cached_retro(y,n,w,h,stride,lut,scratch,16384,0,0,0,1,0,0,0,&s));assert(!memcmp(x,y,n));
   s=S(3,0,1,"'26 11 05");
   apply(x,w,h,&s,1,0,w);
   memcpy(y,src,n);assert(!film_uyvy_map_cached_retro(y,n,w,h,stride,lut,scratch,16384,0,0,0,1,0,0,0,&s));
   assert(!memcmp(x,y,n));
   s=S(4,0,0,"");assert(film_uyvy_map_cached_retro(y,n,w,h,stride,lut,scratch,16384,0,0,0,1,0,0,0,&s)==-1);
   free(src);free(x);free(y);free(scratch); }
 free(a);free(b);free(ref);free(lut);
 puts("Print effects: argument checks, Off byte-identical, dust per-seed/polarity/levels, stamp in its corner box (orange; neutral on black-and-white), all printable ASCII drawn and others as '?', 24 characters in frame, span equivalence, sanitizer bounds and colour-pass hook passed");
 return 0;
}
