/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. SpyTask owns LUT loading/selection and observation logs.
 * Canon task callbacks use a stable latched LUT at the guarded pre-encoder
 * boundary, map the confirmed UYVY input and collect bounded RAM samples. */
#include "camera_info.h"
#include "clock.h"
#include "shooting.h"
#include "film_pipe.h"
#include "film_stage.h"
#include "film_raw.h"
#include "film_colour.h"
#include "properties.h"
#include "cache.h"
#include "task.h"
#include "film_ui.h"
#include "film_lut.h"
#include "film_toy.h"
#include "film_grain.h"
#include "film_bloom.h"
#include "film_hwcolour.h"
#include "film_gbui.h"
#define STAGE_MAGIC 0x47535447u
#define STAGE_CHUNK 4096
#define STAGE_COUNT 4
static const unsigned addresses[STAGE_COUNT]={0x4238a730,0x42cd6330,0x42154f30,0x4284dc30};
/* Last address is base + 1336*5472 under the observed YUV411 hypothesis. */
typedef struct {
    unsigned magic,version,bytes,raw_id,tick,phase,copy_ms,reserved;
    film_pipe_sample descriptor;
    unsigned address[STAGE_COUNT];
    unsigned char data[STAGE_COUNT][STAGE_CHUNK];
} stage_record;
static stage_record *records;
static volatile unsigned armed,ready;
static unsigned current_id,finish_tick;
static int finishing,enabled,patch_requested,patch_ready,patch_rc,patch_ms;
static unsigned short resolution;
/* M1, M2, S: phase 1 saw a shot Canon shrinks, so the grade waits for
 * phase 3; and it ran. */
static int shrunk_pending,graded_shrunk;
static const void *patch_lut;
static char patch_file[13];
static void *patch_halo;
static unsigned patch_halo_level;
static void *patch_grain;
static unsigned patch_grain_level,grain_seed;
static int grain_ms,bloom_ms,colour_ms;
static unsigned last_yield,yields; /* progress sleeps; see colour_progress() */
static int rowcache_requested,rowcache_used;
/* FRAMECACHE.TXT: the passes work on the frame through its cached alias
 * (0x0238a730) instead of the uncached one. The D-cache is cleaned and
 * invalidated whole before and after, so no line of the frame is stale while
 * we work and every write is in RAM before Canon's JPEG engine reads it. */
static int framecache_requested,framecache_used;
static int fused_requested,fused_ms;
/* FADETAB.TXT: Bloom and soft focus take their chroma fade from a 64 KB
 * table (film_bloom_fade_table), built once at start-up. Same bytes. */
static int fade_table_loaded;
/* FASTBLOOM.TXT: fast Bloom (film_bloom_fast), within 1-2 levels of Bloom.
 * FASTHALO.TXT: fast halation (film_halo_fast). */
static int fast_bloom,fast_halo;
static void *patch_bloom;
static unsigned patch_bloom_level,patch_vignette,patch_leaks,patch_toy,patch_soft_level;
static void *patch_soft;
/* The look's DIGIC colour table as installed for Canon's develop (0: not
 * installed), its tint and mono flag, taken at arm; hw_used: Canon loaded
 * it for this shot. Only then does the shot get its effects. */
static const unsigned *patch_hw;
static film_halo_tint patch_tint;
static int patch_mono,hw_used;
/* Canon's sharpening gain in the table it loaded for this shot (-1 unknown). */
static int sharpen_loaded=-1;
static int soft_ms;
static void *patch_toy_work;
/* Dust and the date stamp for this shot, the date read when it was armed. */
static film_print_settings patch_print;
/* ZrnoBoy renders this shot instead of the film effects (settings copied
 * at arm), and how long it took. */
static int patch_gboy,gboy_ms;
static const film_print_settings *patch_print_ptr;
int film_stage_patch_requested(void){return patch_requested;}
/* Research hook (tools/probes): if a module sets this, it is called with the
 * developed frame (3648 x 2736 UYVY) right before the colour pass, in Canon's
 * develop task. Exported to modules; 0 in normal use. */
void (*film_stage_probe)(unsigned char *frame);
#ifdef FILM_HWMASK
/* Hardware Bloom mask (HWMASK.TXT): the DIGIC's resize engine shrinks the
 * graded frame to 912 x 684 for Bloom's highlights, the way Canon's still
 * Miniature drives it from this same develop task (ROM GradateComposite,
 * FilteringResizePath). The 1.25 MB buffer comes from
 * the EXMEM_FIRMUP pool for the shot. Anything missing: Bloom reads the
 * full frame as before. */
#define HWQ_W 912
#define HWQ_H 684
#define HWQ_BYTES (HWQ_W*HWQ_H*2)
static int hwmask_requested,hwmask_used,hwmask_ms;
static unsigned char *hwq;
#ifndef FILM_HW_QUARTER
#include "exmem.h"
#define EXMEM_FIRMUP 5
typedef void (*hw_fn)(int);
static void hw_quarter(unsigned char *frame,unsigned char *q) {
    static const int rec[6]={0,2,1,0x20,0x20,0x40},ratio[5]={32,128,128,512,1};
    int old=*(volatile int*)(0x6830+4);
    ((hw_fn)0xff9483f4)(3);                                  /* Sharemem layout as for Miniature */
    ((hw_fn)0xff948514)(0x780);
    ((int(*)(int,int,int,int,int,int,int,int,int,int))0xffab3818)(0xc,5,4,0,0xc,5,0xf,5,4,0);
    ((volatile int*)0xb19c)[1]=1;((volatile int*)0xb19c)[2]=0;
    ((void(*)(int,int))0xffab3f84)(0,0);
    ((void(*)(void*,void*,int,int,int,int,int,int,int,const int*,const int*))0xffab3cac)
        (frame,q,3648,3648,2736,HWQ_W,4,2,0,rec,ratio);       /* (4,2): the band-free filter at 1/4 */
    ((void(*)(int,int))0xffab4d34)(0,0);
    ((hw_fn)0xff9487a4)(0x780);
    ((hw_fn)0xff9483f4)(old);
}
static unsigned char *hw_alloc(void) {
    exmem_alloc_info ai;unsigned char *p;
    if(!get_exmem_type_status(EXMEM_FIRMUP,&ai)||ai.len)return 0;      /* in use */
    p=exmem_alloc_uncached(EXMEM_FIRMUP,HWQ_BYTES+64,&ai);
    if(p&&ai.len<HWQ_BYTES+64){exmem_free_uncached(EXMEM_FIRMUP);p=0;}
    return p?(unsigned char*)(((unsigned)p+63)&~63u):0;
}
#define FILM_HW_QUARTER(frame,q) hw_quarter(frame,q)
#define FILM_HW_ALLOC() hw_alloc()
#define FILM_HW_FREE() exmem_free_uncached(EXMEM_FIRMUP)
#endif
#endif
#ifdef FILM_HWMASK
#define HWMASK_FMT " hwmask=%d hwmask_ms=%d"
#define HWMASK_ARGS ,hwmask_used,hwmask_ms
#else
#define HWMASK_FMT ""
#define HWMASK_ARGS
#endif
static void patch_init(void) {
    patch_requested=enabled&&!stat("A/CHDK/FILM/LUTPATCH.TXT",0);
    patch_rc=-100;
    if(!patch_requested)return;
    film_lut_init();patch_lut=film_lut_data();patch_ready=patch_lut!=0;
    patch_rc=patch_ready?0:-100;
}
static void barrier(void){asm volatile("" ::: "memory");}
#ifndef FILM_STAGE_READ
#define FILM_STAGE_READ(a) (*(volatile unsigned*)(a))
#endif
static unsigned rd(unsigned a){return FILM_STAGE_READ(a);}
#ifdef FILM_DEVTRACE
#define TRACE_MAX 64
typedef struct {unsigned magic,index,tick,raw_id,op,phase,armed,reserved;unsigned body[48];} dev_trace;
static dev_trace *trace;
static volatile unsigned trace_n,trace_last;
void film_develop_trace(const unsigned *msg) {
    dev_trace *t;unsigned i,n=trace_n;
    if(!trace||n>=TRACE_MAX||!msg)return;
    t=&trace[n];t->magic=0x44545231u;t->index=n;t->tick=get_tick_count();t->raw_id=current_id;
    t->op=FILM_STAGE_READ(0x82350);t->phase=ready;t->armed=armed;t->reserved=0;
    for(i=0;i<48;i++)t->body[i]=msg[i];
    trace_last=t->tick;barrier();trace_n=n+1;
}
/* Written from SpyTask once the develop task has been quiet for 3 s. */
static void trace_flush(void) {
    unsigned n=trace_n;int fd;
    if(!trace||!n||(int)((unsigned)get_tick_count()-trace_last)<3000||shooting_in_progress())return;
    fd=open("A/CHDK/FILM/DEVTRACE.BIN",O_WRONLY|O_CREAT|O_APPEND,0777);
    if(fd>=0){write(fd,trace,n*sizeof(dev_trace));close(fd);}
    trace_n=0;
}
#else
void film_develop_trace(const unsigned *msg){(void)msg;}
#endif
void film_stage_init(int native_enabled) {
    enabled=native_enabled&&!stat("A/CHDK/FILM/STAGE.TXT",0);
    if(enabled)records=malloc(2*sizeof(stage_record));
    if(!records)enabled=0;
    rowcache_requested=enabled&&!stat("A/CHDK/FILM/ROWCACHE.TXT",0);
    framecache_requested=enabled&&!stat("A/CHDK/FILM/FRAMECACHE.TXT",0);
    fused_requested=enabled&&!stat("A/CHDK/FILM/FUSEFX.TXT",0);
    film_ui_set_fused(fused_requested&&rowcache_requested);   /* the fused pass needs the row cache */
    if(enabled&&!fade_table_loaded&&!stat("A/CHDK/FILM/FADETAB.TXT",0)) {
        unsigned char *t=malloc(film_bloom_fade_table_size());
        if(t){film_bloom_fade_table(t);fade_table_loaded=1;}
    }
    fast_bloom=enabled&&!stat("A/CHDK/FILM/FASTBLOOM.TXT",0);
    film_bloom_fast(fast_bloom);
    fast_halo=enabled&&!stat("A/CHDK/FILM/FASTHALO.TXT",0);
    film_halo_fast(fast_halo);
#ifdef FILM_HWMASK
    hwmask_requested=enabled&&!stat("A/CHDK/FILM/HWMASK.TXT",0);
#endif
#ifdef FILM_DEVTRACE
    if(!stat("A/CHDK/FILM/DEVTRACE.TXT",0))trace=malloc(TRACE_MAX*sizeof(dev_trace));
#endif
    patch_init();
    {char line[80];int fd;sprintf(line,"init enabled=%d record_bytes=%u allocation=%u\n",enabled,(unsigned)sizeof(stage_record),records?2u*(unsigned)sizeof(stage_record):0);
     fd=open("A/CHDK/FILM/STAGELOG.TXT",O_WRONLY|O_CREAT|O_APPEND,0777);
     if(fd>=0){write(fd,line,strlen(line));close(fd);}}
}
static void stage_overtake(void);
void film_stage_arm(unsigned id) {
    /* A finished shot whose log is still waiting no longer blocks the next
     * one; a shot still in flight does. */
    if(enabled&&finishing)stage_overtake();
    if(!enabled||armed||ready)return;
    patch_lut=film_lut_data();patch_ready=patch_requested&&patch_lut!=0;
    strcpy(patch_file,film_lut_file());
    patch_halo=patch_ready?film_lut_halo_work():0;
    patch_halo_level=patch_halo?film_lut_halo_level():0;
    patch_grain=patch_ready?film_lut_grain_work():0;
    patch_grain_level=patch_grain?film_lut_grain_level():0;
    patch_bloom=patch_ready?film_lut_bloom_work():0;
    patch_bloom_level=patch_bloom?film_lut_bloom_level():0;bloom_ms=0;
#ifdef FILM_HWMASK
    hwmask_used=hwmask_ms=0;
    if(patch_bloom&&hwmask_requested&&!hwq)hwq=FILM_HW_ALLOC();
#endif
    patch_hw=patch_ready?film_lut_hw_table():0;hw_used=0;sharpen_loaded=-1;
    if(patch_hw&&film_lut_hw_tint())patch_tint=*film_lut_hw_tint();else patch_hw=0;
    patch_mono=patch_ready&&film_lut_mono();
    patch_soft=patch_ready?film_lut_soft_work():0;
    patch_soft_level=patch_soft?film_lut_soft_level():0;soft_ms=0;
    patch_vignette=patch_ready?film_lut_vignette_level():0;
    patch_leaks=patch_ready?film_lut_leak_level():0;
    patch_toy_work=patch_ready?film_lut_toy_work():0;
    patch_toy=patch_toy_work?film_lut_toy_level():0;
    if(patch_ready){film_lut_print_settings(&patch_print);patch_print_ptr=&patch_print;}
    else{memset(&patch_print,0,sizeof(patch_print));patch_print_ptr=0;}
    grain_seed=id*0x9e3779b9u^(unsigned)get_tick_count();grain_ms=0;
    patch_gboy=patch_ready&&film_gbui_arm(grain_seed^0x5bd1e995u);gboy_ms=0;colour_ms=0;rowcache_used=0;framecache_used=0;fused_ms=0;yields=0;shrunk_pending=graded_shrunk=0;
    current_id=id;finishing=0;patch_rc=patch_ready? -101:-100;patch_ms=0;
    resolution=65535;get_property_case(222,&resolution,sizeof(resolution));
    barrier();armed=1;
}
/* Kernels report every 32 rows (about 0.25 s). Yield to SpyTask for GUI
 * redraw and housekeeping at most twice a second: each msleep idles the CPU,
 * and Canon then lowers the clock from 144 to 48 until the task wakes. */
#define PROGRESS_YIELD_MS 500u
static void colour_progress(unsigned row,unsigned total) {
    film_ui_progress(row,total);
    if((unsigned)get_tick_count()-last_yield>=PROGRESS_YIELD_MS) {
        msleep(10);last_yield=(unsigned)get_tick_count();yields++;
    }
}
/* Clean and invalidate the whole D-cache by index, as dcache_clean_all()
 * walks it (ARM946E-S: four segments in bits 31:30, the line index from
 * bit 5 up to a quarter of the cache size). A few hundred operations. */
#if defined(__arm__)
static void __attribute__((target("arm"),noinline)) dcache_clean_flush_all(void) {
    unsigned config,size,seg,i;
    asm volatile("mrc p15, 0, %0, c0, c0, 1" : "=r"(config));
    size=(config>>18)&0xFu;
    if(!size)return;
    size=0x200u<<size;
    for(seg=0;seg<4;seg++)
        for(i=0;i<size/4;i+=32)
            asm volatile("mcr p15, 0, %0, c7, c14, 2" :: "r"((seg<<30)|i) : "memory");
    asm volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory");
}
#else
static void dcache_clean_flush_all(void){} /* host tests: no cache */
#endif
static int finish_bloom_and_grain(unsigned char *image,unsigned w,unsigned h,unsigned stride,unsigned char *scratch,size_t capacity) {
    film_grain_rows grain;
    if(film_grain_begin_rows(&grain,w,h,patch_grain_level,grain_seed,
        patch_grain,film_grain_work_size(3648)))return -1;
    film_bloom_row_pass pass={scratch,capacity,film_grain_finish_row,&grain};
    return film_uyvy_bloom_rows(image,(size_t)stride*h,w,h,stride,patch_bloom_level,
        patch_bloom,film_bloom_work_size(3648),&pass,colour_progress);
}
/* L is graded at phase 1 on the full frame. M1, M2 and S: Canon still
 * develops the full frame; its encode-step loop (0xff8e5ec4, between phases 1
 * and 2) first shrinks it into 0x42154f30, then encodes that. These are
 * graded at phase 3, before the encode step, with the effects scaled to the
 * frame so it looks like L shrunk. The workspaces are sized for 3648 and fit. */
#define STAGE_L_FRAME 0x4238a730u
#define STAGE_SHRUNK_FRAME 0x42154f30u
/* Shrunk 4:3 sizes by image size (property 222 = the job's 0x82358):
 * M1, M2 and S (0xff8e0880). */
static const unsigned short shrunk_size[5][2]={{0,0},{2816,2112},{1600,1200},{0,0},{640,480}};
static const char *const size_name[5]={"L","M1","M2","M3","S"};
static int shrunk(unsigned r){return r<5&&shrunk_size[r][0];}
/* Every pixel-sized effect at once (Q16, 65536 = L). */
static void effect_scale(unsigned q) {
    film_halo_scale(q);film_toy_scale(q);film_bloom_scale(q);film_grain_scale(q);
}
static void stage_grade(unsigned base,unsigned w,unsigned h,unsigned stride,unsigned char *scratch,size_t capacity);
/* Phase 3: before each encode step (film_reloc.c, 0xff8e6050). For a
 * shrunk size the step before it (0x4e48, step 1) left the frame at
 * 0x42154f30, and 0x823d0 names it as the encode's source. */
static void stage_shrunk(void) {
    unsigned t=get_tick_count();
    shrunk_pending=0;
    if(shrunk(resolution)) {
        unsigned w=shrunk_size[resolution][0],h=shrunk_size[resolution][1];
        if(rd(0x4e48)==1&&rd(0x823d0)==STAGE_SHRUNK_FRAME&&rd(0x823b8)==w&&rd(0x823bc)==h&&
           rd(0x82350)==2&&rd(0x82358)==resolution) {
            /* Phase 2 has not populated its snapshot yet, as for L at phase 1. */
            graded_shrunk=1;
            effect_scale((w*65536u+1824u)/3648u);
            stage_grade(STAGE_SHRUNK_FRAME,w,h,w*2u,(unsigned char *)&records[1].data,sizeof(records[1].data));
            effect_scale(65536u);
        } else patch_rc=-103;
    } else patch_rc=-103;
    patch_ms=(unsigned)get_tick_count()-t;
}

void film_stage_observe(unsigned phase) {
    unsigned i,j;stage_record *r;
    /* Phase 2 follows the encode, before Canon writes the JPEG file: the
     * sensor buffer must be on the card by then (background backup,
     * film_raw.c). Immediate without one. */
    if(phase==2)film_raw_backup_wait();
    if(phase==3) {
        if(armed&&records&&ready==1&&patch_ready&&shrunk_pending)stage_shrunk();
        return;
    }
    if(!armed||!records||phase<1||phase>2||rd(0x82350)!=2)return;
    if((phase==1&&ready!=0)||(phase==2&&ready!=1))return;
    r=&records[phase-1];r->magic=STAGE_MAGIC;r->version=1;r->bytes=sizeof(*r);
    r->raw_id=current_id;r->tick=get_tick_count();r->phase=phase;r->reserved=0;
    film_pipe_snapshot(&r->descriptor,current_id,r->tick,phase+1,rd);
    for(i=0;i<STAGE_COUNT;i++) {
        unsigned *dst=(unsigned*)r->data[i];r->address[i]=addresses[i];
        for(j=0;j<STAGE_CHUNK/4;j++)dst[j]=rd(addresses[i]+j*4);
    }
    r->copy_ms=(unsigned)get_tick_count()-r->tick;
    if(phase==1&&patch_ready) {
        unsigned t=get_tick_count();
        if(resolution==0&&rd(0x823d0)==0x4238a730u&&rd(0x82388)==0x459961e4u&&
           rd(0x82350)==2&&rd(0x82358)==0&&rd(0x8235c)==1) {
            /* Phase 2 has not populated its snapshot yet. Its data area can
             * hold the cached row and colour tables without another allocation. */
            stage_grade(STAGE_L_FRAME,3648,2736,7296,(unsigned char *)&records[1].data,sizeof(records[1].data));
        } else if(shrunk(resolution)&&rd(0x82350)==2&&rd(0x82358)==resolution) {
            shrunk_pending=1;                             /* graded at phase 3 */
        } else patch_rc=-102;
        if(!shrunk_pending)patch_ms=(unsigned)get_tick_count()-t;
    } else if(phase==2&&shrunk_pending) {
        shrunk_pending=0;patch_rc=-103;               /* encoded without phase 3 */
    }
    barrier();ready=phase;
    if(phase==2)armed=0;
}
/* Every pass on one frame (base: its uncached address), as phase 1 always did. */
static void stage_grade(unsigned base,unsigned w,unsigned h,unsigned stride,unsigned char *scratch,size_t capacity) {
    size_t bytes=(size_t)stride*h;
    if(patch_gboy) {
        /* ZrnoBoy: the frame as Canon developed it, in any colours, becomes
         * the 4-shade picture; none of the film passes run. The scratch row
         * is the record's data (16 KB, a row is at most 7296 bytes). */
        unsigned char *frame=(unsigned char*)base;
        unsigned start=get_tick_count();
        film_ui_raw_stage(FILM_UI_STAGE_GBOY);last_yield=start;
        framecache_used=framecache_requested;
        if(framecache_used){dcache_clean_flush_all();frame=(unsigned char*)(base&~camera_info.cam_uncached_bit);}
        patch_rc=capacity>=stride?film_gbui_render(frame,w,h,stride,scratch,colour_progress):-112;
        if(framecache_used)dcache_clean_flush_all();else dcache_clean_all();
        gboy_ms=colour_ms=(unsigned)get_tick_count()-start;
        film_ui_raw_stage(3);film_ui_progress(h,h);
        return;
    }
    if(!rowcache_requested||((size_t)scratch&camera_info.cam_uncached_bit))scratch=0;
    rowcache_used=scratch!=0;
    film_ui_raw_stage(4);
    unsigned start=get_tick_count();last_yield=start;
    /* Light leaks use their own seed stream so they vary per shot
     * independently of the grain pattern. */
    unsigned retro_seed=grain_seed^0xa3b195f1u;
    if(film_stage_probe&&base==STAGE_L_FRAME)film_stage_probe((unsigned char*)base);
    unsigned char *frame=(unsigned char*)base;
    framecache_used=framecache_requested;
    if(framecache_used){dcache_clean_flush_all();frame=(unsigned char*)(base&~camera_info.cam_uncached_bit);}
    film_colour_rows_in_place(framecache_used);   /* no scratch round trip on a cached frame */
    /* Canon loaded our table for this shot (My Colors Off) and applies it
     * when it encodes the JPEG, after the effects below. If it did not load
     * it (another My Colors preset, or the table could not be installed),
     * the shot is Canon's own: it is left alone and reported as failed
     * (-104). */
    hw_used=patch_hw&&film_hw_used(patch_hw);
    sharpen_loaded=film_hw_sharpen_loaded();
    if(hw_used)patch_rc=film_uyvy_graded_retro(frame,bytes,
        w,h,stride,patch_mono,patch_halo?patch_halo_level:0,&patch_tint,patch_halo,patch_halo?film_halo_work_size(3648):0,
        scratch,capacity,colour_progress,
        patch_vignette,patch_leaks,retro_seed,patch_toy,patch_toy_work,film_toy_work_size(3648),patch_print_ptr);
    else patch_rc=-104;
    colour_ms=(unsigned)get_tick_count()-start;
    /* Soft focus diffuses the graded frame before Bloom and grain. */
    if(!patch_rc&&patch_soft) {
        unsigned g=get_tick_count();film_ui_raw_stage(FILM_UI_STAGE_SOFT);
        patch_rc=film_uyvy_soften(frame,bytes,w,h,stride,patch_soft_level,patch_soft,film_bloom_work_size(3648),colour_progress);
        soft_ms=(unsigned)get_tick_count()-g;
    }
#ifdef FILM_HWMASK
    if(!patch_rc&&patch_bloom&&hwq&&base==STAGE_L_FRAME) {   /* the shrink is set up for L */
        unsigned g=get_tick_count();
        if(framecache_used)dcache_clean_all(); /* the engine reads RAM */
        FILM_HW_QUARTER((unsigned char*)0x4238a730u,hwq);
        film_bloom_use_quarter(hwq,HWQ_W*2,HWQ_W,HWQ_H);
        hwmask_ms=(unsigned)get_tick_count()-g;hwmask_used=1;
    }
#endif
    if(!patch_rc&&fused_requested&&scratch&&patch_bloom&&patch_grain) {
        unsigned g=get_tick_count();film_ui_raw_stage(FILM_UI_STAGE_BLOOM_GRAIN);
        patch_rc=finish_bloom_and_grain(frame,w,h,stride,scratch,capacity);
        fused_ms=(unsigned)get_tick_count()-g;
    } else {
        if(!patch_rc&&patch_bloom) {
            unsigned g=get_tick_count();film_ui_raw_stage(6);
            patch_rc=film_uyvy_bloom(frame,bytes,w,h,stride,patch_bloom_level,patch_bloom,film_bloom_work_size(3648),colour_progress);
            bloom_ms=(unsigned)get_tick_count()-g;
        }
        if(!patch_rc&&patch_grain) {
            unsigned g=get_tick_count();film_ui_raw_stage(5);
            patch_rc=film_uyvy_grain(frame,bytes,w,h,stride,patch_grain_level,grain_seed,patch_grain,film_grain_work_size(3648),colour_progress);
            grain_ms=(unsigned)get_tick_count()-g;
        }
    }
    film_colour_rows_in_place(0);
    if(framecache_used)dcache_clean_flush_all();else dcache_clean_all();
    film_ui_raw_stage(3);film_ui_progress(h,h);
}
void film_stage_finish(void) {
    if(!enabled)return;
    finishing=1;finish_tick=get_tick_count()+1000u;
}
/* The STAGELOG line of the shot that just finished. */
static void stage_line(char *line) {
    sprintf(line,"raw_id=%u patch_requested=%d patch_ready=%d patch_rc=%d patch_ms=%d lut=%s halation=%d halo_level=%u grain_level=%u grain_seed=%u grain_ms=%d bloom_level=%u bloom_ms=%d colour_ms=%d rowcache=%d framecache=%d fused_ms=%d yields=%u vignette=%u leaks=%u toy=%u dust=%u stamp=%u soft=%u soft_ms=%d gboy=%d gboy_ms=%d engine=%s hw_used=%d sharpen=%d fadetab=%d fastbloom=%d fasthalo=%d size=%s" HWMASK_FMT "\n",current_id,patch_requested,patch_ready,patch_rc,patch_ms,patch_file,patch_halo!=0,patch_halo_level,patch_grain_level,grain_seed,grain_ms,patch_bloom_level,bloom_ms,colour_ms,rowcache_used,framecache_used,fused_ms,yields,patch_vignette,patch_leaks,patch_toy,patch_print.dust,patch_print.stamp,patch_soft_level,soft_ms,patch_gboy,gboy_ms,patch_hw?"digic":"none",hw_used,sharpen_loaded,fade_table_loaded,fast_bloom,fast_halo,resolution<5?size_name[resolution]:"other" HWMASK_ARGS);
}
static void stage_append(const char *text) {
    int fd=open("A/CHDK/FILM/STAGELOG.TXT",O_WRONLY|O_CREAT|O_APPEND,0777);
    if(fd>=0){write(fd,text,strlen(text));close(fd);}
}
/* Lines of shots whose log a newer shot overtook (film_stage_arm): kept in
 * RAM until no shot is in progress, oldest first. */
#define STAGE_PENDING 2304
static char stage_pending[STAGE_PENDING];
/* A shot armed while the previous one waits for its log (its JPEG saved,
 * the log held back by a half-press): the previous line goes to the
 * pending text without I/O, its STAGE.BIN records are dropped (the new shot
 * reuses them), and the stage is free. */
static void stage_overtake(void) {
    char line[768];size_t used=strlen(stage_pending);
    stage_line(line);
    if(used+strlen(line)<STAGE_PENDING)strcpy(stage_pending+used,line);
    armed=0;ready=0;finishing=0;
}
void film_stage_poll(void) {
    unsigned n;int fd;
    film_lut_poll(armed||ready||finishing);
    film_gbui_poll();
#ifdef FILM_DEVTRACE
    trace_flush();
#endif
    if(stage_pending[0]&&!shooting_in_progress()){stage_append(stage_pending);stage_pending[0]=0;}
    if(!enabled||!finishing||(int)((unsigned)get_tick_count()-finish_tick)<0||shooting_in_progress())return;
    armed=0;barrier();n=ready;
    fd=open("A/CHDK/FILM/STAGE.BIN",O_WRONLY|O_CREAT|O_APPEND,0777);
    if(fd>=0) {
        if(n)write(fd,records,n*sizeof(stage_record));
        close(fd);
    }
    /* Always leave a textual result, including an observation that was missed. */
    {char line[768];stage_line(line);stage_append(line);}
#ifdef FILM_HWMASK
    if(hwq){FILM_HW_FREE();hwq=0;}
#endif
    ready=0;finishing=0;
}
/* 0 when the stage graded shot raw_id; otherwise why not: -27 it was never
 * armed for it (the stage was busy), -28 the develop was not observed, or
 * the patch's own code (-100 no LUT, -102 unexpected frame, ...). */
int film_stage_result(unsigned raw_id) {
    if(!enabled)return 0;
    if(current_id!=raw_id)return -27;
    if(patch_rc==-101)return -28;   /* armed, but phase 1 never patched */
    return patch_rc;
}
