/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Original grain approximation, not Apple's algorithm.
 * Correlated 2-pixel clumps plus a fine component; no repeating image tile.
 * Luma only, after colour/LUT/halation and before Canon JPEG encoding.
 * All allocation belongs to SpyTask. No file I/O or allocation here. */
#include "film_grain.h"
#include "film_width.h"
static unsigned next_random(unsigned *state) {
 unsigned x=*state;x^=x<<13;x^=x>>17;x^=x<<5;*state=x;return x;
}
static void noise_row(signed char *p,unsigned count,unsigned *state) {
 for(unsigned i=0;i<count;i++) {
  unsigned x=next_random(state);
  int n=(int)(x&255)+(int)((x>>8)&255)+(int)((x>>16)&255)+(int)(x>>24)-510;
  p[i]=(signed char)(n/4);
 }
}
size_t film_grain_work_size(unsigned width){return !width||width>FILM_MAX_WIDTH||width%2?0:(size_t)(width/2+1)*2;}
unsigned film_grain_amplitude(unsigned level){static const unsigned values[4]={0,2,4,7};return level<4?values[level]:0;}
/* Frame scale (film_grain_scale): the grain gain, Q16. 65536 is L. */
static unsigned grain_gain=65536u;
static __attribute__((unused)) unsigned char grained(unsigned char y,int noise,const unsigned short weights[256]) {
 int scaled=noise*(int)weights[y];
 int delta=scaled>=0?(scaled+16320)/32640:-((-scaled+16320)/32640);
 int result=(int)y+delta;
 return result<0?0:result>255?255:(unsigned char)result;
}
#ifdef MAGUS_G10
/* G10 (docs/PORT-G10.md): the G10's frame is 1.47x the G12's on the same
 * CPU, so the per-pixel step is cheaper here: weights are kept in Q16 of
 * the G12's divisor (w * 65536 / 32640, at most 3584), the division becomes
 * a multiply and a shift, and a table clamps. The grain is the same noise
 * at the same strength; only exact rounding ties can land one level apart
 * (tests/test_grain_g10.c). */
static unsigned char clamp_table[256+128];
static void clamp_init(void){for(int i=0;i<256+128;i++)clamp_table[i]=(unsigned char)(i<64?0:i>=64+255?255:i-64);}
static inline unsigned grained16(unsigned y,int noise,const unsigned short *w16) {
 return clamp_table[64+(int)y+((noise*(int)w16[y]+32768)>>16)];
}
#define GRAINED(y,noise,w) grained16((y),(noise),(w))
#else
#define GRAINED(y,noise,w) grained((y),(noise),(w))
#endif
int film_grain_begin_rows(film_grain_rows *state,unsigned width,unsigned height,
 unsigned level,unsigned seed,void *work,size_t work_size) {
 size_t need=film_grain_work_size(width);
 unsigned amplitude=film_grain_amplitude(level);
 if(!need||!height||!amplitude||!work||work_size<need)return -1;
 state->columns=width/2+1;
 state->height=height;
 state->row=0;
 state->upper=work;
 state->lower=state->upper+state->columns;
 state->fine=seed?seed:0x6d2b79f5u;
 state->coarse=state->fine^0xa511e9b3u;
 if(!state->coarse)state->coarse=0x1b873593u;
 for(unsigned y=0;y<256;y++) {
  unsigned w=((4u*y*(255u-y)+127u)/255u)*amplitude;
  state->weights[y]=(unsigned short)(grain_gain==65536u?w:(w*grain_gain+32768u)>>16);
#ifdef MAGUS_G10
  state->weights[y]=(unsigned short)(((unsigned)state->weights[y]*65536u+16320u)/32640u);
#endif
 }
#ifdef MAGUS_G10
 if(!clamp_table[64+255])clamp_init();
#endif
 noise_row(state->upper,state->columns,&state->coarse);
 noise_row(state->lower,state->columns,&state->coarse);
 return 0;
}

void film_grain_finish_row(unsigned char *pixels,void *context) {
 film_grain_rows *state=context;
 signed char *upper=state->upper,*lower=state->lower;
 unsigned fine=state->fine,odd=state->row&1;
 unsigned pairs=state->columns-1;
 int left=odd?(upper[0]+lower[0])/2:upper[0];
 const unsigned short *weights=state->weights;
 if(!((size_t)pixels&3u)) {
  /* Aligned rows (the camera's frames): one word per pixel pair, U Y0 V Y1,
   * and the odd/even rows in loops of their own. Same bytes as below. */
  unsigned *word=(unsigned *)pixels;
  if(odd) {
   for(unsigned i=0;i<pairs;i++) {
    unsigned r=next_random(&fine),w=word[i];
    int right=(upper[i+1]+lower[i+1])/2;
    unsigned y0=GRAINED((unsigned char)(w>>8),left*4+(int)(r&255)-(int)((r>>8)&255),weights);
    unsigned y1=GRAINED((unsigned char)(w>>24),((left+right)/2)*4+(int)((r>>16)&255)-(int)(r>>24),weights);
    word[i]=(w&0x00ff00ffu)|y0<<8|y1<<24;
    left=right;
   }
  } else {
   for(unsigned i=0;i<pairs;i++) {
    unsigned r=next_random(&fine),w=word[i];
    int right=upper[i+1];
    unsigned y0=GRAINED((unsigned char)(w>>8),left*4+(int)(r&255)-(int)((r>>8)&255),weights);
    unsigned y1=GRAINED((unsigned char)(w>>24),((left+right)/2)*4+(int)((r>>16)&255)-(int)(r>>24),weights);
    word[i]=(w&0x00ff00ffu)|y0<<8|y1<<24;
    left=right;
   }
  }
 } else for(unsigned i=0;i<pairs;i++,pixels+=4) {
  unsigned r=next_random(&fine);
  int right=upper[i+1];
  if(odd)right=(right+lower[i+1])/2;
  int small0=(int)(r&255)-(int)((r>>8)&255),small1=(int)((r>>16)&255)-(int)(r>>24);
  pixels[1]=GRAINED(pixels[1],left*4+small0,weights);
  pixels[3]=GRAINED(pixels[3],((left+right)/2)*4+small1,weights);
  left=right;
 }
 state->fine=fine;
 state->row++;
 if(odd&&state->row<state->height) {
  state->upper=lower;
  state->lower=upper;
  noise_row(state->lower,state->columns,&state->coarse);
 }
}

int film_uyvy_grain(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned level,unsigned seed,void *work,size_t work_size,
 void (*progress)(unsigned,unsigned)) {
 size_t need=film_grain_work_size(width),a=(size_t)image,b=(size_t)work;
 film_grain_rows state;
 if(!image||!need||!height||stride<width*2||height>size/stride||level>3)return -1;
 if(!level)return 0;
 if(a<=b?b-a<size:a-b<need)return -1;
 if(film_grain_begin_rows(&state,width,height,level,seed,work,work_size))return -1;
 for(unsigned row=0;row<height;row++) {
  film_grain_finish_row(image+(size_t)row*stride,&state);
  if(progress&&(row%32==31||row+1==height))progress(row+1,height);
 }
 return 0;
}

/* A smaller frame stands for the L frame shrunk, which averages the grain
 * down. The gain matches the grain's strength (standard deviation on flat
 * grey) to L's shrunk with Lanczos, measured at M1, M2 and S, the same at
 * every level: 0.862, 0.652 and 0.315 (the 2 px clumps fade more slowly than
 * the frame shrinks). Linear between those points (Q16). */
void film_grain_scale(unsigned scale) {
 static const unsigned at[5]={0,11498,28744,50589,65536},gain[5]={0,20644,42729,56492,65536};
 unsigned i=1;
 if(scale>=65536u||!scale){grain_gain=65536u;return;}
 while(scale>at[i])i++;
 grain_gain=gain[i-1]+(unsigned)((unsigned long long)(gain[i]-gain[i-1])*(scale-at[i-1])/(at[i]-at[i-1]));
}
