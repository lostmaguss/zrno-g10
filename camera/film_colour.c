/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Independent checked LUT/interpolation and UYVY effects.
 * No JPEG codec or external film-engine dependency.
 *
 * The camera grades through Canon's DIGIC colour table only. Zrno loads the
 * look's table, this file adds the effects to the frame Canon has developed
 * (film_uyvy_graded_retro), and Canon applies the table when it encodes the
 * JPEG, after the effects. So the frame here still has the scene's colours,
 * also on a black-and-white look. "Graded" in the names below means the
 * effects of that path, not that the look is already in the frame. The CPU LUT (validation, interpolation and the
 * LUT passes) is the host reference the tables are fitted to
 * (tools/hw_table.py) and that tests and tools compare with; it is not built
 * into the camera (CPU_LUT 0 with FILM_LAB_CAMERA). */
#ifdef FILM_LAB_CAMERA
#define CPU_LUT 0
#else
#define CPU_LUT 1
#endif
#include "film_colour.h"
#include "film_width.h"
#include "film_retro.h"
#include "film_toy.h"
#include "film_print.h"
#include "film_compact.h"
#include <string.h>
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
#if CPU_LUT
static unsigned le16(const uint8_t *p) { return p[0]|((unsigned)p[1]<<8); }
static uint32_t le32(const uint8_t *p) { return le16(p)|(uint32_t)le16(p+2)<<16; }
static uint32_t fnv(const uint8_t *p,size_t n) {
    uint32_t h=2166136261u; while(n--) h=(h^*p++)*16777619u; return h;
}
FILM_COMPACT int film_lut_validate(const void *data,size_t size) {
    const uint8_t *p=data; unsigned n;
    if(!data || size<16 || memcmp(p,"G12LUT1\0",8)) return -1;
    n=le16(p+8);
    if((n!=17 && n!=33) || le16(p+10)!=1 || size!=16u+n*n*n*6u) return -1;
    return fnv(p+16,size-16)==le32(p+12)?0:-1;
}
/* Kernels use a LUT prepared in place once at load. Every RGB16 node is
 * stored as value^0x8000, which is value-32768 read as int16, so the inner
 * loop can use signed 16x16 multiply-accumulates. The header magic becomes
 * "G12LUTP" and byte 10 records whether every node has R==G==B. */
#define LUT_MONO 1u
static unsigned prepared_lattice(const void *lut,int *mono) {
    const uint8_t *p=lut; unsigned n;
    if(!lut||((size_t)lut&1)||memcmp(p,"G12LUTP\0",8)) return 0;
    n=le16(p+8); if(n!=17&&n!=33) return 0;
    *mono=(p[10]&LUT_MONO)!=0; return n;
}
FILM_COMPACT int film_lut_prepare(void *data,size_t size) {
    uint8_t *p=data; unsigned short *node; unsigned i,count,mono=1;
    if(film_lut_validate(data,size)||((size_t)data&1)) return -1;
    count=le16(p+8); count=count*count*count; node=(unsigned short*)(p+16);
    for(i=0;i<count;i++,node+=3) {
        if(node[0]!=node[1]||node[1]!=node[2]) mono=0;
        node[0]^=0x8000u; node[1]^=0x8000u; node[2]^=0x8000u;
    }
    memcpy(p,"G12LUTP\0",8); p[10]=mono?LUT_MONO:0; p[11]=0;
    return 0;
}
/* Reference interpolation; accepts a validated file image or a prepared LUT. */
void film_lut_pixel(const void *data,const uint8_t in[3],uint8_t out[3]) {
    const uint8_t *p=data; int n=le16(p+8),i[3],f[3],order[3]={0,1,2};
    unsigned bias=memcmp(p,"G12LUTP\0",8)?0:0x8000u;
    int stride[3]={1,n,n*n},idx,k,c,tmp,verts[4],weights[4];
    for(k=0;k<3;k++) {
        int t=in[k]*(n-1); i[k]=(t+1+(t>>8))>>8; f[k]=t-i[k]*255;
        if(i[k]==n-1) {i[k]--;f[k]=255;}
    }
    /* Sort the fractions: tetrahedral interpolation, R varies fastest. */
    if(f[order[0]]<f[order[1]]) {tmp=order[0];order[0]=order[1];order[1]=tmp;}
    if(f[order[1]]<f[order[2]]) {tmp=order[1];order[1]=order[2];order[2]=tmp;}
    if(f[order[0]]<f[order[1]]) {tmp=order[0];order[0]=order[1];order[1]=tmp;}
    idx=i[0]+n*(i[1]+n*i[2]); verts[0]=idx;
    for(k=0;k<3;k++) verts[k+1]=verts[k]+stride[order[k]];
    weights[0]=255-f[order[0]];weights[1]=f[order[0]]-f[order[1]];
    weights[2]=f[order[1]]-f[order[2]];weights[3]=f[order[2]];
    for(c=0;c<3;c++) {
        uint32_t sum=0;for(k=0;k<4;k++) sum+=(le16(p+16+verts[k]*6+c*2)^bias)*weights[k];
        /* Exact x/65535 for this bounded 24-bit numerator. */
        sum+=32767u;out[c]=(uint8_t)((sum+1u+(sum>>16))>>16);
    }
}
#endif /* CPU_LUT */

/* This table lives after the cached row. Each axis entry packs a node
 * offset in RGB16 samples above the eight fraction bits. Keep the green
 * contributions unrounded until both signed terms have been added. */
typedef struct {
 uint32_t red[256], green[256], blue[256];
 struct {
  int green, direct;
 } u[256], v[256];
 /* Per-level halation energy for each blur-minus-source difference:
  * red linear addition and the green one-fifth addition. */
 unsigned short energy[256], green_energy[256];
} colour_tables;

#if CPU_LUT
/* Signed 16x16 multiply-accumulate: SMLABB on ARMv5TE.
 * arm_acle.h clashes with this file's own uint32_t typedef; use its builtin. */
#if defined(__ARM_FEATURE_DSP)
#define smlabb(a,b,c) __builtin_arm_smlabb((a),(b),(c))
#elif defined(__arm__)
#error "film_colour.c expects an ARMv5TE or later target with DSP multiplies"
#else
#define smlabb(a,b,c) ((int)(short)(a)*(int)(short)(b)+(c))
#endif
typedef short node_t;
/* The four tetrahedral weights sum to 255, so adding 32768*255 to the sum of
 * biased products restores the exact unbiased sum. */
#define NODE_BIAS (32768u*255u)
static inline __attribute__((always_inline)) unsigned char lut_channel(const node_t *p,
 const node_t *q,const node_t *v,const node_t *last,unsigned w0,unsigned w1,unsigned w2,unsigned w3) {
 int acc=smlabb(*p,(int)w0,0);
 acc=smlabb(*q,(int)w1,acc);acc=smlabb(*v,(int)w2,acc);acc=smlabb(*last,(int)w3,acc);
 unsigned sum=(unsigned)acc+NODE_BIAS+32767u;
 /* Exact x/65535 for this bounded 24-bit numerator. */
 return (unsigned char)((sum+1u+(sum>>16))>>16);
}
/* Exact tetrahedral interpolation over aligned little-endian prepared nodes.
 * The axis table removes repeated division/index setup for every pixel.
 * Monochrome LUTs need only the first channel. */
static inline __attribute__((always_inline)) void fast_pixel(const node_t *nodes,
 unsigned n,const unsigned short axis[256],const colour_tables *tables,
 const unsigned char in[3],unsigned char out[3],int mono) {
 unsigned fr,fg,fb,a,c,w0,w1,w2,w3;
 unsigned green_stride=3*n,blue_stride=green_stride*n;
 const node_t *p,*q,*v,*last;
 if(tables) {
  unsigned r=tables->red[in[0]],g=tables->green[in[1]],b=tables->blue[in[2]];
  p=nodes+(r>>8)+(g>>8)+(b>>8);
  fr=r&255;fg=g&255;fb=b&255;
 } else {
  unsigned r=axis[in[0]],g=axis[in[1]],b=axis[in[2]];
  p=nodes+3*((r&255)+n*((g&255)+n*(b&255)));
  fr=r>>8;fg=g>>8;fb=b>>8;
 }
 last=p+3+green_stride+blue_stride;
 if(fr>=fg) {
  if(fg>=fb){a=3;c=green_stride;w0=255-fr;w1=fr-fg;w2=fg-fb;w3=fb;}
  else if(fr>=fb){a=3;c=blue_stride;w0=255-fr;w1=fr-fb;w2=fb-fg;w3=fg;}
  else {a=blue_stride;c=3;w0=255-fb;w1=fb-fr;w2=fr-fg;w3=fg;}
 } else {
  if(fr>=fb){a=green_stride;c=3;w0=255-fg;w1=fg-fr;w2=fr-fb;w3=fb;}
  else if(fg>=fb){a=green_stride;c=blue_stride;w0=255-fg;w1=fg-fb;w2=fb-fr;w3=fr;}
  else {a=blue_stride;c=green_stride;w0=255-fb;w1=fb-fg;w2=fg-fr;w3=fr;}
 }
 q=p+a;v=q+c;
 out[0]=lut_channel(p,q,v,last,w0,w1,w2,w3);
 if(!mono) {
  out[1]=lut_channel(p+1,q+1,v+1,last+1,w0,w1,w2,w3);
  out[2]=lut_channel(p+2,q+2,v+2,last+2,w0,w1,w2,w3);
 } else out[1]=out[2]=out[0];   /* every node has R==G==B */
}
#endif /* CPU_LUT */
/* The ARM and host toolchains use arithmetic right shift for signed ints.
 * Subtract one for negatives to keep the original ties-away-from-zero rule. */
static int round16(int v){return (v+32768-(v<0))>>16;}
static unsigned char byte(int v){return v<0?0:v>255?255:v;}
static int signed_byte(unsigned char b){return b<128?b:(int)b-256;}
#if CPU_LUT
static inline __attribute__((always_inline)) void decode_chroma(
 const unsigned char *pair,const colour_tables *tables,int *red,int *green,int *blue) {
 unsigned u=pair[0],v=pair[2];
 if(tables) {
  *red=tables->v[v].direct;
  *green=round16(tables->u[u].green+tables->v[v].green);
  *blue=tables->u[u].direct;
 } else {
  int signed_u=signed_byte(u),signed_v=signed_byte(v);
  *red=round16(91881*signed_v);
  *green=round16(22553*signed_u+46802*signed_v);
  *blue=round16(116130*signed_u);
 }
}
static inline void rgb(unsigned char y,int red,int green,int blue,unsigned char out[3]) {
 out[0]=byte(y+red);
 out[1]=byte(y-green);
 out[2]=byte(y+blue);
}
static inline unsigned char rgb_luma(const unsigned char rgb_value[3]) {
 unsigned y=19595u*rgb_value[0]+38470u*rgb_value[1]+7471u*rgb_value[2];
 return (unsigned char)((y+32768u)>>16);
}
static inline unsigned char pair_chroma(int sum) {
 /* The original divide-by-two followed by signed Q16 rounding is
  * equivalent to rounding the undivided numerator in Q17. */
 int value=(sum+65536-(sum<0))>>17;
 return (unsigned char)(value>127?127:value);
}
static inline void encode_pair(unsigned char *pair,const unsigned char first[3],
 const unsigned char second[3]) {
 int green=first[1]+second[1];
 int red_difference=first[0]+second[0]-green;
 int blue_difference=first[2]+second[2]-green;
 pair[1]=rgb_luma(first);
 pair[3]=rgb_luma(second);
 pair[0]=pair_chroma(blue_difference*32768-red_difference*11059);
 pair[2]=pair_chroma(red_difference*32768-blue_difference*5329);
}
#endif /* CPU_LUT */

static int overlaps(const void *a,size_t na,const void *b,size_t nb) {
 size_t aa=(size_t)a,bb=(size_t)b;
 return aa<=bb?bb-aa<na:aa-bb<nb;
}
static int valid_row(const void *scratch,size_t capacity,size_t bytes,
 const void *image,size_t image_size,const void *lut,size_t lut_size) {
 return !scratch||(capacity>=bytes&&!overlaps(scratch,bytes,image,image_size)&&
                  !overlaps(scratch,bytes,lut,lut_size));
}

/* Rows are cached and graded in chunks of this many bytes, so the scratch
 * lines stay resident in the data cache between Canon copy-in and copy-out.
 * Scratch layout: [row chunk][colour tables][retro effect tables]. */
#define ROW_CHUNK 1024u
static size_t chunk_bytes(unsigned w){return w*2u<ROW_CHUNK?w*2u:ROW_CHUNK;}
static FILM_COMPACT colour_tables *colour_table_space(unsigned char *scratch,size_t capacity,
 size_t row_bytes,const void *image,size_t image_size,const void *lut,size_t lut_size,
 const void *work,size_t work_size) {
 size_t row_span=(row_bytes+3u)&~(size_t)3u;
 size_t used=row_span+sizeof(colour_tables);
 if(!scratch||((size_t)scratch&3u)||capacity<used||
    overlaps(scratch,used,image,image_size)||overlaps(scratch,used,lut,lut_size)||
    (work&&overlaps(scratch,used,work,work_size)))return 0;
 return (colour_tables*)(scratch+row_span);
}

#if CPU_LUT
/* For monochrome outputs, rgb_luma((g,g,g))==g and every chroma difference is
 * zero, so encode_pair reduces exactly to (0,g0,0,g1). */
static inline void encode_mono(unsigned char *pair,unsigned char g0,unsigned char g1) {
 pair[0]=0;pair[1]=g0;pair[2]=0;pair[3]=g1;
}
static FILM_COMPACT void prepare_colour_tables(unsigned n,unsigned short axis[256],colour_tables *tables) {
 unsigned i;
 for(i=0;i<256;i++) {
  unsigned t=i*(n-1),idx=(t+1+(t>>8))>>8,f=t-idx*255;
  if(idx==n-1){idx--;f=255;}
  if(tables) {
   unsigned red=3*idx,green=n*red,blue=n*green;
   tables->red[i]=(red<<8)|f;
   tables->green[i]=(green<<8)|f;
   tables->blue[i]=(blue<<8)|f;
   int signed_i=signed_byte(i);
   tables->u[i].green=22553*signed_i;
   tables->u[i].direct=round16(116130*signed_i);
   tables->v[i].green=46802*signed_i;
   tables->v[i].direct=round16(91881*signed_i);
  } else {
   axis[i]=idx|(f<<8);
  }
 }
}
#endif /* CPU_LUT */

/* Vignette and light-leak tables follow the colour tables in scratch. The
 * effects need the cached-row path; without room they are skipped and the
 * grade is still applied. */
static FILM_COMPACT int retro_setup(film_retro *retro,colour_tables *tables,unsigned char *scratch,size_t capacity,
 unsigned width,unsigned height,unsigned vignette,unsigned leaks,unsigned seed,int mono,
 const void *image,size_t image_size,const void *lut,size_t lut_size,const void *work,size_t work_size) {
 unsigned char *start;size_t need=film_retro_work_size(width),avail;
 if(vignette>3||leaks>3)return -1;
 if((!vignette&&!leaks)||!tables||!need)return 0;
 start=(unsigned char*)(tables+1);avail=capacity-(size_t)(start-scratch);
 if(start>scratch+capacity||avail<need||overlaps(start,need,image,image_size)||overlaps(start,need,lut,lut_size)||
    (work&&overlaps(start,need,work,work_size)))return 0;
 return film_retro_begin(retro,width,height,vignette,leaks,seed,mono,start,avail)>0;
}
/* Toy lens on input rows: its workspace must not overlap any other buffer. */
static FILM_COMPACT int toy_setup(film_toy *toy,unsigned level,void *toy_work,size_t toy_size,unsigned char *image,
 size_t image_size,unsigned width,unsigned height,unsigned stride,const void *lut,size_t lut_size,
 const void *scratch,size_t capacity,const void *work,size_t work_size) {
 if(level>3)return -1;
 if(!level)return 0;
 if(!toy_work||overlaps(toy_work,toy_size,image,image_size)||overlaps(toy_work,toy_size,lut,lut_size)||
    (scratch&&overlaps(toy_work,toy_size,scratch,capacity))||(work&&overlaps(toy_work,toy_size,work,work_size)))return -1;
 return film_toy_begin(toy,image,stride,width,height,level,toy_work,toy_size);
}
#if CPU_LUT
static inline __attribute__((always_inline)) void map_span(unsigned char *pixels,unsigned w,
 const void *lut,unsigned n,const unsigned short *axis,const colour_tables *tables,int mono) {
 const node_t *nodes=(const node_t*)((const unsigned char*)lut+16);
 for(unsigned col=0;col<w;col+=2) {
  unsigned char *p=pixels+col*2,in[3],a[3],b[3];
  int red,green,blue;
  decode_chroma(p,tables,&red,&green,&blue);
  rgb(p[1],red,green,blue,in);fast_pixel(nodes,n,axis,tables,in,a,mono);
  /* The pair shares input chroma, so equal luma gives equal RGB. */
  if(p[1]==p[3]) {
   b[0]=a[0];b[1]=a[1];b[2]=a[2];
  } else {
   rgb(p[3],red,green,blue,in);fast_pixel(nodes,n,axis,tables,in,b,mono);
  }
  if(mono)encode_mono(p,a[0],b[0]);else encode_pair(p,a,b);
 }
}
#endif /* CPU_LUT */
/* Rows in place: with the frame in cached memory (the stage's FRAMECACHE),
 * copying each chunk to scratch and back only costs time, so the passes work
 * on the frame itself, a whole row per span. The toy lens reads the original
 * current row from the frame, so with it the rows are still copied. */
static int rows_in_place;
void film_colour_rows_in_place(int on){rows_in_place=on!=0;}
/* lut == NULL: Canon's hardware colour table grades the frame, when it
 * encodes the JPEG after these effects; graded_mono says whether the look is
 * monochrome. Only the effects run. */
static int map_cached(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned x,unsigned y,unsigned w,unsigned h,const void *lut,
 unsigned char *scratch,size_t capacity,void (*progress)(unsigned,unsigned),
 unsigned vignette,unsigned leaks,unsigned seed,unsigned toy_level,void *toy_work,size_t toy_size,
 const film_print_settings *print,int graded_mono) {
 unsigned row,col,n;int mono=graded_mono!=0,effects,lens,printing;film_retro retro;film_toy toy;film_print prt;
 if(!image||width>FILM_MAX_WIDTH||!width||!height||width%2||stride<width*2||
    height>size/stride||x%2||w%2||!w||!h||x>width||w>width-x||y>height||h>height-y)return -1;
#if CPU_LUT
 unsigned short axis[256];
 n=lut?prepared_lattice(lut,&mono):2;
 size_t lut_size=lut?16u+n*n*n*6u:0;
#else
 const size_t lut_size=0;n=2;           /* the camera's frame is graded: lut is 0 */
#endif
 if(!n||!valid_row(scratch,capacity,w*2,image,size,lut,lut_size))return -1;
 colour_tables *tables=colour_table_space(scratch,capacity,chunk_bytes(w),image,size,lut,lut_size,0,0);
#if CPU_LUT
 prepare_colour_tables(n,axis,tables);
#endif
 effects=retro_setup(&retro,tables,scratch,capacity,width,height,vignette,leaks,seed,mono,image,size,lut,lut_size,0,0);
 if(effects<0)return -1;
 lens=toy_setup(&toy,toy_level,toy_work,toy_size,image,size,width,height,stride,lut,lut_size,scratch,capacity,0,0);
 if(lens<0)return -1;
 printing=film_print_begin(&prt,width,height,print,seed,mono);
 if(printing<0)return -1;
 if(!lut&&!effects&&!lens&&!printing)return 0;             /* nothing to do on a graded frame */
 int copy=scratch&&!rows_in_place;
 unsigned chunk=copy?ROW_CHUNK/2:w;
 for(row=y;row<y+h;row++) {
  for(col=0;col<w;col+=chunk) {
   unsigned span=w-col<chunk?w-col:chunk;
   unsigned char *source=image+(size_t)row*stride+(x+col)*2;
   unsigned char *pixels=copy?scratch:source;
   if(copy)memcpy(scratch,source,span*2);
   if(lens)film_toy_row(&toy,row,pixels,x+col,span);
#if CPU_LUT
   if(lut)map_span(pixels,span,lut,n,axis,tables,mono);
#endif
   if(effects)film_retro_row(&retro,row,pixels,x+col,span);
   if(printing)film_print_row(&prt,row,pixels,x+col,span);
   if(copy)memcpy(source,scratch,span*2);
  }
  if(progress&&((row-y)%32==31||row+1==y+h))progress(row+1-y,h);
 }
 return 0;
}
#if CPU_LUT /* host reference: the CPU LUT passes */
int film_uyvy_map_cached(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned x,unsigned y,unsigned w,unsigned h,const void *lut,
 unsigned char *scratch,size_t capacity,void (*progress)(unsigned,unsigned)) {
 if(!lut)return -1;
 return map_cached(image,size,width,height,stride,x,y,w,h,lut,scratch,capacity,progress,0,0,0,0,0,0,0,0);
}
int film_uyvy_map_cached_retro(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,const void *lut,unsigned char *scratch,size_t capacity,
 void (*progress)(unsigned,unsigned),unsigned vignette,unsigned leaks,unsigned seed,
 unsigned toy,void *toy_work,size_t toy_size,const film_print_settings *print) {
 if(!lut)return -1;
 return map_cached(image,size,width,height,stride,0,0,width,height,lut,scratch,capacity,progress,vignette,leaks,seed,toy,toy_work,toy_size,print,0);
}
int film_uyvy_map(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned x,unsigned y,unsigned w,unsigned h,const void *lut,void (*progress)(unsigned,unsigned)) {
 return film_uyvy_map_cached(image,size,width,height,stride,x,y,w,h,lut,0,0,progress);
}
int film_uyvy_patch(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,unsigned x,unsigned y,unsigned w,unsigned h,const void *lut) {
 return film_uyvy_map(image,size,width,height,stride,x,y,w,h,lut,0);
}
#endif

/* Optional spatial halation approximation. Highlight energy comes from the
 * original display-referred luma; a half-resolution rolling box mask is evaluated before
 * each row is graded. Linearized red/green additions then pass through the
 * selected LUT, preserving monochrome looks. No file I/O or allocation here. */
static const unsigned short halo_linear[256]={
 0,20,40,60,80,99,119,139,159,179,199,219,241,264,288,313,
 340,367,396,427,458,491,526,562,599,637,677,718,761,805,851,898,
 947,997,1048,1101,1156,1212,1270,1330,1391,1453,1517,1583,1651,1720,1790,1863,
 1937,2013,2090,2170,2250,2333,2418,2504,2592,2681,2773,2866,2961,3058,3157,3258,
 3360,3464,3570,3678,3788,3900,4014,4129,4247,4366,4488,4611,4736,4864,4993,5124,
 5257,5392,5530,5669,5810,5953,6099,6246,6395,6547,6700,6856,7014,7174,7335,7500,
 7666,7834,8004,8177,8352,8528,8708,8889,9072,9258,9445,9635,9828,10022,10219,10417,
 10619,10822,11028,11235,11446,11658,11873,12090,12309,12530,12754,12980,13209,13440,13673,13909,
 14146,14387,14629,14874,15122,15371,15623,15878,16135,16394,16656,16920,17187,17456,17727,18001,
 18277,18556,18837,19121,19407,19696,19987,20281,20577,20876,21177,21481,21787,22096,22407,22721,
 23038,23357,23678,24002,24329,24658,24990,25325,25662,26001,26344,26688,27036,27386,27739,28094,
 28452,28813,29176,29542,29911,30282,30656,31033,31412,31794,32179,32567,32957,33350,33745,34143,
 34544,34948,35355,35764,36176,36591,37008,37429,37852,38278,38706,39138,39572,40009,40449,40891,
 41337,41785,42236,42690,43147,43606,44069,44534,45002,45473,45947,46423,46903,47385,47871,48359,
 48850,49344,49841,50341,50844,51349,51858,52369,52884,53401,53921,54445,54971,55500,56032,56567,
 57105,57646,58190,58737,59287,59840,60396,60955,61517,62082,62650,63221,63795,64372,64952,65535,
};
static const unsigned char halo_highlight[256]={
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,6,11,17,23,28,34,40,45,51,57,62,68,74,
 79,85,91,96,102,108,113,119,125,130,136,142,147,153,159,164,
 170,176,181,187,193,198,204,210,215,221,227,232,238,244,249,255,
};

/* Radius is in full-resolution pixels; only the highlight mask is 2x2
 * downsampled. All RGB additions and LUT calculations remain full resolution.
 * The mask is blurred by three box filters in a row along each axis, each a
 * third of the radius wide: together close to a Gaussian, so a small light
 * gets a round glow that fades out (one box per axis made a flat square). */
unsigned film_halo_radius(unsigned level){static const unsigned v[4]={0,36,54,72};return level<4?v[level]:0;}
unsigned film_halo_strength(unsigned level){static const unsigned v[4]={0,48,72,96};return level<4?v[level]:0;}
#define HALO_STAGES 3u
#define HALO_MAX_BOX 12u                    /* High: 72 px = 36 mask cells = 3 x 12 */
/* One box's radius in mask cells: a sixth of the radius in pixels. */
static unsigned halo_box(unsigned level){return film_halo_radius(level)/(2u*HALO_STAGES);}
/* The tail: CineStill-style halation has a wide, faint glow beyond the
 * core's. It is the same highlight mask on a coarse grid of 16 x 16 pixel
 * cells (each the mean of every other row's 16 pixels), blurred by three box
 * filters per axis of 4, 6 or 8 cells (Low, Mid, High: 64-128 px each, about
 * five times the core's reach), and blended into each mask row as
 * max(core, 0.4 core + 0.6 tail), the tail interpolated between cell
 * centres. The coarse map has room for frames no taller than wide (every
 * G12 shot is 4:3 landscape); a taller frame gets the core alone. */
#define HALO_TAIL_CELL 16u
static unsigned halo_tail_box(unsigned level){static const unsigned v[4]={0,4,6,8};return level<4?v[level]:0;}
static size_t halo_core_bytes(size_t grid,size_t box) {
 /* Three rings of win rows, three u16 sums per column, tmp, current, next,
  * and the padded row the horizontal boxes run in. */
 return ((grid*(2u*box+1u)*HALO_STAGES+1u)&~(size_t)1u)+grid*(2u*HALO_STAGES+3u)+grid+2u*HALO_STAGES*box;
}
/* The tail's coarse map (tw x tw), u16 sums per cell column, the padded line
 * the boxes run in, the interpolated cell row, the tail at every mask column
 * and the blended mask row. */
static size_t halo_tail_bytes(unsigned width,unsigned level) {
 size_t tw=(width+HALO_TAIL_CELL-1u)/HALO_TAIL_CELL;
 return tw*tw+2u*tw+tw+2u*HALO_STAGES*halo_tail_box(level)+tw+2u*(width/2u);
}
size_t film_halo_level_work_size(unsigned width,unsigned level) {
 if(!width||width>FILM_MAX_WIDTH||width%2||!level||level>3)return 0;
 return ((halo_core_bytes(width/2u,halo_box(level))+1u)&~(size_t)1u)+halo_tail_bytes(width,level);
}
size_t film_halo_work_size(unsigned width){return film_halo_level_work_size(width,3);}
typedef struct {
 unsigned char *ring,*tmp,*current,*next,*row;unsigned short *sum;
 unsigned width,height,stride,grid_width,grid_height,center,radius,box,win,strength,blend;
 unsigned inv;                                    /* windows other than 13, 19, 25: ceil(2^32/win) */
 unsigned tint_red,tint_green,tint_blue;          /* graded frames: Q8 gains on the red energy */
 unsigned tint_luma;                              /* mono looks: the gains' luma (all three alike) */
 const unsigned char *highlight;                  /* mask weight per luma */
 const unsigned char *image;
 /* Most of a frame has no highlight near it, so most of the mask is zero.
  * Per vertical stage: slot, the ring row the next row replaces;
  * ring_nz[slot], that ring row has a nonzero value; nz_rows, how many do
  * (none: every sum of the stage is zero). current_zero/next_zero: those
  * mask rows are all zero. */
 unsigned slot[HALO_STAGES],nz_rows[HALO_STAGES];unsigned char ring_nz[HALO_STAGES][2u*HALO_MAX_BOX+1u];
 int current_zero,next_zero;
 int fast;                                        /* fast halation: the mask from row 0 of each 2x2 block */
 /* Coarse halation (film_halo_coarse, the G10): the core mask in 4x4 cells
  * (grid_width = width/4) with half the box, expanded per row to one sample
  * per pixel pair (pair_width = width/2) in expand, where the tail and the
  * blend work as before. Otherwise pair_width == grid_width. */
 int coarse;unsigned pair_width;unsigned char *expand;
 /* The tail (tail_on): the blurred coarse map tail_w x tail_h, its box radius
  * and reciprocal, and per row the interpolated cell row and blended mask. */
 int tail_on;
 unsigned tail_w,tail_h,tail_box,tail_win,tail_inv;
 unsigned char *tail_map,*tail_line,*tail_row,*tail_grid,*comb;unsigned short *tail_sum;
 int comb_zero;                                   /* comb holds only zeros */
 int tail_zero;                                   /* this row pair has no tail */
 unsigned char tail_scale[256],core_scale[256];   /* 0.6 x tail and 0.4 x core, each rounded */
} halo_rows;
/* FASTHALO.TXT (film_halo_fast): the mask averages the top row of each 2x2
 * block instead of all four pixels, halving the frame it reads. Not the
 * same bytes. Taken when a pass starts. */
static int halo_fast_requested;
void film_halo_fast(int on){halo_fast_requested=on!=0;}
static int halo_coarse_requested;
void film_halo_coarse(int on){halo_coarse_requested=on!=0;}
/* Frame scale (film_halo_scale), Q16: 65536 is L. A smaller frame stands for
 * L shrunk, so the glow's radius shrinks with it. Taken when a pass starts. */
static unsigned halo_scale=65536u;
void film_halo_scale(unsigned scale){halo_scale=scale>=65536u||!scale?65536u:scale;}
/* The mask loops are instantiated for each box window of L (13, 19, 25) so
 * every division is by a constant, which the compiler turns into a multiply.
 * The scaled windows of smaller frames (win 0 here: the window in c)
 * multiply by the reciprocal, which is exact: value < 256*win and win <= 25. */
static inline __attribute__((always_inline)) unsigned halo_div(unsigned value,unsigned win,unsigned inv) {
 if(win==7)return value/7u;
 if(win==9)return value/9u;
 if(win==13)return value/13u;
 if(win==19)return value/19u;
 if(win==25)return value/25u;
 return (unsigned)(((unsigned long long)value*inv)>>32);
}
/* One box in place: e holds n+win-1 values, and e[p] becomes the rounded
 * mean of the win values from e[p] on, for p < n. */
static inline __attribute__((always_inline)) void halo_box_pass(unsigned char *e,unsigned n,const unsigned win,unsigned wn,unsigned inv) {
 unsigned s=0,k,p;
 for(k=0;k<wn;k++)s+=e[k];
 for(p=0;;p++) {
  unsigned first=e[p];
  e[p]=(unsigned char)halo_div(s+wn/2,win,inv);
  if(p+1>=n)break;
  s+=e[p+wn]-first;                                /* never below zero: s holds first */
 }
}
/* One grid row of the mask: the 2x2 highlight average of image rows 2*row and
 * 2*row+1 goes into c->row past 3*box edge copies of its first value, with
 * as many of its last after it; the three horizontal boxes then leave the
 * blurred row in c->row[0, grid_width). Returns 0, with c->row's blurred
 * part not written, when the mask row is all zero. */
static inline __attribute__((always_inline)) int halo_mask_row(halo_rows *c,unsigned row,const unsigned win,const int fast) {
 unsigned x,y0=row*2,y1=y0+1,gw=c->grid_width,pad=HALO_STAGES*c->box,any=0;
 const unsigned wn=win?win:c->win,inv=c->inv;
 unsigned char *m=c->row+pad;
 if(c->coarse) {
  /* A 4x4 cell: the highlight mean of its two middle rows' four pixels
   * (eight samples), or of one row with fast. Rows past the frame repeat
   * its last row; a last half cell repeats its pair. */
  unsigned r0=row*4+1,r1=row*4+2,pairs=c->width/2;
  if(r0>=c->height)r0=c->height-1;
  if(r1>=c->height)r1=c->height-1;
  const unsigned *wa=(const unsigned*)(c->image+(size_t)r0*c->stride),*wb=(const unsigned*)(c->image+(size_t)r1*c->stride);
  const unsigned char *hl=c->highlight;
  for(x=0;x<gw;x++) {
   unsigned i=2*x,j=i+1<pairs?i+1:i,p=wa[i],q=wa[j],t;
   t=hl[(p>>8)&255]+hl[p>>24]+hl[(q>>8)&255]+hl[q>>24];
   if(fast)t=(t+2u)/4u;
   else{unsigned u=wb[i],v=wb[j];t=(t+hl[(u>>8)&255]+hl[u>>24]+hl[(v>>8)&255]+hl[v>>24]+4u)/8u;}
   m[x]=(unsigned char)t;any|=t;
  }
  if(!any)return 0;
  memset(c->row,m[0],pad);memset(m+gw,m[gw-1],pad);
  for(unsigned stage=HALO_STAGES;stage--;)halo_box_pass(c->row,gw+2u*c->box*stage,win,wn,inv);
  return 1;
 }
 if(y1>=c->height)y1=c->height-1;
 const unsigned char *a=c->image+(size_t)y0*c->stride,*b=c->image+(size_t)y1*c->stride,*hl=c->highlight;
 /* One aligned 32-bit read per UYVY pair (U,Y0,V,Y1 little-endian) instead of
  * two byte reads. */
 if(!(((size_t)a|(size_t)b)&3u)) {
  const unsigned *wa=(const unsigned*)a,*wb=(const unsigned*)b;
  if(fast)for(x=0;x<gw;x++){unsigned p=wa[x],t=(hl[(p>>8)&255]+hl[p>>24]+1u)/2u;m[x]=(unsigned char)t;any|=t;}
  else for(x=0;x<gw;x++) {
   unsigned p=wa[x],q=wb[x],t=(hl[(p>>8)&255]+hl[p>>24]+hl[(q>>8)&255]+hl[q>>24]+2u)/4u;
   m[x]=(unsigned char)t;any|=t;
  }
 } else if(fast)
 for(x=0;x<gw;x++){unsigned t=(hl[a[x*4+1]]+hl[a[x*4+3]]+1u)/2u;m[x]=(unsigned char)t;any|=t;}
 else
 for(x=0;x<gw;x++){unsigned t=(hl[a[x*4+1]]+hl[a[x*4+3]]+hl[b[x*4+1]]+hl[b[x*4+3]]+2u)/4u;m[x]=(unsigned char)t;any|=t;}
 if(!any)return 0;
 memset(c->row,m[0],pad);memset(m+gw,m[gw-1],pad);
 for(unsigned stage=HALO_STAGES;stage--;)halo_box_pass(c->row,gw+2u*c->box*stage,win,wn,inv);
 return 1;
}
/* One vertical box: in (0: a zero row) replaces stage s's oldest ring row
 * and out gets the stage's blurred row. Returns 1 when that row is all zero;
 * out is then not necessarily written. */
static inline __attribute__((always_inline)) int halo_stage(halo_rows *c,unsigned s,const unsigned char *in,
 unsigned char *out,const unsigned win) {
 unsigned x,gw=c->grid_width,slot=c->slot[s],old=c->ring_nz[s][slot],dnz=0,onz=0;
 const unsigned wn=win?win:c->win,inv=c->inv;
 unsigned char *dst=c->ring+((size_t)s*c->win+slot)*gw;unsigned short *vs=c->sum+(size_t)s*gw;
 if(++c->slot[s]==c->win)c->slot[s]=0;
 if(!in&&!old) {                                  /* a zero row over a zero row: the sums stay */
  if(!c->nz_rows[s])return 1;
  for(x=0;x<gw;x++){unsigned o=halo_div(vs[x]+wn/2,win,inv);out[x]=(unsigned char)o;onz|=o;}
  return !onz;
 }
 /* vs never exceeds win x 255 and always includes dst, so total needs no
  * 16-bit wrap. */
 if(in)for(x=0;x<gw;x++) {
  unsigned v=in[x],total=vs[x]-dst[x]+v,o=halo_div(total+wn/2,win,inv);
  vs[x]=(unsigned short)total;dst[x]=(unsigned char)v;dnz|=v;out[x]=(unsigned char)o;onz|=o;
 } else for(x=0;x<gw;x++) {
  unsigned total=vs[x]-dst[x],o=halo_div(total+wn/2,win,inv);
  vs[x]=(unsigned short)total;dst[x]=0;out[x]=(unsigned char)o;onz|=o;
 }
 c->nz_rows[s]=c->nz_rows[s]+(dnz!=0)-old;c->ring_nz[s][slot]=dnz!=0;
 return !onz;
}
/* All three vertical boxes in one pass over the columns, once the rings are
 * full: the same sums as three halo_stage calls, without writing the rows
 * between them. A zero row into rings that hold only zeros changes nothing. */
static inline __attribute__((always_inline)) int halo_stages(halo_rows *c,const unsigned char *in,
 unsigned char *out,int out_zero,const unsigned win) {
 unsigned x,gw=c->grid_width,s,n1=0,n2=0,n3=0,onz=0;
 const unsigned wn=win?win:c->win,inv=c->inv,half=wn/2;
 if(!in&&!c->nz_rows[0]&&!c->nz_rows[1]&&!c->nz_rows[2]) {
  for(s=0;s<HALO_STAGES;s++)if(++c->slot[s]==c->win)c->slot[s]=0;
  if(out&&!out_zero)memset(out,0,gw);
  return 1;
 }
 unsigned char *r1=c->ring+(size_t)c->slot[0]*gw,*r2=c->ring+((size_t)c->win+c->slot[1])*gw,*r3=c->ring+((size_t)2*c->win+c->slot[2])*gw;
 unsigned short *s1=c->sum,*s2=s1+gw,*s3=s2+gw;unsigned char *o=out?out:c->tmp;
 for(x=0;x<gw;x++) {
  unsigned v=in?in[x]:0,t1=s1[x]-r1[x]+v,a=halo_div(t1+half,win,inv);
  unsigned t2=s2[x]-r2[x]+a,b=halo_div(t2+half,win,inv);
  unsigned t3=s3[x]-r3[x]+b,y=halo_div(t3+half,win,inv);
  s1[x]=(unsigned short)t1;s2[x]=(unsigned short)t2;s3[x]=(unsigned short)t3;
  r1[x]=(unsigned char)v;r2[x]=(unsigned char)a;r3[x]=(unsigned char)b;o[x]=(unsigned char)y;
  n1|=v;n2|=a;n3|=b;onz|=y;
 }
 const unsigned nz[HALO_STAGES]={n1!=0,n2!=0,n3!=0};
 for(s=0;s<HALO_STAGES;s++) {
  unsigned slot=c->slot[s];
  c->nz_rows[s]=c->nz_rows[s]+nz[s]-c->ring_nz[s][slot];c->ring_nz[s][slot]=(unsigned char)nz[s];
  if(++c->slot[s]==c->win)c->slot[s]=0;
 }
 return !onz;
}
/* Mask row `row` through the horizontal boxes and the first `depth` vertical
 * ones (fewer only while the rings fill). With out, the third vertical box's
 * row goes there; out_zero: out's bytes are already all zero, so a zero row
 * needs no clearing. Returns 1 when the last box's row is all zero. */
static inline __attribute__((always_inline)) int halo_push_win(halo_rows *c,unsigned row,unsigned depth,
 unsigned char *out,int out_zero,const unsigned win,const int fast) {
 int zero=!halo_mask_row(c,row,win,fast);
 if(depth==HALO_STAGES)return halo_stages(c,zero?0:c->row,out,out_zero,win);
 zero=halo_stage(c,0,zero?0:c->row,c->tmp,win);
 if(depth<2)return zero;
 zero=halo_stage(c,1,zero?0:c->tmp,c->row,win);
 if(depth<3)return zero;
 zero=halo_stage(c,2,zero?0:c->row,out?out:c->tmp,win);
 if(zero&&out&&!out_zero)memset(out,0,c->grid_width);
 return zero;
}
static int halo_push(halo_rows *c,unsigned row,unsigned depth,unsigned char *out,int out_zero) {
 if(c->fast)switch(c->win){
  case 7:return halo_push_win(c,row,depth,out,out_zero,7,1);
  case 9:return halo_push_win(c,row,depth,out,out_zero,9,1);
  case 13:return halo_push_win(c,row,depth,out,out_zero,13,1);
  case 19:return halo_push_win(c,row,depth,out,out_zero,19,1);
  case 25:return halo_push_win(c,row,depth,out,out_zero,25,1);
  default:return halo_push_win(c,row,depth,out,out_zero,0,1);
 }
 switch(c->win){
  case 7:return halo_push_win(c,row,depth,out,out_zero,7,0);
  case 9:return halo_push_win(c,row,depth,out,out_zero,9,0);
  case 13:return halo_push_win(c,row,depth,out,out_zero,13,0);
  case 19:return halo_push_win(c,row,depth,out,out_zero,19,0);
  case 25:return halo_push_win(c,row,depth,out,out_zero,25,0);
  default:return halo_push_win(c,row,depth,out,out_zero,0,0);
 }
}
static void halo_prepare_odd(halo_rows *c) {
 if(c->center+1<c->grid_height) {
  unsigned incoming=++c->center+c->radius;
  c->next_zero=halo_push(c,incoming>=c->grid_height?c->grid_height-1:incoming,HALO_STAGES,c->next,c->next_zero);
 } else {memcpy(c->next,c->current,c->grid_width);c->next_zero=c->current_zero;}
}
/* out[i] = (a[i]+b[i]+1)/2. Four bytes per word where all three are aligned:
 * (a|b) - (((a^b)>>1) & 0x7f7f7f7f) is that rounded-up average per byte. */
static void halo_average(unsigned char *restrict out,const unsigned char *restrict a,const unsigned char *restrict b,unsigned n) {
 unsigned i=0;
 if(!(((size_t)out|(size_t)a|(size_t)b)&3u)) {
  unsigned *o=(unsigned*)out;const unsigned *x=(const unsigned*)a,*y=(const unsigned*)b;
  for(;i+4<=n;i+=4,o++,x++,y++){unsigned p=*x,q=*y;*o=(p|q)-(((p^q)>>1)&0x7f7f7f7fu);}
 }
 for(;i<n;i++)out[i]=(unsigned char)((a[i]+b[i]+1)/2);
}
/* Coarse halation: out = a + (b - a) * phase / 4, rounded, phase 1..3. */
static void halo_lerp4(unsigned char *out,const unsigned char *a,const unsigned char *b,unsigned n,unsigned phase) {
 unsigned i,wa=4u-phase;
 for(i=0;i<n;i++)out[i]=(unsigned char)((a[i]*wa+b[i]*phase+2u)>>2);
}
/* Coarse halation: one sample per pixel pair from a row of 4x4 cells. Pair
 * 2k lies a quarter cell left of cell k's centre, pair 2k+1 a quarter right:
 * 3/4 of the cell and 1/4 of its neighbour (edge cells repeated). */
static void halo_expand(unsigned char *out,const unsigned char *m,unsigned cells,unsigned pairs) {
 unsigned k;
 for(k=0;k<cells;k++) {
  unsigned v=3u*m[k],prev=k?m[k-1]:m[k],next=k+1<cells?m[k+1]:m[k];
  if(2*k<pairs)out[2*k]=(unsigned char)((v+prev+2u)>>2);
  if(2*k+1<pairs)out[2*k+1]=(unsigned char)((v+next+2u)>>2);
 }
}
/* The tail's coarse map: each cell the rounded mean highlight weight of 16
 * of its pixels, rows 0, 4, 8, 12 by columns 0, 4, 8, 12 (a sample past the
 * frame's edge repeats its last row or column: a wide glow needs no finer
 * look, and the core still sees every pixel); then three boxes along each
 * row and three along each column, edge cells repeated. */
static void halo_tail_build(halo_rows *c) {
 unsigned tw=c->tail_w,th=c->tail_h,i,j,r,x,box=c->tail_box,pad=HALO_STAGES*box,win=c->tail_win;
 const unsigned char *hl=c->highlight;unsigned short *sum=c->tail_sum;unsigned char *line=c->tail_line;
 for(j=0;j<th;j++) {
  for(i=0;i<tw;i++)sum[i]=0;
  for(r=0;r<HALO_TAIL_CELL;r+=4) {
   unsigned y=j*HALO_TAIL_CELL+r;
   const unsigned char *row=c->image+(size_t)(y<c->height?y:c->height-1)*c->stride+1;
   for(x=0;x<c->width;x+=4)sum[x>>4]+=hl[row[2u*x]];
   for(;x<tw*HALO_TAIL_CELL;x+=4)sum[tw-1]+=hl[row[2u*(c->width-1u)]];
  }
  for(i=0;i<tw;i++)c->tail_map[(size_t)j*tw+i]=(unsigned char)((sum[i]+8u)>>4);
 }
 for(j=0;j<th;j++) {
  unsigned char *m=c->tail_map+(size_t)j*tw;
  memset(line,m[0],pad);memcpy(line+pad,m,tw);memset(line+pad+tw,m[tw-1],pad);
  for(unsigned stage=HALO_STAGES;stage--;)halo_box_pass(line,tw+2u*box*stage,0,win,c->tail_inv);
  memcpy(m,line,tw);
 }
 for(i=0;i<tw;i++) {
  for(j=0;j<th;j++)line[pad+j]=c->tail_map[(size_t)j*tw+i];
  memset(line,line[pad],pad);memset(line+pad+th,line[pad+th-1],pad);
  for(unsigned stage=HALO_STAGES;stage--;)halo_box_pass(line,th+2u*box*stage,0,win,c->tail_inv);
  for(j=0;j<th;j++)c->tail_map[(size_t)j*tw+i]=line[j];
 }
}
/* Position p (in 1/16 cell, from cell 0's centre) between cells: the cell
 * before it and the weight of the one after; clamped at both ends. */
static inline __attribute__((always_inline)) unsigned halo_tail_at(int p,unsigned n,unsigned *f) {
 unsigned i;
 if(p<=0){*f=0;return 0;}
 i=(unsigned)p>>4;*f=(unsigned)p&15u;
 if(i+1u>=n){*f=0;return n-1u;}
 return i;
}
/* The blended mask for image row y into c->comb: core (0: a zero row) with
 * the tail at each mask column x (pixel 2x), max(core, 0.4 core + 0.6 tail),
 * each part rounded on its own. The tail is interpolated once
 * per row pair, at its even row: between the cell rows around it, then
 * across each cell (eight mask columns, from the 1/16 position 2x - 7 past
 * cell 0's centre) by stepping the weighted sum. Returns 0 when the blend
 * is all zero. */
static int halo_tail_combine(halo_rows *c,unsigned y,const unsigned char *core) {
 unsigned gw=c->pair_width,x,any=0;
 unsigned char *t=c->tail_grid,*o=c->comb;const unsigned char *ts=c->tail_scale;
 if(!(y&1u)) {
  unsigned tw=c->tail_w,i,g,rany=0,j=halo_tail_at((int)y-7,c->tail_h,&g);
  const unsigned char *a=c->tail_map+(size_t)j*tw,*b=g?a+tw:a;unsigned char *r=c->tail_row;
  for(i=0;i<tw;i++){unsigned v=(a[i]*(16u-g)+b[i]*g+8u)>>4;r[i]=(unsigned char)v;rany|=v;}
  c->tail_zero=!rany;
  if(rany) {
   unsigned first=gw<4u?gw:4u;
   for(x=0;x<first;x++)t[x]=r[0];                 /* left of cell 0's centre */
   for(i=0;i+1u<tw&&x<gw;i++) {                   /* x = 8i+4 .. 8i+11: positions 1, 3, .. 15 past cell i */
    int d=(int)r[i+1]-(int)r[i],acc=16*(int)r[i]+8+d;unsigned end=8u*i+12u<gw?8u*i+12u:gw;
    for(;x<end;x++){t[x]=(unsigned char)(acc>>4);acc+=2*d;}
   }
   for(;x<gw;x++)t[x]=r[tw-1];                    /* right of the last cell's centre */
  }
 }
 if(c->tail_zero) {
  if(!core){if(!c->comb_zero){memset(o,0,gw);c->comb_zero=1;}return 0;}
  for(x=0;x<gw;x++){o[x]=core[x];any|=core[x];}
  c->comb_zero=!any;return any!=0;
 }
 if(!core)for(x=0;x<gw;x++){unsigned v=ts[t[x]];o[x]=(unsigned char)v;any|=v;}
 else {
  const unsigned char *cs=c->core_scale;
  for(x=0;x<gw;x++){unsigned m=core[x],v=cs[m]+ts[t[x]];v=v<m?m:v;o[x]=(unsigned char)v;any|=v;}
 }
 c->comb_zero=!any;return any!=0;
}
/* Quantized linear->sRGB inverse: at most one code value from nearest
 * linear16 inversion, exhaustively checked over all 65536 inputs. */
static const unsigned char halo_encode[4097]={
 0,1,2,2,3,4,5,6,6,7,8,9,10,10,11,12,13,13,14,15,15,16,16,17,18,18,19,19,20,20,21,21,
 22,22,23,23,23,24,24,25,25,25,26,26,27,27,27,28,28,29,29,29,30,30,30,31,31,31,32,32,32,33,33,33,
 34,34,34,34,35,35,35,36,36,36,37,37,37,37,38,38,38,38,39,39,39,39,40,40,40,41,41,41,41,42,42,42,
 42,43,43,43,43,43,44,44,44,44,45,45,45,45,46,46,46,46,46,47,47,47,47,48,48,48,48,48,49,49,49,49,
 49,50,50,50,50,50,51,51,51,51,51,52,52,52,52,52,53,53,53,53,53,54,54,54,54,54,55,55,55,55,55,55,
 56,56,56,56,56,57,57,57,57,57,57,58,58,58,58,58,58,59,59,59,59,59,59,60,60,60,60,60,60,61,61,61,
 61,61,61,62,62,62,62,62,62,63,63,63,63,63,63,64,64,64,64,64,64,64,65,65,65,65,65,65,66,66,66,66,
 66,66,66,67,67,67,67,67,67,67,68,68,68,68,68,68,68,69,69,69,69,69,69,69,70,70,70,70,70,70,70,71,
 71,71,71,71,71,71,72,72,72,72,72,72,72,72,73,73,73,73,73,73,73,74,74,74,74,74,74,74,74,75,75,75,
 75,75,75,75,75,76,76,76,76,76,76,76,77,77,77,77,77,77,77,77,77,78,78,78,78,78,78,78,78,79,79,79,
 79,79,79,79,79,80,80,80,80,80,80,80,80,81,81,81,81,81,81,81,81,81,82,82,82,82,82,82,82,82,83,83,
 83,83,83,83,83,83,83,84,84,84,84,84,84,84,84,84,85,85,85,85,85,85,85,85,85,86,86,86,86,86,86,86,
 86,86,87,87,87,87,87,87,87,87,87,87,88,88,88,88,88,88,88,88,88,89,89,89,89,89,89,89,89,89,90,90,
 90,90,90,90,90,90,90,90,91,91,91,91,91,91,91,91,91,91,92,92,92,92,92,92,92,92,92,92,93,93,93,93,
 93,93,93,93,93,93,94,94,94,94,94,94,94,94,94,94,95,95,95,95,95,95,95,95,95,95,96,96,96,96,96,96,
 96,96,96,96,96,97,97,97,97,97,97,97,97,97,97,98,98,98,98,98,98,98,98,98,98,98,99,99,99,99,99,99,
 99,99,99,99,99,100,100,100,100,100,100,100,100,100,100,100,101,101,101,101,101,101,101,101,101,101,101,102,102,102,102,102,
 102,102,102,102,102,102,103,103,103,103,103,103,103,103,103,103,103,103,104,104,104,104,104,104,104,104,104,104,104,105,105,105,
 105,105,105,105,105,105,105,105,105,106,106,106,106,106,106,106,106,106,106,106,106,107,107,107,107,107,107,107,107,107,107,107,
 107,108,108,108,108,108,108,108,108,108,108,108,108,109,109,109,109,109,109,109,109,109,109,109,109,110,110,110,110,110,110,110,
 110,110,110,110,110,111,111,111,111,111,111,111,111,111,111,111,111,111,112,112,112,112,112,112,112,112,112,112,112,112,112,113,
 113,113,113,113,113,113,113,113,113,113,113,114,114,114,114,114,114,114,114,114,114,114,114,114,115,115,115,115,115,115,115,115,
 115,115,115,115,115,116,116,116,116,116,116,116,116,116,116,116,116,116,117,117,117,117,117,117,117,117,117,117,117,117,117,117,
 118,118,118,118,118,118,118,118,118,118,118,118,118,119,119,119,119,119,119,119,119,119,119,119,119,119,119,120,120,120,120,120,
 120,120,120,120,120,120,120,120,120,121,121,121,121,121,121,121,121,121,121,121,121,121,121,122,122,122,122,122,122,122,122,122,
 122,122,122,122,122,123,123,123,123,123,123,123,123,123,123,123,123,123,123,124,124,124,124,124,124,124,124,124,124,124,124,124,
 124,125,125,125,125,125,125,125,125,125,125,125,125,125,125,125,126,126,126,126,126,126,126,126,126,126,126,126,126,126,127,127,
 127,127,127,127,127,127,127,127,127,127,127,127,127,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,129,129,129,129,
 129,129,129,129,129,129,129,129,129,129,129,130,130,130,130,130,130,130,130,130,130,130,130,130,130,130,131,131,131,131,131,131,
 131,131,131,131,131,131,131,131,131,131,132,132,132,132,132,132,132,132,132,132,132,132,132,132,132,133,133,133,133,133,133,133,
 133,133,133,133,133,133,133,133,133,134,134,134,134,134,134,134,134,134,134,134,134,134,134,134,134,135,135,135,135,135,135,135,
 135,135,135,135,135,135,135,135,135,136,136,136,136,136,136,136,136,136,136,136,136,136,136,136,136,137,137,137,137,137,137,137,
 137,137,137,137,137,137,137,137,137,138,138,138,138,138,138,138,138,138,138,138,138,138,138,138,138,138,139,139,139,139,139,139,
 139,139,139,139,139,139,139,139,139,139,140,140,140,140,140,140,140,140,140,140,140,140,140,140,140,140,140,141,141,141,141,141,
 141,141,141,141,141,141,141,141,141,141,141,141,142,142,142,142,142,142,142,142,142,142,142,142,142,142,142,142,142,143,143,143,
 143,143,143,143,143,143,143,143,143,143,143,143,143,143,144,144,144,144,144,144,144,144,144,144,144,144,144,144,144,144,144,144,
 145,145,145,145,145,145,145,145,145,145,145,145,145,145,145,145,145,146,146,146,146,146,146,146,146,146,146,146,146,146,146,146,
 146,146,146,147,147,147,147,147,147,147,147,147,147,147,147,147,147,147,147,147,148,148,148,148,148,148,148,148,148,148,148,148,
 148,148,148,148,148,148,149,149,149,149,149,149,149,149,149,149,149,149,149,149,149,149,149,149,149,150,150,150,150,150,150,150,
 150,150,150,150,150,150,150,150,150,150,150,151,151,151,151,151,151,151,151,151,151,151,151,151,151,151,151,151,151,152,152,152,
 152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,152,153,153,153,153,153,153,153,153,153,153,153,153,153,153,153,153,
 153,153,153,154,154,154,154,154,154,154,154,154,154,154,154,154,154,154,154,154,154,154,155,155,155,155,155,155,155,155,155,155,
 155,155,155,155,155,155,155,155,155,156,156,156,156,156,156,156,156,156,156,156,156,156,156,156,156,156,156,156,157,157,157,157,
 157,157,157,157,157,157,157,157,157,157,157,157,157,157,157,158,158,158,158,158,158,158,158,158,158,158,158,158,158,158,158,158,
 158,158,158,159,159,159,159,159,159,159,159,159,159,159,159,159,159,159,159,159,159,159,160,160,160,160,160,160,160,160,160,160,
 160,160,160,160,160,160,160,160,160,160,161,161,161,161,161,161,161,161,161,161,161,161,161,161,161,161,161,161,161,161,162,162,
 162,162,162,162,162,162,162,162,162,162,162,162,162,162,162,162,162,162,163,163,163,163,163,163,163,163,163,163,163,163,163,163,
 163,163,163,163,163,163,163,164,164,164,164,164,164,164,164,164,164,164,164,164,164,164,164,164,164,164,164,165,165,165,165,165,
 165,165,165,165,165,165,165,165,165,165,165,165,165,165,165,165,166,166,166,166,166,166,166,166,166,166,166,166,166,166,166,166,
 166,166,166,166,166,167,167,167,167,167,167,167,167,167,167,167,167,167,167,167,167,167,167,167,167,167,168,168,168,168,168,168,
 168,168,168,168,168,168,168,168,168,168,168,168,168,168,168,169,169,169,169,169,169,169,169,169,169,169,169,169,169,169,169,169,
 169,169,169,169,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,170,171,171,171,171,171,171,
 171,171,171,171,171,171,171,171,171,171,171,171,171,171,171,172,172,172,172,172,172,172,172,172,172,172,172,172,172,172,172,172,
 172,172,172,172,172,173,173,173,173,173,173,173,173,173,173,173,173,173,173,173,173,173,173,173,173,173,173,174,174,174,174,174,
 174,174,174,174,174,174,174,174,174,174,174,174,174,174,174,174,174,175,175,175,175,175,175,175,175,175,175,175,175,175,175,175,
 175,175,175,175,175,175,175,175,176,176,176,176,176,176,176,176,176,176,176,176,176,176,176,176,176,176,176,176,176,176,177,177,
 177,177,177,177,177,177,177,177,177,177,177,177,177,177,177,177,177,177,177,177,177,178,178,178,178,178,178,178,178,178,178,178,
 178,178,178,178,178,178,178,178,178,178,178,179,179,179,179,179,179,179,179,179,179,179,179,179,179,179,179,179,179,179,179,179,
 179,179,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,180,181,181,181,181,181,181,
 181,181,181,181,181,181,181,181,181,181,181,181,181,181,181,181,181,182,182,182,182,182,182,182,182,182,182,182,182,182,182,182,
 182,182,182,182,182,182,182,182,183,183,183,183,183,183,183,183,183,183,183,183,183,183,183,183,183,183,183,183,183,183,183,183,
 184,184,184,184,184,184,184,184,184,184,184,184,184,184,184,184,184,184,184,184,184,184,184,184,185,185,185,185,185,185,185,185,
 185,185,185,185,185,185,185,185,185,185,185,185,185,185,185,185,186,186,186,186,186,186,186,186,186,186,186,186,186,186,186,186,
 186,186,186,186,186,186,186,186,187,187,187,187,187,187,187,187,187,187,187,187,187,187,187,187,187,187,187,187,187,187,187,187,
 188,188,188,188,188,188,188,188,188,188,188,188,188,188,188,188,188,188,188,188,188,188,188,188,188,189,189,189,189,189,189,189,
 189,189,189,189,189,189,189,189,189,189,189,189,189,189,189,189,189,190,190,190,190,190,190,190,190,190,190,190,190,190,190,190,
 190,190,190,190,190,190,190,190,190,190,191,191,191,191,191,191,191,191,191,191,191,191,191,191,191,191,191,191,191,191,191,191,
 191,191,191,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,193,193,193,193,
 193,193,193,193,193,193,193,193,193,193,193,193,193,193,193,193,193,193,193,193,193,194,194,194,194,194,194,194,194,194,194,194,
 194,194,194,194,194,194,194,194,194,194,194,194,194,194,194,195,195,195,195,195,195,195,195,195,195,195,195,195,195,195,195,195,
 195,195,195,195,195,195,195,195,195,196,196,196,196,196,196,196,196,196,196,196,196,196,196,196,196,196,196,196,196,196,196,196,
 196,196,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,197,198,198,198,198,
 198,198,198,198,198,198,198,198,198,198,198,198,198,198,198,198,198,198,198,198,198,198,198,199,199,199,199,199,199,199,199,199,
 199,199,199,199,199,199,199,199,199,199,199,199,199,199,199,199,199,200,200,200,200,200,200,200,200,200,200,200,200,200,200,200,
 200,200,200,200,200,200,200,200,200,200,200,200,201,201,201,201,201,201,201,201,201,201,201,201,201,201,201,201,201,201,201,201,
 201,201,201,201,201,201,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,202,
 202,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,203,204,204,204,204,
 204,204,204,204,204,204,204,204,204,204,204,204,204,204,204,204,204,204,204,204,204,204,204,205,205,205,205,205,205,205,205,205,
 205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,205,206,206,206,206,206,206,206,206,206,206,206,206,206,
 206,206,206,206,206,206,206,206,206,206,206,206,206,206,207,207,207,207,207,207,207,207,207,207,207,207,207,207,207,207,207,207,
 207,207,207,207,207,207,207,207,207,207,208,208,208,208,208,208,208,208,208,208,208,208,208,208,208,208,208,208,208,208,208,208,
 208,208,208,208,208,208,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,209,
 209,209,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,210,211,211,
 211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,211,212,212,212,212,212,
 212,212,212,212,212,212,212,212,212,212,212,212,212,212,212,212,212,212,212,212,212,212,212,212,213,213,213,213,213,213,213,213,
 213,213,213,213,213,213,213,213,213,213,213,213,213,213,213,213,213,213,213,213,214,214,214,214,214,214,214,214,214,214,214,214,
 214,214,214,214,214,214,214,214,214,214,214,214,214,214,214,214,214,215,215,215,215,215,215,215,215,215,215,215,215,215,215,215,
 215,215,215,215,215,215,215,215,215,215,215,215,215,215,216,216,216,216,216,216,216,216,216,216,216,216,216,216,216,216,216,216,
 216,216,216,216,216,216,216,216,216,216,216,216,217,217,217,217,217,217,217,217,217,217,217,217,217,217,217,217,217,217,217,217,
 217,217,217,217,217,217,217,217,217,218,218,218,218,218,218,218,218,218,218,218,218,218,218,218,218,218,218,218,218,218,218,218,
 218,218,218,218,218,218,218,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,
 219,219,219,219,219,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,220,
 220,220,220,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,221,
 221,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,222,
 223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,223,224,224,
 224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,224,225,225,225,
 225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,225,226,226,226,226,
 226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,226,227,227,227,227,227,
 227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,227,228,228,228,228,228,
 228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,228,229,229,229,229,229,229,
 229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,229,230,230,230,230,230,230,
 230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,230,231,231,231,231,231,231,
 231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,231,232,232,232,232,232,232,
 232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,233,233,233,233,233,233,
 233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,233,234,234,234,234,234,234,
 234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,234,235,235,235,235,235,
 235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,236,236,236,236,
 236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,236,237,237,237,
 237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,237,238,238,
 238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,238,239,
 239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,239,
 239,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,240,
 240,240,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,241,
 241,241,241,241,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,242,
 242,242,242,242,242,242,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,243,
 243,243,243,243,243,243,243,243,243,244,244,244,244,244,244,244,244,244,244,244,244,244,244,244,244,244,244,244,244,244,244,244,
 244,244,244,244,244,244,244,244,244,244,244,245,245,245,245,245,245,245,245,245,245,245,245,245,245,245,245,245,245,245,245,245,
 245,245,245,245,245,245,245,245,245,245,245,245,245,245,246,246,246,246,246,246,246,246,246,246,246,246,246,246,246,246,246,246,
 246,246,246,246,246,246,246,246,246,246,246,246,246,246,246,246,246,247,247,247,247,247,247,247,247,247,247,247,247,247,247,247,
 247,247,247,247,247,247,247,247,247,247,247,247,247,247,247,247,247,247,247,247,248,248,248,248,248,248,248,248,248,248,248,248,
 248,248,248,248,248,248,248,248,248,248,248,248,248,248,248,248,248,248,248,248,248,248,248,249,249,249,249,249,249,249,249,249,
 249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,249,250,250,250,250,250,250,
 250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,250,251,251,
 251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,251,
 251,251,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,252,
 252,252,252,252,252,252,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,253,
 253,253,253,253,253,253,253,253,253,253,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,
 254,254,254,254,254,254,254,254,254,254,254,254,254,254,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
 255,
};
/* Glow energy (linear light) for a mask above the pixel's own weight by d:
 * in proportion to d, with a toe below 8 (scaled by d/8), so the faint edge
 * of a wide glow fades in instead of starting with one whole step (about 250
 * of 65535 at High), which in the shadows showed as a contour line. */
static inline __attribute__((always_inline)) unsigned halo_energy(unsigned d,unsigned strength) {
 unsigned e=(d*strength*257u+50u)/100u;
 return d<8u?(e*d+4u)>>3:e;
}
static unsigned char halo_encoded(unsigned linear) {
 return linear>=65535?255:halo_encode[(linear+8)>>4];
}
#if CPU_LUT
static inline __attribute__((always_inline)) void halo_add(unsigned blur,unsigned strength,
 unsigned char luma,unsigned char rgb_value[3],const colour_tables *tables) {
 unsigned source=halo_highlight[luma],energy;
 if(blur<=source)return;
 if(tables) {
  unsigned d=blur-source;energy=tables->energy[d];
  if(!energy)return;
  rgb_value[0]=halo_encoded(halo_linear[rgb_value[0]]+energy);
  rgb_value[1]=halo_encoded(halo_linear[rgb_value[1]]+tables->green_energy[d]);
  return;
 }
 /* Profile red linear-light contribution; green at one fifth, blue unchanged. */
 energy=halo_energy(blur-source,strength);
 if(!energy)return;
 rgb_value[0]=halo_encoded(halo_linear[rgb_value[0]]+energy);
 rgb_value[1]=halo_encoded(halo_linear[rgb_value[1]]+(energy+2)/5);
}
#endif /* CPU_LUT */
/* Halation for Canon's colour table: the glow is added in linear light,
 * coloured by the look's tint (tools/hw_table.py fits it as glow added to
 * the look's output, although Canon applies the table after it). Pairs with no glow are left exactly as they are.
 * The scan over every pair is ARM; the pair decode, glow and encode run only
 * for the few pairs that glow, out of line so the scan keeps its registers. */
#define COMPACT FILM_COMPACT
#define GLOW static inline __attribute__((always_inline))
GLOW void graded_decode(const unsigned char *p,const colour_tables *tables,unsigned char a[3],unsigned char b[3]) {
 int u=signed_byte(p[0]),v=signed_byte(p[2]),red=round16(91881*v),green=round16(22553*u+46802*v),blue=round16(116130*u),k;
 (void)tables;                                   /* the same values the tables hold */
 for(k=0;k<2;k++){unsigned char *o=k?b:a;int y=p[1+2*k];o[0]=byte(y+red);o[1]=byte(y-green);o[2]=byte(y+blue);}
}
GLOW unsigned char graded_luma(const unsigned char c[3]) {
 return (unsigned char)((19595u*c[0]+38470u*c[1]+7471u*c[2]+32768u)>>16);
}
GLOW void graded_encode(unsigned char *pair,const unsigned char *a,const unsigned char *b) {
 int green=a[1]+b[1],red_difference=a[0]+b[0]-green,blue_difference=a[2]+b[2]-green,value,k;
 pair[1]=graded_luma(a);pair[3]=graded_luma(b);
 for(k=0;k<2;k++){
  int sum=k?red_difference*32768-blue_difference*5329:blue_difference*32768-red_difference*11059;
  value=(sum+65536-(sum<0))>>17;pair[k*2]=(unsigned char)(value>127?127:value);
 }
}
GLOW int halo_add_graded(unsigned blur,const halo_rows *c,
 unsigned char luma,unsigned char rgb_value[3],const colour_tables *tables) {
 unsigned source=c->highlight[luma],energy;
 if(blur<=source)return 0;
 energy=tables?tables->energy[blur-source]:halo_energy(blur-source,c->strength);
 if(!energy)return 0;
 rgb_value[0]=halo_encoded(halo_linear[rgb_value[0]]+((energy*c->tint_red+128u)>>8));
 rgb_value[1]=halo_encoded(halo_linear[rgb_value[1]]+((energy*c->tint_green+128u)>>8));
 rgb_value[2]=halo_encoded(halo_linear[rgb_value[2]]+((energy*c->tint_blue+128u)>>8));
 return 1;
}
/* Monochrome looks: the glow goes on the luma alone and the pixel's chroma
 * is kept. Canon applies the look's table in its JPEG step, after this, so
 * the frame here still carries the scene's colour, and the black and white
 * table turns that colour into grey. Zeroing it in glowing pixels only gave
 * them a different grey: 10 to 20 levels darker under warm light, with an
 * edge where the glow begins (the tail's 16 px cells, as squares). On a
 * neutral pixel this is the same value the RGB path gives, since a mono
 * look's tint is the same on all three channels. */
GLOW int halo_add_luma(unsigned blur,const halo_rows *c,unsigned char *y,const colour_tables *tables) {
 unsigned source=c->highlight[*y],energy;
 if(blur<=source)return 0;
 energy=tables?tables->energy[blur-source]:halo_energy(blur-source,c->strength);
 if(!energy)return 0;
 *y=halo_encoded(halo_linear[*y]+((energy*c->tint_luma+128u)>>8));
 return 1;
}
static __attribute__((noinline)) void halo_glow_pair(const halo_rows *c,unsigned char *p,unsigned left,unsigned middle,
 const colour_tables *tables,int mono) {
 unsigned char a[3],b[3];int glow;
 if(mono){halo_add_luma(left,c,p+1,tables);halo_add_luma(middle,c,p+3,tables);return;}
 graded_decode(p,tables,a,b);
 glow=halo_add_graded(left,c,p[1],a,tables);
 glow|=halo_add_graded(middle,c,p[3],b,tables);
 if(glow)graded_encode(p,a,b);
}
/* mask: this row's glow samples per grid column (odd rows halfway between
 * two mask rows, the tail blended in). */
static void halo_span_graded(const halo_rows *c,const unsigned char *mask,unsigned char *pixels,unsigned start,
 unsigned span,const colour_tables *tables,int mono) {
 const unsigned char *hl=c->highlight,*m=mask+start/2;
 unsigned end=start+span,last=end<c->width?end:end-2,col,left=*m++;
 unsigned char *p=pixels;
 if(!((size_t)p&3u))                                /* one word per pair: U,Y0,V,Y1 little-endian */
 for(col=start;col<last;col+=2,p+=4) {
  /* A zero left sample and four zero samples after it: the next four pairs
   * have no glow. Skip them a word of the mask at a time. */
  if(!left) {
   while(!((size_t)m&3u)&&col+8<=last&&!*(const unsigned*)m){m+=4;col+=8;p+=16;}
   if(col>=last)break;
  }
  unsigned right=*m++;
  if(left|right) {                                 /* both zero: no glow, the pixels are not read */
   unsigned pair=*(const unsigned*)p,middle=(left+right+1)>>1;
   if((left>hl[(pair>>8)&255u])|(middle>hl[pair>>24]))halo_glow_pair(c,p,left,middle,tables,mono);
  }
  left=right;
 }
 else
 for(col=start;col<last;col+=2,p+=4) {             /* the right sample exists */
  unsigned right=*m++,middle=(left+right+1)>>1;
  if((left|right)&&(left>hl[p[1]]||middle>hl[p[3]]))halo_glow_pair(c,p,left,middle,tables,mono);
  left=right;
 }
 if(col<end&&(left>hl[p[1]]||left>hl[p[3]]))      /* the row's last pair: right = left */
  halo_glow_pair(c,p,left,left,tables,mono);
}
#if CPU_LUT
static inline __attribute__((always_inline)) void halo_span(const halo_rows *cp,const unsigned char *mask,unsigned char *pixels,
 unsigned start,unsigned span,const void *lut,unsigned n,const unsigned short *axis,const colour_tables *tables,int mono) {
 const node_t *nodes=(const node_t*)((const unsigned char*)lut+16);
 halo_rows c=*cp;unsigned width=c.width;
 /* Within a row, the left sample at column col is always mask[col/2]. */
 unsigned left=mask[start/2];
 for(unsigned col=start;col<start+span;col+=2) {
  unsigned right=col+2<width?mask[col/2+1]:left;
  unsigned char *p=pixels+(col-start)*2,in[3],a[3],b[3];
  int red,green,blue;
  decode_chroma(p,tables,&red,&green,&blue);
  rgb(p[1],red,green,blue,in);
  if(left)halo_add(left,c.strength,p[1],in,tables);
  fast_pixel(nodes,n,axis,tables,in,a,mono);
  unsigned middle=(left+right+1)/2;
  if(p[1]==p[3]&&left==middle) {
   b[0]=a[0];b[1]=a[1];b[2]=a[2];
  } else {
   rgb(p[3],red,green,blue,in);
   if(middle)halo_add(middle,c.strength,p[3],in,tables);
   fast_pixel(nodes,n,axis,tables,in,b,mono);
  }
  if(mono)encode_mono(p,a[0],b[0]);else encode_pair(p,a,b);
  left=right;
 }
}
#endif /* CPU_LUT */
/* lut == NULL: Canon's colour table grades the frame after this; the glow
 * takes the tint (required) and graded_mono marks monochrome looks. */
static int map_halo_cached(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,const void *lut,unsigned level,void *work,size_t work_size,
 unsigned char *scratch,size_t capacity,void (*progress)(unsigned,unsigned),
 unsigned vignette,unsigned leaks,unsigned seed,unsigned toy_level,void *toy_work,size_t toy_size,
 const film_print_settings *print,const film_halo_tint *tint,int graded_mono) {
 unsigned row,col,n,i;halo_rows c;size_t needed=film_halo_level_work_size(width,level);
 int mono=graded_mono!=0,effects,lens,printing;film_retro retro;film_toy toy;film_print prt;
 if(!image||!work||!needed||!height||stride<width*2||height>size/stride||
    work_size<needed||((size_t)work&1)||(!lut&&!tint))return -1;
#if CPU_LUT
 unsigned short axis[256];
 n=lut?prepared_lattice(lut,&mono):2;
 size_t lut_size=lut?16u+n*n*n*6u:0;
#else
 const size_t lut_size=0;n=2;           /* the camera's frame is graded: lut is 0 */
#endif
 if(!n||overlaps(work,needed,image,size)||(lut&&overlaps(work,needed,lut,lut_size))||
    !valid_row(scratch,capacity,width*2,image,size,lut,lut_size)||
    (scratch&&overlaps(scratch,width*2,work,needed)))return -1;
 colour_tables *tables=colour_table_space(scratch,capacity,chunk_bytes(width),image,size,lut,lut_size,work,needed);
#if CPU_LUT
 prepare_colour_tables(n,axis,tables);
#endif
 if(tables)for(i=0;i<256;i++){
  unsigned e=halo_energy(i,film_halo_strength(level));
  tables->energy[i]=e;tables->green_energy[i]=(e+2)/5;
 }
 effects=retro_setup(&retro,tables,scratch,capacity,width,height,vignette,leaks,seed,mono,image,size,lut,lut_size,work,needed);
 if(effects<0)return -1;
 lens=toy_setup(&toy,toy_level,toy_work,toy_size,image,size,width,height,stride,lut,lut_size,scratch,capacity,work,needed);
 if(lens<0)return -1;
 printing=film_print_begin(&prt,width,height,print,seed,mono);
 if(printing<0)return -1;
 memset(work,0,needed);memset(&c,0,sizeof(c));c.width=width;c.height=height;c.stride=stride;c.image=image;
 c.box=halo_box(level);
 if(halo_scale!=65536u){c.box=(c.box*halo_scale+32768u)>>16;if(!c.box)c.box=1;}
 c.coarse=halo_coarse_requested;
 if(c.coarse){c.box/=2;if(!c.box)c.box=1;}         /* 4x4 cells: half the box, the same glow size */
 c.radius=HALO_STAGES*c.box;c.win=2*c.box+1;c.inv=(unsigned)((0x100000000ull+c.win-1u)/c.win);c.strength=film_halo_strength(level);
 c.tint_red=tint?tint->red:256;c.tint_green=tint?tint->green:0;c.tint_blue=tint?tint->blue:0;
 c.tint_luma=(19595u*c.tint_red+38470u*c.tint_green+7471u*c.tint_blue+32768u)>>16;
 c.highlight=tint&&tint->highlight?tint->highlight:halo_highlight;c.fast=halo_fast_requested;
 c.pair_width=width/2;
 if(c.coarse){c.grid_width=(width+3)/4;c.grid_height=(height+3)/4;}
 else{c.grid_width=width/2;c.grid_height=(height+1)/2;}
 c.ring=work;c.sum=(unsigned short*)(c.ring+(((size_t)c.grid_width*c.win*HALO_STAGES+1u)&~(size_t)1u));
 c.tmp=(unsigned char*)(c.sum+HALO_STAGES*c.grid_width);c.current=c.tmp+c.grid_width;c.next=c.current+c.grid_width;c.row=c.next+c.grid_width;
 /* The tail, after the core's workspace (as film_halo_level_work_size lays
  * it out for the level; a smaller frame's boxes only use less). */
 { unsigned tw=(width+HALO_TAIL_CELL-1u)/HALO_TAIL_CELL,th=(height+HALO_TAIL_CELL-1u)/HALO_TAIL_CELL,tb=halo_tail_box(level);
   unsigned char *t=(unsigned char*)work+((halo_core_bytes(c.grid_width,halo_box(level))+1u)&~(size_t)1u);
   if(halo_scale!=65536u){tb=(tb*halo_scale+32768u)>>16;if(!tb)tb=1;}
   c.tail_on=th<=tw;
   c.tail_sum=(unsigned short*)t;c.tail_map=t+2u*tw;c.tail_line=c.tail_map+(size_t)tw*tw;
   c.tail_row=c.tail_line+tw+2u*HALO_STAGES*halo_tail_box(level);c.tail_grid=c.tail_row+tw;c.comb=c.tail_grid+c.pair_width;c.comb_zero=1;
   c.expand=c.comb+c.pair_width;     /* coarse only: the core grid uses far less than the workspace's 2x2 size */
   for(i=0;i<256;i++){c.tail_scale[i]=(unsigned char)((154u*i+64u)>>8);c.core_scale[i]=(unsigned char)((102u*i+64u)>>8);}
   c.tail_w=tw;c.tail_h=th;c.tail_box=tb;c.tail_win=2u*tb+1u;c.tail_inv=(unsigned)((0x100000000ull+c.tail_win-1u)/c.tail_win);
   if(c.tail_on)halo_tail_build(&c); }
 /* Fill the rings from rows -radius to radius (edge rows repeated): box k
  * starts once box k-1 has a full window, and the last row gives row 0. */
 for(i=0;i<=2*c.radius;i++) {
  int yy=(int)i-(int)c.radius;unsigned depth=1u+(i+1>=c.win)+(i+2>=2*c.win);
  int zero=halo_push(&c,yy<0?0:(unsigned)yy>=c.grid_height?c.grid_height-1:(unsigned)yy,depth,i==2*c.radius?c.current:0,0);
  if(i==2*c.radius)c.current_zero=zero;
 }
 c.next_zero=1;
 int copy=scratch&&!rows_in_place;
 for(row=0;row<height;row++) {
  const unsigned char *mask=c.current;int mask_zero=c.current_zero;
  if(c.coarse) {
   /* Four image rows per mask row: a quarter step from current to next
    * per row, then two samples per cell along the row. */
   unsigned phase=row&3u;
   c.blend=phase!=0;
   if(phase==1)halo_prepare_odd(&c);
   if(phase) {
    mask_zero=c.current_zero&&c.next_zero;
    if(!mask_zero){halo_lerp4(c.tmp,c.current,c.next,c.grid_width,phase);mask=c.tmp;}
   }
   if(!mask_zero){halo_expand(c.expand,mask,c.grid_width,c.pair_width);mask=c.expand;}
  } else if((c.blend=row&1)) {
   /* Odd rows sample halfway between two mask rows. tmp is free until the
    * next halo_fill, so it holds this row's samples. */
   halo_prepare_odd(&c);
   mask_zero=c.current_zero&&c.next_zero;
   if(!mask_zero){halo_average(c.tmp,c.current,c.next,c.grid_width);mask=c.tmp;}
  }
  if(c.tail_on){mask_zero=!halo_tail_combine(&c,row,mask_zero?0:mask);mask=c.comb;}
  unsigned chunk=copy?ROW_CHUNK/2:width;
  for(col=0;col<width;col+=chunk) {
   unsigned span=width-col<chunk?width-col:chunk;
   unsigned char *source=image+(size_t)row*stride+col*2;
   unsigned char *pixels=copy?scratch:source;
   if(copy)memcpy(scratch,source,span*2);
   if(lens)film_toy_row(&toy,row,pixels,col,span);
#if CPU_LUT
   if(lut)halo_span(&c,mask,pixels,col,span,lut,n,axis,tables,mono);
   else
#endif
   if(!mask_zero)halo_span_graded(&c,mask,pixels,col,span,tables,mono);   /* no glow without a mask */
   if(effects)film_retro_row(&retro,row,pixels,col,span);
   if(printing)film_print_row(&prt,row,pixels,col,span);
   if(copy)memcpy(source,scratch,span*2);
  }
  if(c.coarse?(row&3u)==3u:c.blend){unsigned char *swap=c.current;int z=c.current_zero;c.current=c.next;c.next=swap;c.current_zero=c.next_zero;c.next_zero=z;}
  if(progress&&(row%32==31||row+1==height))progress(row+1,height);
 }
 return 0;
}
#if CPU_LUT /* host reference: the CPU LUT with halation before it */
int film_uyvy_map_halo_cached(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,const void *lut,unsigned level,void *work,size_t work_size,
 unsigned char *scratch,size_t capacity,void (*progress)(unsigned,unsigned)) {
 if(!lut)return -1;
 return map_halo_cached(image,size,width,height,stride,lut,level,work,work_size,scratch,capacity,progress,0,0,0,0,0,0,0,0,0);
}
int film_uyvy_map_halo_cached_retro(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,const void *lut,unsigned level,void *work,size_t work_size,
 unsigned char *scratch,size_t capacity,void (*progress)(unsigned,unsigned),
 unsigned vignette,unsigned leaks,unsigned seed,unsigned toy,void *toy_work,size_t toy_size,
 const film_print_settings *print) {
 if(!lut)return -1;
 return map_halo_cached(image,size,width,height,stride,lut,level,work,work_size,scratch,capacity,progress,vignette,leaks,seed,toy,toy_work,toy_size,print,0,0);
}
#endif /* CPU_LUT */
COMPACT int film_uyvy_graded_retro(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,int mono,unsigned level,const film_halo_tint *tint,void *work,size_t work_size,
 unsigned char *scratch,size_t capacity,void (*progress)(unsigned,unsigned),
 unsigned vignette,unsigned leaks,unsigned seed,unsigned toy,void *toy_work,size_t toy_size,
 const film_print_settings *print) {
 if(level)return map_halo_cached(image,size,width,height,stride,0,level,work,work_size,scratch,capacity,progress,
                                 vignette,leaks,seed,toy,toy_work,toy_size,print,tint,mono);
 return map_cached(image,size,width,height,stride,0,0,width,height,0,scratch,capacity,progress,vignette,leaks,seed,toy,toy_work,toy_size,print,mono);
}

#if CPU_LUT /* uncached wrapper for host tests */
int film_uyvy_map_halo_level(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,const void *lut,unsigned level,void *work,size_t work_size,void (*progress)(unsigned,unsigned)) {
 return film_uyvy_map_halo_cached(image,size,width,height,stride,lut,level,work,work_size,0,0,progress);
}
#endif

#if CPU_LUT /* legacy host test entry */
/* Legacy internal entry defaults to the new Low preset. */
int film_uyvy_map_halo(unsigned char *image,size_t size,unsigned width,unsigned height,
 unsigned stride,const void *lut,void *work,size_t work_size,void (*progress)(unsigned,unsigned)) {
 return film_uyvy_map_halo_level(image,size,width,height,stride,lut,1,work,work_size,progress);
}
#endif
