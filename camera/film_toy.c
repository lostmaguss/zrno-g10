/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Independently written toy-camera lens: soft corners and
 * lateral chromatic aberration over signed UYVY422 input rows. Integer only.
 *
 * A grid of (Y, U, V) cell averages (32 or 64 px), smoothed with a [1 2 1]
 * filter in both directions, is built from rows ahead of the current one,
 * while they are still untouched, and kept in a small ring. Each pixel pair
 * outside the sharp centre is blended toward the grid sampled at its own
 * position with a cubic B-spline (softness). Bilinear sampling would leave a
 * slope break at every cell boundary, which the eye sees as straight lines
 * along the grid (Mach bands) across diagonal edges; the B-spline is smooth
 * in value and slope, so there are none.
 *
 * Chromatic aberration is lateral: the red image is slightly magnified, so
 * each pixel takes its red from a point nearer the centre (6, 10 or 14 px at
 * the corners of a G12 frame, in proportion elsewhere). Green and blue stay,
 * so edges get red and cyan fringes. Only red moves because every source
 * point then lies inside the frame; a channel sampled outward would freeze
 * at the frame edge. Red is read at full resolution from the original
 * image: rows at and below the current one are still untouched in the
 * frame, and the red planes of recent rows are cached by row number: the
 * top half reuses its source rows from one row to the next, and the bottom
 * half, whose sources lie above, finds them still original in the cache. Per row, the two source rows are
 * mixed into one line; per pixel the line is read with linear interpolation
 * at the scaled column. Where the pixel is blended into the blur, the red
 * shift is taken on the blurred grid instead (Y and V at the source row and
 * column), so blurred corners carry soft colour like a real soft lens. The
 * red change dR moves luma by 0.299 dR, Cr by 0.5 dR and Cb by -0.169 dR.
 * The aberration covers the whole frame: its shift grows from zero at the
 * centre, as in a real lens, so it needs no fade.
 *
 * Blur samples are taken at knots every 16 px and interpolated linearly for
 * each pixel pair in between; over 16 px the B-spline is nearly straight
 * (under half a level off), so this adds no visible structure. Radial
 * weights, which change very slowly, are taken once per 16 px block
 * (anchored to absolute columns) from interpolated tables. The blend keeps
 * the fraction bits and rounds with a fixed dither, so smooth gradients show
 * no contour steps. Vertical interpolation is done once per row, because the
 * radial scaling is separable. Cells average every other row and pixel pair,
 * plenty for 32-64 px cells. */
#include "film_compact.h"
#include "film_toy.h"
#include <string.h>

/* Per level: sharp centre and full-strength radius as 255*r^2 of the
 * half-diagonal (Low 0.22/0.74, Mid 0.21/0.70, High 0.2/0.65), and blur cell. */
static const unsigned flat_q[4]={0,12,11,10},full_q[4]={0,140,125,110};
static const unsigned cell_shift[4]={0,5,6,6};         /* blur cell 32, 64, 64 px */
static const unsigned soft_max[4]={0,200,230,256};     /* Q8 luma blend at the corners */
static const unsigned ca_px[4]={0,6,10,14};            /* red shift at the corners of a G12 frame */
#define TOY_CA_HD 2280u                                /* G12 half-diagonal the shifts refer to */

#define TOY_BLOCK 8u                                   /* pixel pairs per sample block (16 px) */
/* Uniform cubic B-spline weights (Q12) for the 16 sub-cell positions; taps
 * are grid cells j-1, j, j+1, j+2. */
static const unsigned short bspline[16][4]={{683,2730,683,0},{562,2716,818,0},{457,2671,967,1},{366,2601,1125,4},
 {288,2506,1291,11},{222,2393,1460,21},{167,2262,1631,36},{122,2118,1799,57},{85,1963,1963,85},{57,1799,2118,122},
 {36,1630,2263,167},{21,1460,2393,222},{11,1290,2507,288},{4,1126,2600,366},{1,967,2671,457},{0,819,2715,562}};
static size_t toy_layout(unsigned width,size_t blocks,size_t gw) {
 return 2u*256u+4u*(blocks+1u)+10u*(gw+3u)+6u*FILM_TOY_RING*gw+256u+2u*blocks
  +(size_t)FILM_TOY_CA_ROWS*width+width+1u+616u+16u;
}
size_t film_toy_work_size(unsigned width) {
 size_t pairs=width/2,blocks=(pairs+TOY_BLOCK-1)/TOY_BLOCK,gw=(width+FILM_TOY_MIN_CELL-1)/FILM_TOY_MIN_CELL;
 if(!width||width>3744||width%2)return 0;
 return toy_layout(width,blocks,gw);
}
/* Frame scale (film_toy_scale), Q16: 65536 is L. */
static unsigned toy_scale=65536u;
void film_toy_scale(unsigned scale){toy_scale=scale>=65536u||!scale?65536u:scale;}
static unsigned isqrt64(unsigned long long v) {
 unsigned long long r=0,bit=1ull<<62;
 while(bit>v)bit>>=2;
 while(bit){if(v>=r+bit){v-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}
 return (unsigned)r;
}
/* Grid coordinate in 1/16 cell for a pixel-edge coordinate p16 (1/16 px) scaled
 * by `scale` (Q16) about the centre c16, clamped to the grid. shift: log2 of
 * the cell, or 0 for a scaled cell that is not a power of two. */
static unsigned grid_pos(long long p16,long long c16,unsigned scale,unsigned cells,unsigned cell,unsigned shift) {
 long long x=c16+(((p16-c16)*(long long)scale)>>16);
 long long d=x-8ll*cell,g;                           /* (x - half cell) / cell, Q4 */
 if(shift)g=d>>shift;
 else g=d>=0?d/(long long)cell:-((-d+(long long)cell-1)/(long long)cell);
 long long top=((long long)cells-1)*16;
 return (unsigned)(g<0?0:g>top?top:g);
}
/* Source position (Q16, in pixel or row indices) of index i along an axis of
 * n samples, scaled by s (Q16) about the axis centre: n/2 + (i+0.5-n/2)*s - 0.5. */
static unsigned long long scaled_q16(unsigned n,unsigned long long i,unsigned s) {
 unsigned long long h=(unsigned long long)n*32768u;
 return h-((h*s)>>16)+(s>>1)-32768u+i*s;
}
FILM_COMPACT int film_toy_begin(film_toy *t,const unsigned char *image,unsigned stride,unsigned width,
 unsigned height,unsigned level,void *work,size_t work_size) {
 size_t need=film_toy_work_size(width);
 unsigned i,c,hd,block;
 unsigned char *p;
 if(!t||!need||!height||level>3)return -1;
 memset(t,0,sizeof(*t));
 if(!level)return 0;
 if(!image||stride<width*2u||!work||work_size<need)return -1;
 t->image=image;t->width=width;t->height=height;t->stride=stride;t->level=level;
 t->shift=cell_shift[level];t->cell=1u<<t->shift;t->bshift=3u;     /* TOY_BLOCK pairs per block */
 if(toy_scale!=65536u) {
  /* A smaller frame stands for L shrunk: the cell shrinks with it (even, any
   * size), and the knots stay at least two per cell (a block of a power of
   * two pairs, at most TOY_BLOCK, no wider than a quarter cell). */
  unsigned cell=((t->cell*toy_scale+65536u)>>17)*2u;
  if(cell<2u)cell=2u;
  t->cell=cell;t->shift=0;
  for(i=1;i<16;i++)if(cell==1u<<i)t->shift=i;
  while(t->bshift&&(4u<<t->bshift)>cell)t->bshift--;
 }
 block=1u<<t->bshift;
 t->pairs=width/2;t->blocks=(t->pairs+block-1)/block;t->gw=(width+t->cell-1)/t->cell;t->gh=(height+t->cell-1)/t->cell;
 if(toy_scale!=65536u){need=toy_layout(width,t->blocks,t->gw);if(work_size<need)return -1;}
 t->aligned=!(((size_t)image|stride)&3u);
 t->diag2=(unsigned long long)width*width/4u+(unsigned long long)height*height/4u;
 hd=isqrt64(t->diag2);if(!hd)hd=1;
 /* Red magnified by ca/2280 of the half-diagonal: sources scale by s < 1. */
 t->ca_scale=(unsigned)((65536ull*TOY_CA_HD+(TOY_CA_HD+ca_px[level])/2)/(TOY_CA_HD+ca_px[level]));
 t->ca_base=(unsigned)scaled_q16(width,0,t->ca_scale);
 /* The lowest row's source lies up to (height/2)*(1-s) rows above it: the
  * cache must hold those rows and the row itself (10 for a G12 at High). */
 if(((unsigned long long)height*(65536u-t->ca_scale)>>17)+2u>FILM_TOY_CA_ROWS)return -1;
 for(i=0;i<FILM_TOY_CA_ROWS;i++)t->ca_tag[i]=~0u;
 p=(unsigned char*)(((size_t)work+3u)&~(size_t)3u);
 t->soft=(unsigned short*)p;p+=256u*2u;
 t->xs=(unsigned short*)p;p+=4u*(t->blocks+1u);
 t->lines=(short*)p;p+=10u*(t->gw+3u);
 t->raw_y=p;p+=FILM_TOY_RING*t->gw;
 t->raw_u=(signed char*)p;p+=FILM_TOY_RING*t->gw;
 t->raw_v=(signed char*)p;p+=FILM_TOY_RING*t->gw;
 t->ring_y=p;p+=FILM_TOY_RING*t->gw;
 t->ring_u=(signed char*)p;p+=FILM_TOY_RING*t->gw;
 t->ring_v=(signed char*)p;p+=FILM_TOY_RING*t->gw;
 t->dither=p;p+=256u;
 {
  unsigned r=0x2545f491u;
  for(i=0;i<256;i++){r^=r<<13;r^=r>>17;r^=r<<5;t->dither[i]=(unsigned char)(r>>24);}
 }
 p=(unsigned char*)(((size_t)p+3u)&~(size_t)3u);  /* word-aligned for prepare_red */
 t->ca_line=p;p+=(width+4u)&~3u;
 t->ca_ring=p;p+=(size_t)FILM_TOY_CA_ROWS*width;
 for(i=0;i<616u;i++)p[i]=(unsigned char)(i<180u?0:i>435u?255:i-180u);
 t->ca_clamp=p+180;p+=616u;
 t->nx2=(unsigned short*)(((size_t)p+1u)&~(size_t)1u);
 for(i=0;i<256;i++) {
  unsigned f0=flat_q[level],f1=full_q[level];
  unsigned s=i<=f0?0:i>=f1?4096u:(i-f0)*4096u/(f1-f0);
  unsigned smooth=(unsigned)((unsigned long long)s*s*(3u*4096u-2u*s)>>24);    /* Q12 */
  t->soft[i]=(unsigned short)((soft_max[level]*smooth+2048u)>>12);
 }
 for(c=0;c<t->blocks;c++) {
  /* Block c covers 2*block pixels from pixel edge 2*block*c; its
   * centre in half-pixel units, and 255*dx^2/diag2 there in Q8. */
  long long dx=(long long)(4u*block*c+2u*block)-(long long)width;
  t->nx2[c]=(unsigned short)((unsigned long long)(dx*dx)*255u*256u/(4u*t->diag2));
 }
 for(c=0;c<=t->blocks;c++) {
  /* Knot c sits on pixel edge 2*block*c, the start of block c. */
  long long p16=(long long)(2u*block*c)*16,c16=(long long)width*8;
  t->xs[2*c]=(unsigned short)grid_pos(p16,c16,65536u,t->gw,t->cell,t->shift);
  t->xs[2*c+1]=(unsigned short)grid_pos(p16,c16,t->ca_scale,t->gw,t->cell,t->shift);
 }
 return 1;
}
static int toy_signed(unsigned char b){return b<128?b:(int)b-256;}
/* Average one raw grid row of cells from untouched frame rows, then smooth
 * it horizontally with [1 2 1]. */
static void build_row(film_toy *t,unsigned g) {
 unsigned slot=g%FILM_TOY_RING,i,y,y0=g*t->cell,y1=y0+t->cell;
 unsigned char *ry=t->raw_y+slot*t->gw;signed char *ru=t->raw_u+slot*t->gw,*rv=t->raw_v+slot*t->gw;
 if(y1>t->height)y1=t->height;
 for(i=0;i<t->gw;i++) {
  unsigned c0=i*(t->cell/2),c1=c0+t->cell/2,c,n=0;int sy=0,su=0,sv=0;
  if(c1>t->pairs)c1=t->pairs;
  for(y=y0;y<y1;y++) {
   const unsigned char *row=t->image+(size_t)y*t->stride;
   if((y-y0)&1u)continue;                            /* every other row */
   for(c=c0;c<c1;c+=2,n++) {                          /* and every other pair */
    if(t->aligned) {
     unsigned w=((const unsigned *)row)[c];
     sy+=(int)(((w>>8)&255u)+(w>>24));su+=(signed char)(w&255u);sv+=(signed char)((w>>16)&255u);
    } else {
     const unsigned char *q=row+4u*c;
     sy+=q[1]+q[3];su+=toy_signed(q[0]);sv+=toy_signed(q[2]);
    }
   }
  }
  ry[i]=(unsigned char)((sy+(int)n)/(int)(2u*n));
  ru[i]=(signed char)(su>=0?(su+(int)n/2)/(int)n:-((-su+(int)n/2)/(int)n));
  rv[i]=(signed char)(sv>=0?(sv+(int)n/2)/(int)n:-((-sv+(int)n/2)/(int)n));
 }
 {
  int py=ry[0],pu=ru[0],pv=rv[0];
  for(i=0;i<t->gw;i++) {
   unsigned nx=i+1<t->gw?i+1:i;
   int cy=ry[i],cu=ru[i],cv=rv[i];
   ry[i]=(unsigned char)((py+2*cy+ry[nx]+2)>>2);
   ru[i]=(signed char)((pu+2*cu+ru[nx]+2)>>2);
   rv[i]=(signed char)((pv+2*cv+rv[nx]+2)>>2);
   py=cy;pu=cu;pv=cv;
  }
 }
}
/* Smoothed row s = [1 2 1] over raw rows s-1, s, s+1 (clamped at the edges). */
static void smooth_row(film_toy *t,unsigned s) {
 unsigned a=(s?s-1:0)%FILM_TOY_RING,b=s%FILM_TOY_RING,c=(s+1<t->gh?s+1:s)%FILM_TOY_RING,d=s%FILM_TOY_RING,i,gw=t->gw;
 for(i=0;i<gw;i++) {
  t->ring_y[d*gw+i]=(unsigned char)((t->raw_y[a*gw+i]+2*t->raw_y[b*gw+i]+t->raw_y[c*gw+i]+2)>>2);
  t->ring_u[d*gw+i]=(signed char)((t->raw_u[a*gw+i]+2*t->raw_u[b*gw+i]+t->raw_u[c*gw+i]+2)>>2);
  t->ring_v[d*gw+i]=(signed char)((t->raw_v[a*gw+i]+2*t->raw_v[b*gw+i]+t->raw_v[c*gw+i]+2)>>2);
 }
}
/* Grid ring offsets of the four B-spline taps around grid row g16>>4,
 * clamped to the grid and to the rows held in the ring. */
static void spline_rows(unsigned g16,unsigned rows,unsigned done,unsigned ring,unsigned width,unsigned out[4]) {
 unsigned k,lo=done>ring-1?done-(ring-1):0,hi=done?done-1:0;
 int j=(int)(g16>>4)-1;
 for(k=0;k<4;k++,j++) {
  int r=j<0?0:j>(int)rows-1?(int)rows-1:j;
  if(r<(int)lo)r=(int)lo;
  if(r>(int)hi)r=(int)hi;
  out[k]=((unsigned)r%ring)*width;
 }
}
/* Vertical B-spline of one blur channel at grid row position g16 into a line
 * of gw+3 values (x16): one repeated value before the grid and two after, so
 * the horizontal taps need no bounds checks. */
static void make_line(const film_toy *t,unsigned g16,int channel,short *line) {
 unsigned r[4],i,gw=t->gw;
 const unsigned short *w=bspline[g16&15u];
 unsigned w0=w[0],w1=w[1],w2=w[2],w3=w[3];
 spline_rows(g16,t->gh,t->smoothed,FILM_TOY_RING,gw,r);
 short *out=line+1;
 if(channel==0) {
  const unsigned char *g=t->ring_y;
  for(i=0;i<gw;i++)out[i]=(short)((w0*g[r[0]+i]+w1*g[r[1]+i]+w2*g[r[2]+i]+w3*g[r[3]+i]+128u)>>8);
 } else {
  const signed char *g=channel==1?t->ring_u:t->ring_v;
  for(i=0;i<gw;i++)out[i]=(short)(((int)w0*g[r[0]+i]+(int)w1*g[r[1]+i]+(int)w2*g[r[2]+i]+(int)w3*g[r[3]+i]+128)>>8);
 }
 line[0]=out[0];out[gw]=out[gw+1]=out[gw-1];
}
/* Horizontal B-spline of a padded line at x16, scaled x256 (lines hold x16
 * values; the weights add x4096, so drop x256). */
static inline int spline_raw(const short *restrict line,unsigned x16) {
 const short *l=line+(x16>>4);
 const unsigned short *w=bspline[x16&15u];
 return ((int)w[0]*l[0]+(int)w[1]*l[1]+(int)w[2]*l[2]+(int)w[3]*l[3]+128)>>8;
}
/* Red plane (Y + 1.402 V, clamped) of original frame row y. */
static void red_row(const film_toy *t,unsigned y,unsigned char *dst) {
 const unsigned char *row=t->image+(size_t)y*t->stride,*cl=t->ca_clamp;
 unsigned c,pairs=t->pairs;   /* locals: the stores below could alias *t */
 if(t->aligned) {
  const unsigned *w=(const unsigned *)row;
  for(c=0;c<pairs;c++) {
   unsigned x=w[c];int v=(signed char)((x>>16)&255u);
   v=(359*v+128)>>8;
   dst[2*c]=cl[(int)((x>>8)&255u)+v];dst[2*c+1]=cl[(int)(x>>24)+v];
  }
 } else for(c=0;c<pairs;c++) {
  const unsigned char *q=row+4u*c;int v=toy_signed(q[2]);
  v=(359*v+128)>>8;
  dst[2*c]=cl[q[1]+v];dst[2*c+1]=cl[q[3]+v];
 }
}
/* Cached red plane of frame row y. Rows not cached are read from the frame,
 * which holds them unchanged: they are the current row or below, or rows
 * the caller skipped. Rows above that the bottom half needs (from the centre
 * row down) were cached when processed and are not evicted before use. */
static const unsigned char *red_plane(film_toy *t,unsigned y) {
 unsigned slot=y%FILM_TOY_CA_ROWS;unsigned char *dst=t->ca_ring+(size_t)slot*t->width;
 if(t->ca_tag[slot]!=y){red_row(t,y,dst);t->ca_tag[slot]=y;}
 return dst;
}
/* The red line for `row`: the red plane at its source row, mixed from the
 * two nearest rows. */
static void prepare_red(film_toy *t,unsigned row) {
 unsigned long long sy=scaled_q16(t->height,row,t->ca_scale);
 unsigned y0=(unsigned)(sy>>16),fy=(unsigned)(sy>>8)&255u,y1=y0+1u<t->height?y0+1u:y0,x,w=t->width;
 unsigned char *line=t->ca_line;
 const unsigned char *a=red_plane(t,y0);
 if(fy&&y1!=y0) {
  const unsigned char *b=red_plane(t,y1);
  x=0;
  if(!(((size_t)a|(size_t)b|(size_t)line)&3u)) {
   /* Two pixels per 16-bit lane pair: each lane is at most 255*256+128, so
    * one multiply mixes two bytes with no carry between them. */
   const unsigned *wa=(const unsigned*)a,*wb=(const unsigned*)b;unsigned *wl=(unsigned*)line,fa=256u-fy;
   for(;x+4<=w;x+=4,wa++,wb++,wl++) {
    unsigned A=*wa,B=*wb;
    unsigned lo=(A&0x00ff00ffu)*fa+(B&0x00ff00ffu)*fy+0x00800080u;
    unsigned hi=((A>>8)&0x00ff00ffu)*fa+((B>>8)&0x00ff00ffu)*fy+0x00800080u;
    *wl=((lo>>8)&0x00ff00ffu)|(hi&0xff00ff00u);
   }
  }
  for(;x<w;x++)line[x]=(unsigned char)((a[x]*(256u-fy)+b[x]*fy+128u)>>8);
 } else memcpy(line,a,w);
 line[w]=line[w-1];
 /* The bottom half reads rows from the centre down after changing them. */
 if(2u*row+2u>=t->height)red_plane(t,row);
}
/* Per-row set-up, done once even when a row is applied in several spans:
 * build the grid rows this row samples (from untouched frame rows at or
 * below it), interpolate the sample lines vertically and make the red line. */
static void prepare_row(film_toy *t,unsigned row) {
 unsigned c,need,g16[2],gw=t->gw;
 long long p16=(long long)row*16+8,c16=(long long)t->height*8;
 g16[0]=grid_pos(p16,c16,65536u,t->gh,t->cell,t->shift);
 g16[1]=grid_pos(p16,c16,t->ca_scale,t->gh,t->cell,t->shift);
 need=0;
 for(c=0;c<2;c++){unsigned j=(g16[c]>>4)+2u;if(j>need)need=j;}  /* last B-spline tap */
 if(need>=t->gh)need=t->gh-1;
 /* Smoothed row s needs raw rows up to s+1. The raw ring keeps the newest
  * FILM_TOY_RING rows, enough for the row before s while smoothing it. */
 while(t->smoothed<=need) {
  unsigned want=t->smoothed+1<t->gh?t->smoothed+1:t->gh-1;
  if(t->built<=want)build_row(t,t->built++);
  else smooth_row(t,t->smoothed++);
 }
 make_line(t,g16[0],0,t->lines);
 make_line(t,g16[0],1,t->lines+(gw+3));
 make_line(t,g16[0],2,t->lines+2*(gw+3));
 make_line(t,g16[1],0,t->lines+3*(gw+3));
 make_line(t,g16[1],2,t->lines+4*(gw+3));
 prepare_red(t,row);
 long long dy=2ll*row+1ll-(long long)t->height;
 t->ny2=(unsigned)((unsigned long long)(dy*dy)*255u*256u/(4u*t->diag2));
 t->line_row=row+1;
}
/* Raw values at knot n: blurred Y, U, V scaled x256, and the blurred red
 * change (x256) from moving to the red source position. */
#define TOY_KNOT 4
static void knot_raw(const film_toy *t,unsigned n,int out[TOY_KNOT]) {
 unsigned gw=t->gw+3;
 const short *ly=t->lines,*lu=ly+gw,*lv=lu+gw,*lys=lv+gw,*lvs=lys+gw;
 const unsigned short *x=t->xs+2*n;
 out[0]=spline_raw(ly,x[0]);out[1]=spline_raw(lu,x[0]);out[2]=spline_raw(lv,x[0]);
 out[3]=spline_raw(lys,x[1])-out[0]+((359*(spline_raw(lvs,x[1])-out[2])+128)>>8);
}
/* The pixel pairs [c, end) of one block with constant blur weight ws: the
 * same arithmetic for every pair as described in film_toy_row. blurred is a
 * constant (0: the sharp centre, ws = 0, where every blur term is zero), so
 * each variant keeps its values in registers. */
static inline __attribute__((always_inline)) void toy_pairs(unsigned char *restrict p,unsigned c,unsigned end,
 int ws,int v0,int v1,int v2,int v3,int s0,int s1,int s2,int s3,unsigned sx,unsigned scale,unsigned rh,
 const unsigned char *red,const unsigned char *cl,const unsigned char *dither,const int blurred) {
 const int keep=(256-ws)*8;
 unsigned char *const stop=p+4u*(end-c);unsigned h2=c<<1;   /* the pair index only feeds the dither */
 for(;p<stop;p+=4,h2+=2u,sx+=2u*scale) {
  int y0=p[1],y1=p[3],u=toy_signed(p[0]),v=toy_signed(p[2]);
  int dr0,dr1;
  {
   unsigned i=sx>>16,f=(sx>>8)&255u,sx1=sx+scale;
   int rv=(359*v+128)>>8,blur=blurred?(ws*v3)>>8:0;
   int r=red[i]+((((int)red[i+1]-(int)red[i])*(int)f+128)>>8);
   dr0=(r-(int)cl[y0+rv])*(blurred?keep:2048)+blur;
   i=sx1>>16;f=(sx1>>8)&255u;
   r=red[i]+((((int)red[i+1]-(int)red[i])*(int)f+128)>>8);
   dr1=(r-(int)cl[y1+rv])*(blurred?keep:2048)+blur;
  }
  unsigned h=h2^rh;
  int d0=(int)dither[h&255u]<<11,d1=(int)dither[(h^1u)&255u]<<11,dp=(dr0+dr1)>>1;
  if(blurred) {
   y0+=((v0-y0*2048)*ws+77*dr0+d0)>>19;
   y1+=((v0-y1*2048)*ws+77*dr1+d1)>>19;
   u+=((v1-u*2048)*ws-43*dp+(d0^0x40000))>>19;
   v+=((v2-v*2048)*ws+128*dp+(d1^0x40000))>>19;
   v0+=s0;v1+=s1;v2+=s2;v3+=s3;
  } else {
   y0+=(77*dr0+d0)>>19;
   y1+=(77*dr1+d1)>>19;
   u+=(-43*dp+(d0^0x40000))>>19;
   v+=(128*dp+(d1^0x40000))>>19;
  }
  if((unsigned)y0>255u)y0=y0<0?0:255;
  if((unsigned)y1>255u)y1=y1<0?0:255;
  if((unsigned)(u+128)>255u)u=u<0?-128:127;
  if((unsigned)(v+128)>255u)v=v<0?-128:127;
  p[1]=(unsigned char)y0;p[3]=(unsigned char)y1;p[0]=(unsigned char)u;p[2]=(unsigned char)v;
 }
}
void film_toy_row(film_toy *t,unsigned row,unsigned char *pixels,unsigned start_col,unsigned span) {
 unsigned first=start_col/2,end=first+span/2,c;
 if(!t||!t->level||row>=t->height||end>t->pairs)return;
 if(t->line_row!=row+1)prepare_row(t,row);
 unsigned ny2=t->ny2,flat=flat_q[t->level],scale=t->ca_scale,base=t->ca_base;
 const unsigned short *nx2=t->nx2;const unsigned char *dither=t->dither,*red=t->ca_line,*cl=t->ca_clamp;
 const unsigned short *soft=t->soft;
 unsigned rh=(t->dither[row&255u]+97u*(row>>8))&255u;  /* per-row dither permutation */
 const unsigned bs=t->bshift,bm=(1u<<bs)-1u,step=8u>>bs;  /* TOY_BLOCK pairs per block at L */
 unsigned char *p=pixels;
 unsigned cached=~0u;
 /* Running values (x2048) for blurred Y, U, V and the blurred red change,
  * stepped once per pixel pair across the block from its two knots. The raw
  * end knot is kept for the next block; each block applies its own
  * weights, so spans give identical results. One block per outer step. */
 int K[2][TOY_KNOT];
 for(c=first;c<end;) {
  unsigned b=c>>bs,k=c&bm,stop=(b+1u)<<bs;int ws;
  if(stop>end)stop=end;
  /* Radius in Q8, the weight tables interpolated between whole steps. */
  unsigned q=nx2[b]+ny2,f;
  if(q>255u*256u)q=255u*256u;
  f=q&255u;q>>=8;
  if(q<=flat)ws=0;
  else{unsigned n=q<255u?q+1u:q;ws=soft[q]+((((int)soft[n]-(int)soft[q])*(int)f+128)>>8);}
  unsigned sx=base+2u*c*scale;
  if(!ws) {
   /* Sharp centre: no blur, so only the aberration applies. */
   cached=~0u;
   toy_pairs(p,c,stop,0,0,0,0,0,0,0,0,0,sx,scale,rh,red,cl,dither,0);
  } else {
   /* Knot n lives in slot n&1, so the end knot of one block is the start
    * knot of the next without copying. */
   int *S=K[b&1u],*E=K[(b+1u)&1u];
   if(cached!=b)knot_raw(t,b,S);
   knot_raw(t,b+1,E);cached=b+1;
   /* Pair centres sit at (2k+1)/(2*block) of the block: values are x256
    * and running values x2048, so a step is (E-S)*8/block, starting half
    * a step in. */
   int s0=(E[0]-S[0])*(int)step,s1=(E[1]-S[1])*(int)step,s2=(E[2]-S[2])*(int)step,s3=(E[3]-S[3])*(int)step;
   int v0=S[0]*8+s0/2+s0*(int)k,v1=S[1]*8+s1/2+s1*(int)k,v2=S[2]*8+s2/2+s2*(int)k,v3=S[3]*8+s3/2+s3*(int)k;
   toy_pairs(p,c,stop,ws,v0,v1,v2,v3,s0,s1,s2,s3,sx,scale,rh,red,cl,dither,1);
  }
  p+=4u*(stop-c);c=stop;
 }
}
