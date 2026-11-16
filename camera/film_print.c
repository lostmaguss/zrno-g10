/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Independently written print effects for signed UYVY422
 * rows. Integer only. The colour pass's scratch area is full, so per-shot
 * data lives in a small workspace of its own (one pass runs at a time).
 *
 * Dust and scratches: specks (two overlapping soft discs), hairs (thin
 * curved polylines) and long fine scratches. Scratches run horizontally,
 * the way film travels through a 35 mm camera. Objects come from the
 * per-shot seed, so every shot differs. Prints from negatives show dust as light marks; slide
 * and instant looks show it dark. Edges are antialiased from exact distances.
 *
 * Date stamp: an orange LED dot-matrix text (5x7 font, any printable ASCII)
 * in a chosen corner, about 4 % of the frame height, with a soft glow. The
 * light is added to the picture like an imprint exposed onto film, so it
 * washes out over bright areas. */
#include "film_print.h"
#include "film_width.h"
#include "film_font.h"
#include <stdlib.h>

#define REF_HEIGHT 2736u                 /* G12 frame height the sizes refer to */
static unsigned isqrt32(unsigned v) {
 unsigned r=0,b=1u<<30;
 while(b>v)b>>=2;
 while(b){if(v>=r+b){v-=r+b;r=(r>>1)+b;}else r>>=1;b>>=2;}
 return r;
}
static unsigned mix(unsigned x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
static unsigned rnd(const film_print *p,unsigned obj,unsigned k){return mix(p->seed^mix(obj*0x9e3779b1u+k*0x85ebca77u));}
/* cos of k/64 of a turn, Q8. */
static const short cos64[64]={256,255,251,245,237,226,213,198,181,162,142,121,98,74,50,25,
 0,-25,-50,-74,-98,-121,-142,-162,-181,-198,-213,-226,-237,-245,-251,-255,
 -256,-255,-251,-245,-237,-226,-213,-198,-181,-162,-142,-121,-98,-74,-50,-25,
 0,25,50,74,98,121,142,162,181,198,213,226,237,245,251,255};
/* cos of a/1024 of a turn, Q8, interpolated between the 64 table steps. */
static int cos1024(unsigned a){unsigned i=(a>>4)&63u,f=a&15u;return (cos64[i]*(int)(16u-f)+cos64[(i+1u)&63u]*(int)f)/16;}
static const unsigned specks_n[4]={0,8,18,36},hairs_n[4]={0,1,2,3},scratches_n[4]={0,0,1,2};
static int clamp_c(int v){return v<-128?-128:v>127?127:v;}
/* One colour pass runs at a time, so the per-shot data lives in one
 * workspace (about 1.9 KiB) rather than on the Canon task's stack: dust
 * objects generated once per shot and the stamp's dot map.
 * It is taken from the heap the first time a print effect is on and kept
 * (core memory is full); film_print_begin fills it for the pass that follows. */
#define MAX_SPECKS 36
#define HAIR_SEGS 24
#define MAX_HAIRS 3
#define MAX_SCRATCHES 2
#define SCRATCH_KNOT 96u
#define SCRATCH_KNOTS 40
typedef struct {unsigned short cx,cy,reach;unsigned char r16,r2,o;signed char dx,dy;} speck_t;
typedef struct {int X[HAIR_SEGS+1],Y[HAIR_SEGS+1];unsigned short ylo,yhi;unsigned char hw,o;} hair_t;
typedef struct {unsigned short y,xs,xe;unsigned char hw,o;signed char drift,bow;unsigned char knot[SCRATCH_KNOTS+1];} scratch_t;
typedef struct {
 speck_t speck[MAX_SPECKS];
 hair_t hair[MAX_HAIRS];
 scratch_t scratch[MAX_SCRATCHES];
 unsigned specks,hairs,scratches;
 unsigned dots[7][(FILM_PRINT_TEXT*6+31)/32];     /* lit stamp dots, one bit per dot column */
 unsigned char bump[257];                          /* (1-t^2)^2 glow profile, t in 1/256 of the glow radius, 0..255 */
 unsigned short column[FILM_PRINT_TEXT*6+4];       /* per dot column: vertical glow of its lit dots for the current row */
 unsigned column_row;                              /* row the column sums are for (+1) */
} print_work;
static print_work *workspace;
#define work (*workspace)
static int sb(unsigned char b){return b<128?b:(int)b-256;}

static void make_dust(const film_print *p);
int film_print_begin(film_print *p,unsigned width,unsigned height,const film_print_settings *s,unsigned seed,int mono) {
 unsigned i,len=0;
 if(!p||!width||!height||width%2||width>FILM_MAX_WIDTH)return -1;
 for(i=0;i<sizeof(*p);i++)((unsigned char*)p)[i]=0;
 if(!s)return 0;
 if(s->dust>3||s->stamp>4)return -1;
 if(s->dust||s->stamp) {
  if(!workspace)workspace=malloc(sizeof(print_work));
  if(!workspace)return -1;
 }
 p->width=width;p->height=height;p->mono=mono!=0;
 p->dust=s->dust;p->dust_dark=s->dust_dark!=0;p->seed=seed^0x5d1f2e3cu;
 p->scale=(height*256u+REF_HEIGHT/2)/REF_HEIGHT;if(!p->scale)p->scale=1;
 while(len<FILM_PRINT_TEXT&&s->text[len])len++;
 /* Trailing spaces would only move the stamp. */
 while(len&&s->text[len-1]==' ')len--;
 if(s->stamp&&len) {
  unsigned pitch=(height*14u+REF_HEIGHT/2)/REF_HEIGHT,mx=width/16u,my=height/14u,w,h,g;
  if(pitch<2)pitch=2;
  if(len*6u*pitch-pitch>width-2u*mx)pitch=(width-2u*mx+1u)/(len*6u);
  if(pitch>=2) {
   for(i=0;i<len;i++){unsigned char c=(unsigned char)s->text[i];p->glyph[i]=(unsigned char)(c>=32&&c<=126?c-32:'?'-32);}
   p->stamp=s->stamp;p->len=len;p->pitch=pitch;
   w=len*6u*pitch-pitch;h=7u*pitch;g=(5u*pitch+1u)/2u;
   p->tx=(int)(s->stamp==FILM_STAMP_BOTTOM_RIGHT||s->stamp==FILM_STAMP_TOP_RIGHT?width-mx-w:mx);
   p->ty=(int)(s->stamp<=FILM_STAMP_BOTTOM_LEFT?height-my-h:my);
   p->x0=(unsigned)p->tx>g?(unsigned)p->tx-g:0;p->x1=p->tx+w+g<width?p->tx+w+g:width;
   p->y0=(unsigned)p->ty>g?(unsigned)p->ty-g:0;p->y1=p->ty+h+g<height?p->ty+h+g:height;
   p->radius16=pitch*27u/4u;                          /* dot radius 0.42 pitch, Q4 */
   p->glow_inv=(256u<<16)/(40u*pitch);                /* |offset| (Q4) to 1/256 of the glow radius, Q16 */
  }
 }
 if(p->dust)make_dust(p);
 if(p->stamp) {
  unsigned c,i2,col;
  for(i2=0;i2<7;i2++)for(c=0;c<(FILM_PRINT_TEXT*6u+31u)/32u;c++)work.dots[i2][c]=0;
  for(c=0;c<p->len;c++)for(i2=0;i2<7;i2++)for(col=0;col<5;col++)
   if(film_font5x7[p->glyph[c]][i2]>>(4u-col)&1u){unsigned j=c*6u+col;work.dots[i2][j>>5]|=1u<<(j&31u);}
  for(c=0;c<=256u;c++){unsigned u=65536u-c*c;work.bump[c]=(unsigned char)((unsigned long long)u*u*255u>>32);} /* (1-t^2)^2, 0..255 */
  work.column_row=0;
 }
 return p->dust||p->stamp?1:0;
}

/* Mark pixel x of the span with dust coverage k (Q8): lighten or darken luma
 * and drain colour from its pair. */
static void dust_pixel(const film_print *p,unsigned char *px,unsigned xr,unsigned k) {
 unsigned char *q=px+(xr&~1u)*2u;int y=px[2*xr+1];
 if(k>256u)k=256u;
 if(p->dust_dark)y-=(int)((unsigned)y*k*220u>>16);
 else if(y<245)y+=(int)((245u-(unsigned)y)*k>>8);
 px[2*xr+1]=(unsigned char)y;
 q[0]=(unsigned char)(sb(q[0])*(int)(256u-k/2u)/256);q[2]=(unsigned char)(sb(q[2])*(int)(256u-k/2u)/256);
}
/* Antialiased coverage (Q8) of a shape edge at distance d16 from a centre
 * line or point with half-width / radius r16 (both Q4). */
static unsigned edge_cover(int r16,int d16){int c=(r16-d16+8)*16;return c<0?0:c>256?256u:(unsigned)c;}
/* The same from a squared distance: the root is needed only in the 1 px
 * antialiased band; inside is full, outside empty. */
static unsigned cover2(int r16,unsigned d2) {
 int in=r16-8,out=r16+8;
 if(in>0&&d2<=(unsigned)(in*in))return 256u;
 if(d2>=(unsigned)(out*out))return 0;
 return edge_cover(r16,(int)isqrt32(d2));
}

static void make_dust(const film_print *p) {
 unsigned i,k;
 work.specks=specks_n[p->dust];work.hairs=hairs_n[p->dust];work.scratches=scratches_n[p->dust];
 for(i=0;i<work.specks;i++) {
  speck_t *o=&work.speck[i];unsigned r16=(24u+rnd(p,i,2)%56u)*p->scale>>8,d=rnd(p,i,3);
  if(r16<10)r16=10;
  if(r16>255)r16=255;
  o->cx=(unsigned short)(rnd(p,i,0)%p->width);o->cy=(unsigned short)(rnd(p,i,1)%p->height);
  o->r16=(unsigned char)r16;o->r2=(unsigned char)(r16*3u/5u);o->o=(unsigned char)(140u+rnd(p,i,4)%116u);
  o->dx=(signed char)(cos64[d&63]*(int)r16*3/1024);o->dy=(signed char)(cos64[(d+16)&63]*(int)r16*3/1024);
  o->reach=(unsigned short)((2u*r16+32u)>>4);
 }
 for(i=0;i<work.hairs;i++) {
  hair_t *o=&work.hair[i];unsigned h=100u+i,len=(60u+rnd(p,h,2)%180u)*p->scale>>8,a=rnd(p,h,3),curl=rnd(p,h,4),hw=(10u+rnd(p,h,5)%8u)*p->scale>>8;
  int seg16,bend=(int)(curl%9u)-4,lo,hi;
  if(len<8)len=8;
  if(hw<6)hw=6;
  if(hw>255)hw=255;
  seg16=(int)(len*16u/HAIR_SEGS);
  o->X[0]=(int)(rnd(p,h,0)%p->width)*16;o->Y[0]=(int)(rnd(p,h,1)%p->height)*16;lo=hi=o->Y[0];
  a<<=4;                                              /* direction in 1/1024 turn */
  for(k=0;k<HAIR_SEGS;k++) {
   a+=(unsigned)(bend*3)+(rnd(p,h,10u+k)%9u)-4u;       /* a gentle irregular curl, no corners */
   o->X[k+1]=o->X[k]+cos1024(a)*seg16/256;o->Y[k+1]=o->Y[k]+cos1024(a+768u)*seg16/256;
   if(o->Y[k+1]<lo)lo=o->Y[k+1];
   if(o->Y[k+1]>hi)hi=o->Y[k+1];
  }
  lo=(lo-(int)hw-16)>>4;hi=(hi+(int)hw+16)>>4;
  o->ylo=(unsigned short)(lo<0?0:lo);o->yhi=(unsigned short)(hi<0?0:hi);
  o->hw=(unsigned char)hw;o->o=(unsigned char)(170u+rnd(p,h,6)%60u);
 }
 for(i=0;i<work.scratches;i++) {
  scratch_t *o=&work.scratch[i];unsigned s=200u+i,xs=rnd(p,s,1)%(p->width/2u),len=p->width*(25u+rnd(p,s,2)%50u)/100u,hw=(4u+rnd(p,s,3)%3u)*p->scale>>8;
  if(hw<4)hw=4;
  if(hw>255)hw=255;
  o->y=(unsigned short)(rnd(p,s,0)%p->height);o->xs=(unsigned short)xs;o->xe=(unsigned short)(xs+len<p->width?xs+len:p->width-1u);
  o->hw=(unsigned char)hw;o->o=(unsigned char)(50u+rnd(p,s,4)%50u);
  o->drift=(signed char)((int)(rnd(p,s,5)%97u)-48);o->bow=(signed char)((int)(rnd(p,s,6)%65u)-32);
  /* Strength knots every 96 px; about a third break the line. */
  for(k=0;k<=SCRATCH_KNOTS;k++){unsigned v=rnd(p,s,16u+k)&255u;o->knot[k]=(unsigned char)(v<90u?0:v);}
 }
}

static void specks(const film_print *p,unsigned row,unsigned char *px,unsigned first,unsigned end) {
 unsigned i,x,xa,xb;
 for(i=0;i<work.specks;i++) {
  const speck_t *o=&work.speck[i];
  if(row+o->reach<o->cy||row>(unsigned)o->cy+o->reach)continue;
  xa=o->cx>o->reach?o->cx-o->reach:0;xb=(unsigned)o->cx+o->reach+1u;
  if(xb>p->width)xb=p->width;
  if(xa<first)xa=first;
  if(xb>end)xb=end;
  if(xa>=xb)continue;
  int cx16=(int)o->cx*16+8,py=(int)row*16+8-((int)o->cy*16+8);
  for(x=xa;x<xb;x++) {
   int pxx=(int)x*16+8-cx16;
   unsigned c1=cover2(o->r16,(unsigned)(pxx*pxx+py*py));
   unsigned c2=cover2(o->r2,(unsigned)((pxx-o->dx)*(pxx-o->dx)+(py-o->dy)*(py-o->dy)));
   unsigned c=c1>c2?c1:c2;
   if(c)dust_pixel(p,px,x-first,c*o->o>>8);
  }
 }
}

static void hairs(const film_print *p,unsigned row,unsigned char *px,unsigned first,unsigned end) {
 unsigned i;int k;
 for(i=0;i<work.hairs;i++) {
  const hair_t *o=&work.hair[i];
  int lo=1<<30,hi=-(1<<30),py=(int)row*16+8,m=(int)o->hw+16,seg[HAIR_SEGS],n=0,q;
  if(row<o->ylo||row>o->yhi)continue;
  /* Segments whose band includes this row, and the columns they can touch. */
  for(k=0;k<HAIR_SEGS;k++) {
   int ya=o->Y[k]<o->Y[k+1]?o->Y[k]:o->Y[k+1],yb=o->Y[k]<o->Y[k+1]?o->Y[k+1]:o->Y[k];
   if(py<ya-m||py>yb+m)continue;
   seg[n++]=k;
   int xa=(o->X[k]<o->X[k+1]?o->X[k]:o->X[k+1])-m,xb=(o->X[k]<o->X[k+1]?o->X[k+1]:o->X[k])+m;
   if(xa<lo)lo=xa;
   if(xb>hi)hi=xb;
  }
  if(lo>hi)continue;
  lo=lo<0?0:lo>>4;hi=(hi>>4)+1;
  if(lo<(int)first)lo=(int)first;
  if(hi>(int)end)hi=(int)end;
  for(int x=lo;x<hi;x++) {
   int pxx=x*16+8;unsigned best=~0u,along=0;
   for(q=0;q<n;q++) {
    k=seg[q];
    int dx=o->X[k+1]-o->X[k],dy=o->Y[k+1]-o->Y[k],qx=pxx-o->X[k],qy=py-o->Y[k],l2=dx*dx+dy*dy,t;
    t=l2?(qx*dx+qy*dy)*256/l2:0;
    t=t<0?0:t>256?256:t;
    int ex=qx-dx*t/256,ey=qy-dy*t/256;unsigned d2=(unsigned)(ex*ex+ey*ey);
    if(d2<best){best=d2;along=(unsigned)k*256u+(unsigned)t;}
   }
   /* Slightly out of focus: a soft 1.25 px edge. Fades over the first and
    * last quarter of the length. */
   int soft=20,lim=(int)o->hw+soft/2;
   if(best>=(unsigned)(lim*lim))continue;
   int cv=((int)o->hw+soft/2-(int)isqrt32(best))*256/soft;
   unsigned c=cv>256?256u:(unsigned)cv,taper;
   along=along*4u/HAIR_SEGS;                          /* 0..1024 over the hair */
   taper=along<256u?along:along>768u?1024u-along:256u;
   c=(c*taper>>8)*o->o>>8;
   if(c)dust_pixel(p,px,(unsigned)x-first,c);
  }
 }
}

/* Scratches: faint, slightly bowed lines whose strength wanders along their
 * length and breaks off in places, like a real emulsion scratch; a straight
 * even line would read as a rendering fault. */
static void scratches(const film_print *p,unsigned row,unsigned char *px,unsigned first,unsigned end) {
 unsigned i;
 for(i=0;i<work.scratches;i++) {
  const scratch_t *o=&work.scratch[i];
  unsigned xs=o->xs,xe=o->xe,x,xa,xb,fade;
  if(xe<=xs+16u||row+6u<o->y||row>o->y+6u)continue;
  xa=xs>first?xs:first;xb=xe<end?xe:end;
  fade=(xe-xs)/6u;if(!fade)fade=1;
  for(x=xa;x<xb;x++) {
   int t=(int)((x-xs)*256u/(xe-xs)),line=(int)o->y*16+8+o->drift*t/256+o->bow*4*t*(256-t)/65536,d=(int)row*16+8-line;
   unsigned c=edge_cover(o->hw,d<0?-d:d),e,kn,f,v;
   if(!c)continue;
   e=x-xs<xe-x?x-xs:xe-x;e=e>=fade?256u:e*256u/fade;  /* fade in and out at the ends */
   kn=(x-xs)/SCRATCH_KNOT;f=(x-xs)%SCRATCH_KNOT;
   if(kn>=SCRATCH_KNOTS){kn=SCRATCH_KNOTS-1;f=SCRATCH_KNOT;}
   v=(o->knot[kn]*(SCRATCH_KNOT-f)+o->knot[kn+1]*f)/SCRATCH_KNOT;
   c=((c*o->o>>8)*e>>8)*v>>8;
   if(c)dust_pixel(p,px,x-first,c);
  }
 }
}

/* Stamp light (Q8). The glow is separable, a horizontal times a vertical
 * (1-t^2)^2 bump out to 2.5 dot pitches: per row, each dot column sums the
 * vertical bumps of its lit dots once, and per pixel the five nearest
 * columns are weighted by the horizontal bump. The bright core is the disc
 * of the pixel's own cell (a dot of radius 0.42 pitch never leaves it). */
#define DOT(i,j) (work.dots[i][(j)>>5]>>((j)&31u)&1u)
static unsigned glow_bump(const film_print *p,int e16) {
 unsigned t=(unsigned)(e16<0?-e16:e16)*p->glow_inv>>16;
 return t>256u?0:work.bump[t];
}
static void stamp(const film_print *p,unsigned row,unsigned char *px,unsigned first,unsigned end) {
 unsigned x,xa=p->x0>first?p->x0:first,xb=p->x1<end?p->x1:end,cols=p->len*6u,j;
 int S16=(int)p->pitch*16,ly16,iy,ey,lx16,jx,ex16,dx,i;
 if(row<p->y0||row>=p->y1||xa>=xb)return;
 ly16=((int)row-p->ty)*16+8;iy=(ly16+3*S16)/S16-3;
 if(work.column_row!=row+1) {
  for(j=0;j<cols;j++) {
   unsigned g=0;
   for(i=iy-2;i<=iy+2;i++)if(i>=0&&i<=6&&DOT(i,j))g+=glow_bump(p,ly16-(i*S16+S16/2));
   work.column[j]=(unsigned short)g;
  }
  work.column_row=row+1;
 }
 /* The core's vertical distance: to the dot row the pixel is in. */
 ey=ly16-(iy*S16+S16/2);
 /* Dot column and offset from its centre, stepped per pixel (no division). */
 lx16=((int)xa-p->tx)*16+8;jx=(lx16+3*S16)/S16-3;ex16=lx16-(jx*S16+S16/2);
 for(x=xa;x<xb;x++,ex16+=16) {
  unsigned glow=0,core=0,a;
  if(ex16>=S16/2){jx++;ex16-=S16;}
  for(dx=-2;dx<=2;dx++) {
   int c=jx+dx;
   if(c<0||(unsigned)c>=cols||!work.column[c])continue;
   glow+=glow_bump(p,ex16-dx*S16)*work.column[c];
  }
  if(iy>=0&&iy<=6&&jx>=0&&(unsigned)jx<cols&&DOT(iy,(unsigned)jx))core=cover2((int)p->radius16,(unsigned)(ex16*ex16+ey*ey));
  glow=(glow>>8)*48u>>8;if(glow>256u)glow=256u;
  a=core+((256u-core)*glow>>8);
  if(!a)continue;
  unsigned xr=x-first;unsigned char *q=px+(xr&~1u)*2u;
  int y=px[2*xr+1]+(int)(a*150u>>8);                   /* orange LED: Y 150, Cb -72, Cr +68 at full light */
  px[2*xr+1]=(unsigned char)(y>255?255:y);
  if(!p->mono){q[0]=(unsigned char)clamp_c(sb(q[0])-(int)(a*36u>>8));q[2]=(unsigned char)clamp_c(sb(q[2])+(int)(a*34u>>8));}
 }
}

void film_print_row(const film_print *p,unsigned row,unsigned char *pixels,unsigned start_col,unsigned span) {
 unsigned end=start_col+span;
 if(!p||row>=p->height||end>p->width||(start_col|span)&1u)return;
 if(p->dust){specks(p,row,pixels,start_col,end);hairs(p,row,pixels,start_col,end);scratches(p,row,pixels,start_col,end);}
 if(p->stamp)stamp(p,row,pixels,start_col,end);
}
