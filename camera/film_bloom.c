/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Independently written neutral bloom approximation.
 * Soft highlight extraction on a 4x4 grid, separable triangular diffusion,
 * bilinear upsampling and white screen blend in signed full-range UYVY.
 * Soft focus reuses the same diffusion on luma itself (midtones too) and
 * blends toward it like a diffusion filter: pixels darker than their
 * surroundings' light are lifted (mist, halos spilling into shadows), and
 * brighter ones are softened a little (lower local contrast).
 * Ring rows preserve original highlights ahead of in-place image writes.
 * No allocation, I/O or floating point; workspace may be reused after halo. */
#include "film_bloom.h"
#include "film_width.h"
#include <string.h>
#define BLOOM_MAX_RADIUS 24u
unsigned film_bloom_radius(unsigned level){static const unsigned v[4]={0,32,64,96};return level<4?v[level]:0;}
unsigned film_bloom_strength(unsigned level){static const unsigned strengths[4]={0,32,120,192};return level<4?strengths[level]:0;}
/* Soft focus per level: diffusion radius in pixels, and the Q8 share of the
 * difference to the diffused light taken where it is brighter (lift) and
 * where it is darker (soften). */
unsigned film_soft_radius(unsigned level){static const unsigned v[4]={0,32,64,96};return level<4?v[level]:0;}
static const unsigned soft_lift[4]={0,72,120,168},soft_soften[4]={0,24,44,64};
size_t film_bloom_work_size(unsigned width) {
 size_t grid=(width+3u)/4u,ring=grid*(2u*BLOOM_MAX_RADIUS+2u);
 if(!width||width>FILM_MAX_WIDTH||width%2)return 0;
 return ((ring+3u)&~(size_t)3u)+grid*17u;
}
/* Per grid column: the triangular vertical sum and its two halves (each at
 * most 25 x 255), interleaved so one cache line serves four columns. */
typedef struct {unsigned weighted;unsigned short left,right;} bloom_col;
typedef struct {
 const unsigned char *image;
 unsigned char highlight[256];
 unsigned char *ring,*tmp,*fresh,*current,*next,*last;
 bloom_col *cols;
 unsigned width,height,stride,gw,gh,radius,win,slot,center,last_valid;
 unsigned inv;              /* radii other than 8, 16, 24: ceil(2^32/(radius+1)^2) */
 unsigned current_glow,next_glow;
 int soft,fast;
 #ifdef FILM_HWMASK
 const unsigned char *quarter;unsigned qstride;
 #endif
} bloom_rows;
#ifdef FILM_HWMASK
/* A 1/4-size UYVY copy of the frame, made by the DIGIC's resize engine, for
 * the next Bloom: its luma replaces the 4x4 reads of the full frame (the
 * highlight knee then applies to the shrunk luma). Its last row and column
 * are unreliable, so the ones before them are used. One use. */
static const unsigned char *bloom_quarter;
static unsigned bloom_quarter_stride,bloom_quarter_w,bloom_quarter_h;
void film_bloom_use_quarter(const unsigned char *uyvy,unsigned stride,unsigned width,unsigned height) {
 bloom_quarter=uyvy;bloom_quarter_stride=stride;bloom_quarter_w=width;bloom_quarter_h=height;
}
#endif
static unsigned clamp_index(int i,unsigned n){return i<0?0:(unsigned)i>=n?n-1:(unsigned)i;}
/* value/(radius+1)^2. Radii 8, 16 and 24 (L) divide by a constant; the
 * scaled radii of smaller frames (film_bloom_scale) multiply by inv, which is
 * exact here: value < 256*(radius+1)^2 and (radius+1)^2 <= 625 < 4096. */
static unsigned divide(unsigned value,unsigned radius,unsigned inv) {
 if(radius==8)return value/81u;
 if(radius==16)return value/289u;
 if(radius==24)return value/625u;
 return (unsigned)(((unsigned long long)value*inv)>>32);
}
static void prepare_highlights(bloom_rows *rows,unsigned level) {
 static const unsigned thresholds[4]={0,192,168,144};
 unsigned threshold=thresholds[level];
 unsigned range=255u-threshold;
 unsigned denominator=range*range;
 memset(rows->highlight,0,threshold+1);
 /* Compute the soft knee once, then use a lookup for each source pixel. */
 for(unsigned y=threshold+1;y<256;y++) {
  unsigned above=y-threshold;
  rows->highlight[y]=(unsigned char)((above*above*255u+denominator/2)/denominator);
 }
}
/* Soft focus diffuses luma itself. */
static void prepare_soft(bloom_rows *rows) {
 for(unsigned y=0;y<256;y++)rows->highlight[y]=(unsigned char)y;
}
/* filtered_row's horizontal pass, for a constant radius (8, 16, 24: L) so the
 * division is by a constant, or 0: the scaled radius in c, through inv. */
static inline __attribute__((always_inline)) void filter_cols(const bloom_rows *c,unsigned char *out,
 unsigned weighted,unsigned left,unsigned right,const unsigned radius) {
 const unsigned r=radius?radius:c->radius,den=(r+1)*(r+1),inv=c->inv;
 for(unsigned x=0;x<c->gw;x++) {
  unsigned old=c->tmp[clamp_index((int)x-(int)r,c->gw)];
  unsigned mid=c->tmp[clamp_index((int)x+1,c->gw)];
  unsigned incoming=c->tmp[clamp_index((int)x+(int)r+2,c->gw)];
  out[x]=(unsigned char)(radius?(weighted+den/2)/den:(unsigned)(((unsigned long long)(weighted+den/2)*inv)>>32));
  weighted=weighted+right-left;left=left-old+mid;right=right-mid+incoming;
 }
}
static void filtered_row(bloom_rows *c,unsigned row,unsigned char *out) {
 unsigned x,weighted=0,left=0,right=0,r=c->radius;
 if(row==c->gh-1&&c->last_valid){memcpy(out,c->last,c->gw);return;}
#ifdef FILM_HWMASK
 if(c->quarter) {
  const unsigned char *q=c->quarter+(size_t)(row+1<c->gh?row:row-1)*c->qstride+1;
  for(x=0;x+1<c->gw;x++)c->tmp[x]=c->highlight[q[x*2]];
  c->tmp[x]=c->tmp[x-1];
 } else
#endif
 {
 /* Interior cells: read each pixel pair as one aligned 32-bit word
  * (U,Y0,V,Y1 little-endian), halving uncached accesses. Same sums. */
 x=0;
 if(row*4+3<c->height&&!(((size_t)c->image|c->stride)&3u)) {
  const unsigned *r0=(const unsigned*)(c->image+(size_t)row*4*c->stride);
  unsigned words=c->stride/4,full=c->width/4;
  if(c->fast) {
   /* Fast Bloom: rows 0 and 2 of each cell, as soft focus samples luma. */
   for(;x<full;x++) {
    const unsigned *q=r0+x*2,*q2=q+2*words;
    unsigned w0=q[0],w1=q[1],w2=q2[0],w3=q2[1];
    unsigned sum=c->highlight[(w0>>8)&255]+c->highlight[w0>>24]+c->highlight[(w1>>8)&255]+c->highlight[w1>>24]
                +c->highlight[(w2>>8)&255]+c->highlight[w2>>24]+c->highlight[(w3>>8)&255]+c->highlight[w3>>24];
    c->tmp[x]=(sum+4u)>>3;
   }
  }
  if(c->soft) {
   /* Soft focus spreads luma itself over 32-96 px: rows 0 and 2 of each cell
    * are plenty, read straight from the words without the mask table. */
   for(;x<full;x++) {
    const unsigned *q=r0+x*2,*q2=q+2*words;
    unsigned w0=q[0],w1=q[1],w2=q2[0],w3=q2[1];
    unsigned sum=((w0>>8)&255u)+(w0>>24)+((w1>>8)&255u)+(w1>>24)+((w2>>8)&255u)+(w2>>24)+((w3>>8)&255u)+(w3>>24);
    c->tmp[x]=(sum+4u)>>3;
   }
  }
  for(;x<full;x++) {
   unsigned sum=0;const unsigned *q=r0+x*2;
   for(unsigned dy=0;dy<4;dy++,q+=words) {
    unsigned w0=q[0],w1=q[1];
    sum+=c->highlight[(w0>>8)&255]+c->highlight[w0>>24]+c->highlight[(w1>>8)&255]+c->highlight[w1>>24];
   }
   c->tmp[x]=(sum+8u)>>4;
  }
 }
 for(;x<c->gw;x++) {
  unsigned sum=0,count=0;
  /* Fast Bloom samples rows 0 and 2 of a full cell on any frame, as above. */
  if(c->fast&&row*4+3<c->height&&x*4+3<c->width) {
   for(unsigned dy=0;dy<4;dy+=2) {
    const unsigned char *p=c->image+(size_t)(row*4+dy)*c->stride+x*8;
    sum+=c->highlight[p[1]]+c->highlight[p[3]]+c->highlight[p[5]]+c->highlight[p[7]];
   }
   c->tmp[x]=(sum+4u)>>3;
   continue;
  }
  for(unsigned dy=0;dy<4&&row*4+dy<c->height;dy++) {
   const unsigned char *p=c->image+(size_t)(row*4+dy)*c->stride;
   for(unsigned dx=0;dx<4&&x*4+dx<c->width;dx++){sum+=c->highlight[p[(x*4+dx)*2+1]];count++;}
  }
  c->tmp[x]=count==16?(sum+8u)>>4:(sum+count/2)/count;
 }
 }
 for(int k=-(int)r;k<=(int)r+1;k++) {
  unsigned v=c->tmp[clamp_index(k,c->gw)];
  if(k<=0)left+=v;else right+=v;
  if(k<=(int)r)weighted+=v*(r+1u-(unsigned)(k<0?-k:k));
 }
 if(r==8)filter_cols(c,out,weighted,left,right,8);
 else if(r==16)filter_cols(c,out,weighted,left,right,16);
 else if(r==24)filter_cols(c,out,weighted,left,right,24);
 else filter_cols(c,out,weighted,left,right,0);
 /* Future bottom-border replication must never re-read modified pixels. */
 if(row==c->gh-1){memcpy(c->last,out,c->gw);c->last_valid=1;}
}
static unsigned blurred_row(bloom_rows *c,unsigned char *out) {
 unsigned den=(c->radius+1)*(c->radius+1);
 unsigned has_glow=0;
 for(unsigned x=0;x<c->gw;x++) {
  unsigned value=divide(c->cols[x].weighted+den/2,c->radius,c->inv);
  out[x]=(unsigned char)value;
  has_glow|=value;
 }
 return has_glow;
}
/* advance's column loop with blurred_row folded in (the same sums, one
 * pass), for a constant radius so the division is by a constant. */
static inline __attribute__((always_inline)) unsigned advance_cols(bloom_rows *c,unsigned char *slot_row,
 const unsigned char *mid_row,const unsigned radius) {
 /* radius 0: the scaled radius in c, divided through its reciprocal. */
 const unsigned rr=radius?radius:c->radius,den=(rr+1)*(rr+1),inv=c->inv;
 bloom_col *k=c->cols;const unsigned char *fresh=c->fresh;unsigned char *out=c->next;unsigned glow=0;
 for(unsigned x=0;x<c->gw;x++) {
  unsigned old=slot_row[x],mid=mid_row[x],v=fresh[x];
  unsigned w=k[x].weighted+k[x].right-k[x].left;
  k[x].weighted=w;k[x].left=(unsigned short)(k[x].left-old+mid);k[x].right=(unsigned short)(k[x].right-mid+v);
  slot_row[x]=(unsigned char)v;
  unsigned o=radius?(w+den/2)/den:(unsigned)(((unsigned long long)(w+den/2)*inv)>>32);
  out[x]=(unsigned char)o;glow|=o;
 }
 return glow;
}
static void advance(bloom_rows *c) {
 if(c->center+1>=c->gh) {
  memcpy(c->next,c->current,c->gw);
  c->next_glow=c->current_glow;
  return;
 }
 unsigned middle=(c->slot+c->radius+1)%c->win;
 unsigned incoming=++c->center+c->radius+1;
 filtered_row(c,incoming>=c->gh?c->gh-1:incoming,c->fresh);
 unsigned char *slot_row=c->ring+(size_t)c->slot*c->gw;const unsigned char *mid_row=c->ring+(size_t)middle*c->gw;
 /* Constant divisors for radius 8, 16 and 24; any other (a scaled radius)
  * takes radius 0 here, which means its reciprocal and the radius in c. */
 c->next_glow=c->radius==8?advance_cols(c,slot_row,mid_row,8):c->radius==16?advance_cols(c,slot_row,mid_row,16):
  c->radius==24?advance_cols(c,slot_row,mid_row,24):advance_cols(c,slot_row,mid_row,0);
 if(++c->slot==c->win)c->slot=0;
}
static unsigned vertical_glow(const bloom_rows *rows,unsigned col,unsigned fraction) {
 return rows->current[col]*(4-fraction)+rows->next[col]*fraction;
}

static unsigned screen_amount(unsigned weighted,unsigned strength) {
 unsigned value=(weighted>>4)*strength+127u;
 return (value+1u+(value>>8))>>8;
}

static unsigned div255(unsigned x){x+=127u;return (x+1u+(x>>8))>>8;}
static unsigned char faded(unsigned char value,unsigned amount) {
 int v=value<128?value:(int)value-256;
 unsigned magnitude=(unsigned)(v<0?-v:v);
 int result=(int)div255(magnitude*(255u-amount));
 return (unsigned char)(v<0?-result:result);
}
/* fade: 0, or the fade table (row = amount, 256 bytes). shared: fast
 * Bloom, both lumas take the pair's average amount, screened together in
 * two 16-bit lanes of one multiply (each lane exactly div255). */
#define BLOOM_INLINE static inline __attribute__((always_inline))
BLOOM_INLINE void blend_pair(unsigned char *pair,unsigned first,unsigned second,const unsigned char *fade,int shared) {
 unsigned average=(first+second+1)/2;
 if(shared==2&&fade) {   /* aligned pair: one load and one store */
  unsigned w=*(unsigned *)pair,y=(w>>8)&0x00ff00ffu,x=(0x00ff00ffu-y)*average+0x007f007fu;
  const unsigned char *f=fade+average*256u;
  y+=((x+0x00010001u+((x>>8)&0x00ff00ffu))>>8)&0x00ff00ffu;
  *(unsigned *)pair=f[w&255u]|y<<8|(unsigned)f[(w>>16)&255u]<<16;
  return;
 }
 if(shared) {
  unsigned y=pair[1]|(unsigned)pair[3]<<16,x=(0x00ff00ffu-y)*average+0x007f007fu;
  y+=((x+0x00010001u+((x>>8)&0x00ff00ffu))>>8)&0x00ff00ffu;
  pair[1]=(unsigned char)y;pair[3]=(unsigned char)(y>>16);
 } else {
  pair[1]=(unsigned char)(pair[1]+div255((255u-pair[1])*first));
  pair[3]=(unsigned char)(pair[3]+div255((255u-pair[3])*second));
 }
 if(fade){const unsigned char *f=fade+average*256u;pair[0]=f[pair[0]];pair[2]=f[pair[2]];}
 else{pair[0]=faded(pair[0],average);pair[2]=faded(pair[2],average);}
}
static const unsigned char *fade_table;
static int bloom_fast;
void film_bloom_fast(int on){bloom_fast=on!=0;}
/* Frame scale (film_bloom_scale), Q16: 65536 is L. */
static unsigned bloom_scale=65536u;
void film_bloom_scale(unsigned scale){bloom_scale=scale>=65536u||!scale?65536u:scale;}
/* screen_amount(w, strength) depends only on w>>4: one table per Bloom. */
static unsigned char bloom_amount[256];
size_t film_bloom_fade_table_size(void){return 256u*256u;}
void film_bloom_fade_table(unsigned char *memory) {
 if(memory)for(unsigned a=0;a<256;a++)for(unsigned v=0;v<256;v++)memory[a*256u+v]=faded((unsigned char)v,a);
 fade_table=memory;
}

/* Soft focus on one pair: glow values are the diffused luma x16. Lift toward
 * brighter surroundings, soften toward darker ones; colour fades as much as
 * the pixel is lifted (the scattered light is neutral). */
BLOOM_INLINE void soft_pair(unsigned char *pair,unsigned g0,unsigned g1,unsigned lift,unsigned soften,const unsigned char *fade) {
 int d0=(int)g0-(int)pair[1]*16,d1=(int)g1-(int)pair[3]*16,up=0;
 int a0=d0>0?(d0*(int)lift+2048)>>12:-((-d0*(int)soften+2048)>>12);
 int a1=d1>0?(d1*(int)lift+2048)>>12:-((-d1*(int)soften+2048)>>12);
 pair[1]=(unsigned char)((int)pair[1]+a0);pair[3]=(unsigned char)((int)pair[3]+a1);
 up=(a0>0?a0:0)+(a1>0?a1:0);
 if(up) {
  unsigned amount=(unsigned)up*2u>255u?255u:(unsigned)up*2u;
  if(fade){const unsigned char *f=fade+amount*256u;pair[0]=f[pair[0]];pair[2]=f[pair[2]];}
  else{pair[0]=faded(pair[0],amount);pair[2]=faded(pair[2],amount);}
 }
}

/* One row's blend; inlined twice, with and without the fade table. */
BLOOM_INLINE void soft_row(const bloom_rows *c,unsigned char *pixels,unsigned width,unsigned fy,unsigned lift,unsigned soften,const unsigned char *fade) {
 unsigned left=vertical_glow(c,0,fy);
 for(unsigned col=0;col<c->gw;col++) {
  unsigned right=col+1<c->gw?vertical_glow(c,col+1,fy):left;
  unsigned char *pair=pixels+col*8;
  soft_pair(pair,4*left,3*left+right,lift,soften,fade);
  if(col*4+2<width)soft_pair(pair+4,2*left+2*right,left+3*right,lift,soften,fade);
  left=right;
 }
}
BLOOM_INLINE void bloom_row(const bloom_rows *c,unsigned char *pixels,unsigned width,unsigned fy,const unsigned char *fade,int shared) {
 const unsigned char *amount=bloom_amount;
 unsigned left=vertical_glow(c,0,fy);
 for(unsigned col=0;col<c->gw;col++) {
  unsigned right=col+1<c->gw?vertical_glow(c,col+1,fy):left;
  if(left||right) {
   /* Reuse the two vertical samples for all four horizontal positions. */
   unsigned char *pair=pixels+col*8;
   unsigned first=amount[(4*left+8)>>4];
   unsigned second=amount[(3*left+right+8)>>4];
   blend_pair(pair,first,second,fade,shared);
   if(col*4+2<width) {
    unsigned third=amount[(2*left+2*right+8)>>4];
    unsigned fourth=amount[(left+3*right+8)>>4];
    blend_pair(pair+4,third,fourth,fade,shared);
   }
  }
  left=right;
 }
}
static int bloom_overlap(const void *a,size_t a_size,const void *b,size_t b_size) {
 size_t x=(size_t)a,y=(size_t)b;
 return x<=y?y-x<a_size:x-y<b_size;
}

static int diffuse_rows(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned level,int soft,void *work,size_t work_size,
 const film_bloom_row_pass *pass,void (*progress)(unsigned,unsigned)) {
 size_t need=film_bloom_work_size(width),a=(size_t)image,b=(size_t)work;
 bloom_rows c;unsigned strength=film_bloom_strength(level);
 if(!image||!need||!height||stride<width*2||height>size/stride||level>3)return -1;
 if(!level)return 0;
 if(!work||work_size<need||(b&3u)||(a<=b?b-a<size:a-b<need))return -1;
 unsigned char *scratch=pass?pass->scratch:0;
 if(pass&&(!scratch||!pass->finish_row||pass->capacity<width*2||
    bloom_overlap(scratch,width*2,image,size)||bloom_overlap(scratch,width*2,work,need)))return -1;
 memset(work,0,need);
 c.soft=soft;c.fast=!soft&&bloom_fast;
 if(!soft)for(unsigned i=0;i<256;i++)bloom_amount[i]=(unsigned char)screen_amount(i<<4,strength);
 if(soft)prepare_soft(&c);else prepare_highlights(&c,level);
#ifdef FILM_HWMASK
 c.quarter=0;
 if(!soft) {
  if(bloom_quarter&&bloom_quarter_w==(width+3u)/4u&&bloom_quarter_h==(height+3u)/4u&&
     bloom_quarter_w>1&&bloom_quarter_h>1&&bloom_quarter_stride>=bloom_quarter_w*2){c.quarter=bloom_quarter;c.qstride=bloom_quarter_stride;}
  bloom_quarter=0;
 }
#endif
 c.image=image;c.width=width;c.height=height;c.stride=stride;c.gw=(width+3u)/4u;c.gh=(height+3u)/4u;
 c.radius=(soft?film_soft_radius(level):film_bloom_radius(level))/4;
 /* A smaller frame stands for L shrunk: the glow spreads over the same share
  * of it, so the radius (in 4 px cells) shrinks with the frame. */
 if(bloom_scale!=65536u){c.radius=(c.radius*bloom_scale+32768u)>>16;if(!c.radius)c.radius=1;}
 c.inv=(unsigned)((0x100000000ull+(c.radius+1)*(c.radius+1)-1u)/((c.radius+1)*(c.radius+1)));
 c.win=2*c.radius+2;c.slot=c.center=c.last_valid=0;
 c.ring=work;c.cols=(bloom_col*)(c.ring+(((size_t)c.gw*(2u*BLOOM_MAX_RADIUS+2u)+3u)&~(size_t)3u));
 c.tmp=(unsigned char*)(c.cols+c.gw);   /* 8+5 bytes per column of the 17 reserved */
 c.fresh=c.tmp+c.gw;c.current=c.fresh+c.gw;c.next=c.current+c.gw;c.last=c.next+c.gw;
 for(unsigned i=0;i<c.win;i++) {
  unsigned row=clamp_index((int)i-(int)c.radius,c.gh);
  unsigned char *p=c.ring+(size_t)i*c.gw;filtered_row(&c,row,p);
  unsigned weight=i<=c.radius?i+1:2*c.radius+1-i;
  for(unsigned x=0;x<c.gw;x++) {
   if(i<=c.radius)c.cols[x].left+=p[x];else c.cols[x].right+=p[x];
   c.cols[x].weighted+=p[x]*weight;
  }
 }
 c.current_glow=blurred_row(&c,c.current);advance(&c);
 for(unsigned row=0;row<height;row++) {
  unsigned char *source=image+(size_t)row*stride;
  unsigned char *pixels=source;
  unsigned fy=row&3;
  /* With a zero mask the row is already Bloom's exact output. */
  if(soft) {
   if(scratch){memcpy(scratch,source,width*2);pixels=scratch;}
   if(fade_table)soft_row(&c,pixels,width,fy,soft_lift[level],soft_soften[level],fade_table);
   else soft_row(&c,pixels,width,fy,soft_lift[level],soft_soften[level],0);
  }
  else if(c.current_glow||(fy&&c.next_glow)) {
   if(scratch) {
    memcpy(scratch,source,width*2);
    pixels=scratch;
   }
   if(c.fast&&fade_table&&!((size_t)pixels&3u))bloom_row(&c,pixels,width,fy,fade_table,2);
   else if(c.fast)bloom_row(&c,pixels,width,fy,fade_table,1);
   else if(fade_table)bloom_row(&c,pixels,width,fy,fade_table,0);
   else bloom_row(&c,pixels,width,fy,0,0);
  }
  if(pass) {
   pass->finish_row(pixels,pass->context);
   if(pixels!=source)memcpy(source,pixels,width*2);
  }
  if(fy==3&&row+1<height) {
   unsigned char *swap=c.current;
   c.current=c.next;
   c.next=swap;
   c.current_glow=c.next_glow;
   advance(&c);
  }
  if(progress&&(row%32==31||row+1==height))progress(row+1,height);
 }
 return 0;
}

int film_uyvy_bloom_rows(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned level,void *work,size_t work_size,
 const film_bloom_row_pass *pass,void (*progress)(unsigned,unsigned)) {
 return diffuse_rows(image,size,width,height,stride,level,0,work,work_size,pass,progress);
}
int film_uyvy_bloom(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned level,void *work,size_t work_size,void (*progress)(unsigned,unsigned)) {
 return film_uyvy_bloom_rows(image,size,width,height,stride,level,work,work_size,0,progress);
}
int film_uyvy_soften(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned level,void *work,size_t work_size,void (*progress)(unsigned,unsigned)) {
 return diffuse_rows(image,size,width,height,stride,level,1,work,work_size,0,progress);
}
