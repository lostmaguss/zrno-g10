/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#define film_uyvy_grain stage_mock_grain
#define film_uyvy_bloom stage_mock_bloom
#define film_uyvy_soften stage_mock_soften
#define film_uyvy_graded_retro stage_mock_graded
#define FILM_HWMASK 1
#define FILM_HW_QUARTER(frame,q) mock_hw_quarter(frame,q)
#define FILM_HW_ALLOC() mock_hw_alloc()
#define FILM_HW_FREE() mock_hw_free()
static void mock_hw_quarter(unsigned char *frame,unsigned char *q);
static unsigned char *mock_hw_alloc(void);
static void mock_hw_free(void);
#include "../camera/film_stage.c"
static int marker=1,clock_tick,shooting,opens,writes;
static int fake_ready,guard_ready,map_calls,lut_poll_busy,halo_calls,fake_halo_ready;
static unsigned char fake_lut[16];
static unsigned char fake_halo[2];
static unsigned char fake_grain[2];
static unsigned fake_grain_level,grain_calls,fake_bloom_level,bloom_calls;static int grain_error,bloom_error,effect_order;
static unsigned op=2,read_count;
/* The size the passes must get (L, or M1, M2 or S at phase 3), and the frame
 * scale every pixel-sized effect must have then (65536 = L). */
static unsigned exp_w=3648,exp_h=2736,exp_stride=7296,exp_scale=65536;
static unsigned halo_q=65536,toy_q=65536,bloom_q=65536,grain_q=65536;
void film_halo_scale(unsigned q){halo_q=q;}
void film_toy_scale(unsigned q){toy_q=q;}
void film_bloom_scale(unsigned q){bloom_q=q;}
void film_grain_scale(unsigned q){grain_q=q;}
#define SCALES_ARE(q) (halo_q==(q)&&toy_q==(q)&&bloom_q==(q)&&grain_q==(q))
/* The frame the passes must get: uncached, or its cached alias with FRAMECACHE. */
static unsigned long expect_frame=0x4238a730u;
int mock_read(int fd,void *p,unsigned n){(void)fd;(void)p;(void)n;return -1;}
static unsigned short fake_size;
long get_property_case(long id,void *p,long n){assert(id==222&&n==2);*(unsigned short*)p=fake_size;return 0;}
void dcache_clean_all(void){}
static int rows_in_place_now=-1;
void film_colour_rows_in_place(int on){rows_in_place_now=on;}
static unsigned char *fade_installed;
size_t film_bloom_fade_table_size(void){return 65536;}
void film_bloom_fade_table(unsigned char *m){fade_installed=m;}
static int fast_set=-1;void film_bloom_fast(int on){fast_set=on;}
static int halo_fast_set=-1;void film_halo_fast(int on){halo_fast_set=on;}
static int sleeps;
void msleep(int t){(void)t;sleeps++;}
static int backup_waits;
int film_raw_backup_wait(void){backup_waits++;return 0;}
void film_ui_progress(int a,int b){(void)a;(void)b;}
void film_ui_raw_stage(int s){(void)s;}
void film_ui_set_fused(int on){(void)on;}
void film_lut_init(void){}
void film_lut_poll(int busy){lut_poll_busy=busy;}
const void *film_lut_data(void){return fake_ready?fake_lut:0;}
const char *film_lut_file(void){return fake_ready?"PORTRA.LUT":"none";}
void *film_lut_halo_work(void){return fake_halo_ready?fake_halo:0;}
void *film_lut_bloom_work(void){return fake_bloom_level?fake_halo:0;}
unsigned film_lut_bloom_level(void){return fake_bloom_level;}
static unsigned fake_vignette,fake_leaks,seen_vignette=9,seen_leaks=9,seen_seed;
unsigned film_lut_vignette_level(void){return fake_vignette;}
unsigned film_lut_leak_level(void){return fake_leaks;}
static unsigned fake_toy,seen_toy=9;static unsigned char fake_toy_work[4];static void *seen_toy_work;static size_t seen_toy_size;
unsigned film_lut_toy_level(void){return fake_toy;}
void *film_lut_toy_work(void){return fake_toy?fake_toy_work:0;}
static unsigned fake_dust;static film_print_settings seen_print;static int seen_print_set;
static unsigned fake_soft,soft_calls,seen_soft_level;static void *seen_soft_work;
unsigned film_lut_soft_level(void){return fake_soft;}
void *film_lut_soft_work(void){return fake_soft?fake_halo:0;}
int stage_mock_soften(unsigned char *data,size_t bytes,unsigned width,unsigned height,unsigned pitch,unsigned level,void *work,size_t capacity,void (*progress)(unsigned,unsigned)) {
 (void)bytes;(void)width;(void)height;(void)pitch;(void)capacity;
 assert((unsigned long)data==expect_frame);
 assert(effect_order==1);                /* after the colour pass, before Bloom and grain */
 soft_calls++;seen_soft_level=level;seen_soft_work=work;progress(2736,2736);return 0;
}
void film_lut_print_settings(film_print_settings *s){memset(s,0,sizeof(*s));s->dust=fake_dust;s->stamp=fake_dust?1:0;strcpy(s->text,"'26 11 05");}
size_t film_toy_work_size(unsigned width){return width*7u;}
size_t film_bloom_work_size(unsigned width){assert(width==3648);return 61104;}
unsigned film_bloom_radius(unsigned level){return level*32;}
unsigned film_bloom_strength(unsigned level){return level*32;}
int stage_mock_bloom(unsigned char *data,size_t bytes,unsigned width,unsigned height,unsigned pitch,unsigned level,void *work,size_t capacity,void (*progress)(unsigned,unsigned)) {
 assert(effect_order==1);effect_order=2;
 assert(work==fake_halo&&capacity==61104&&level==fake_bloom_level);
 assert((unsigned long)data==expect_frame&&bytes==exp_stride*exp_h&&width==exp_w&&height==exp_h&&pitch==exp_stride);
 bloom_calls++;progress(height,height);return bloom_error;
}
int film_grain_begin_rows(film_grain_rows *state,unsigned width,unsigned height,
 unsigned level,unsigned seed,void *work,size_t work_size) {
 assert(width==exp_w&&height==exp_h&&level==fake_grain_level&&seed==grain_seed);
 assert(work==fake_grain&&work_size==3650);
 state->row=0;
 return 0;
}
void film_grain_finish_row(unsigned char *pixels,void *context) {
 film_grain_rows *state=context;
 assert(pixels==(unsigned char *)&records[1].data&&state->row==0&&effect_order==2);
 state->row++;
 grain_calls++;effect_order=3;
}
int film_uyvy_bloom_rows(unsigned char *data,size_t bytes,unsigned width,unsigned height,
 unsigned pitch,unsigned level,void *work,size_t work_size,
 const film_bloom_row_pass *pass,void (*progress)(unsigned,unsigned)) {
 assert(pass&&pass->capacity==sizeof(records[1].data));
 assert(pass->finish_row==film_grain_finish_row);
 int rc=stage_mock_bloom(data,bytes,width,height,pitch,level,work,work_size,progress);
 if(!rc)pass->finish_row(pass->scratch,pass->context);
 return rc;
}
void *film_lut_grain_work(void){return fake_grain_level?fake_grain:0;}
unsigned film_lut_grain_level(void){return fake_grain_level;}
size_t film_grain_work_size(unsigned width){return (width/2+1)*2u;}
int stage_mock_grain(unsigned char *data,size_t bytes,unsigned width,unsigned height,unsigned pitch,unsigned level,unsigned seed,void *work,size_t capacity,void (*progress)(unsigned,unsigned)) {
 assert(work==fake_grain&&capacity==3650&&level==fake_grain_level&&seed==grain_seed);
 assert((unsigned long)data==expect_frame&&bytes==exp_stride*exp_h&&width==exp_w&&height==exp_h&&pitch==exp_stride);
 if(fake_bloom_level){assert(effect_order==2);effect_order=3;}
 grain_calls++;progress(height,height);return grain_error;
}
unsigned film_lut_halo_level(void){return fake_halo_ready;}
unsigned film_halo_radius(unsigned level){return level?18u*(level+1):0;}
unsigned film_halo_strength(unsigned level){return level?12u*(level+1):0;}
size_t film_halo_work_size(unsigned width){return (width/2)*78u;}
static int shrunk_desc,shrunk_bad,shrunk_step=1;
unsigned mock_stage_read(unsigned a) {
 read_count++;
 if(a==0x82350)return op;
 if(shrunk_desc) {   /* an M1, M2 or S shot: phase 1 as Canon left it, then after the resize step */
  if(a==0x82358)return fake_size;
  if(shrunk_desc==2) {
   if(a==0x4e48)return shrunk_step;
   if(a==0x823d0)return shrunk_bad?0x4238a730:0x42154f30;
   if(a==0x823b8)return fake_size==1?2816:fake_size==2?1600:640;
   if(a==0x823bc)return fake_size==1?2112:fake_size==2?1200:480;
  }
 }
 if(guard_ready&&!shrunk_desc) {
  if(a==0x823d0)return 0x4238a730;
  if(a==0x82388)return 0x459961e4;
  if(a==0x82358)return 0;
  if(a==0x8235c)return 1;
 }
 for(unsigned i=0;i<4;i++)if(a>=pipe_blocks[i].address&&a<pipe_blocks[i].address+4*pipe_blocks[i].count)return a;
 for(unsigned i=0;i<4;i++)if(a>=addresses[i]&&a<addresses[i]+STAGE_CHUNK)return a^0xa5a50000;
 assert(0);return 0;
}
static int fadetab_marker,fastbloom_marker,fasthalo_marker;
int mock_stat(const char *p,struct mock_stat *s){(void)s;if(!strcmp(p,"A/CHDK/FILM/STAGE.TXT"))return marker?0:-1;if(!strcmp(p,"A/CHDK/FILM/FADETAB.TXT"))return fadetab_marker?0:-1;if(!strcmp(p,"A/CHDK/FILM/FASTBLOOM.TXT"))return fastbloom_marker?0:-1;if(!strcmp(p,"A/CHDK/FILM/FASTHALO.TXT"))return fasthalo_marker?0:-1;return -1;}
int mock_open(const char *p,int f,int m){(void)p;(void)f;(void)m;opens++;return 3;}
static unsigned longest_write;
int mock_write(int fd,const void *p,unsigned n){(void)fd;writes++;if(n>=7&&!memcmp(p,"raw_id=",7)&&n>longest_write)longest_write=n;return n;}
/* DIGIC colour engine: the look's table, whether Canon loaded it, and the
 * graded pass. */
static unsigned fake_hw_words[1000];static int fake_hw_on,fake_hw_loaded,fake_mono,graded_calls,graded_level,graded_mono;
static film_halo_tint fake_tint={300,60,5,0};static const film_halo_tint *graded_tint;
const unsigned *film_lut_hw_table(void){return fake_ready&&fake_hw_on?fake_hw_words:0;}
const film_halo_tint *film_lut_hw_tint(void){return fake_ready&&fake_hw_on?&fake_tint:0;}
int film_lut_mono(void){return fake_mono;}
int film_hw_used(const unsigned *t){assert(t==fake_hw_words);return fake_hw_loaded;}
int film_hw_sharpen_loaded(void){return 64;}
int stage_mock_graded(unsigned char *d,size_t b,unsigned w,unsigned h,unsigned p,int mono,unsigned level,const film_halo_tint *tint,void *work,size_t ws,
 unsigned char *s,size_t c,void (*pr)(unsigned,unsigned),unsigned v,unsigned l,unsigned seed,unsigned toy,void *tw,size_t ts,const film_print_settings *ps) {
 assert((unsigned long)d==expect_frame&&b==exp_stride*exp_h&&w==exp_w&&h==exp_h&&p==exp_stride&&c==sizeof(records[1].data));
 assert(level?(work==fake_halo&&ws==142272&&level==(unsigned)fake_halo_ready):(!work&&!ws));
 assert(SCALES_ARE(exp_scale));
 assert(s==(rowcache_requested?(unsigned char *)&records[1].data:0));
 if(s)memset(s,0x5a,7296);
 if(ps)seen_print=*ps;seen_print_set=ps!=0;seen_toy=toy;seen_toy_work=tw;seen_toy_size=ts;seen_vignette=v;seen_leaks=l;seen_seed=seed;
 graded_calls++;graded_level=level;graded_mono=mono;graded_tint=tint;effect_order=1;
 if(level)halo_calls++;else map_calls++;   /* a pass with halation, or a plain one */
 pr(h,h);return 0;
}
int mock_close(int fd){(void)fd;return 0;}
/* ZrnoBoy: the engine renders the whole frame instead of the film passes. */
static int fake_gboy,gboy_calls,gboy_polls,gboy_rc;static unsigned gboy_seed;
int film_gbui_arm(unsigned seed){gboy_seed=seed;return fake_gboy;}
void film_gbui_poll(void){gboy_polls++;}
int film_gbui_render(unsigned char *frame,unsigned w,unsigned h,unsigned stride,unsigned char *row,void (*pr)(unsigned,unsigned)){
 assert((unsigned long)frame==expect_frame&&w==exp_w&&h==exp_h&&stride==exp_stride&&row==(unsigned char *)&records[1].data);
 memset(row,0x33,stride);gboy_calls++;pr(h,h);return gboy_rc;
}
int get_tick_count(void){return clock_tick++;}
/* Hardware Bloom mask: pool, shrink and the hand-off to Bloom. */
static unsigned char hw_buf[64];static int hw_pool_free=1,hw_allocs,hw_frees,hw_shrinks;static unsigned long hw_frame;
static const unsigned char *quarter_ptr;static unsigned quarter_stride,quarter_w,quarter_h;
static unsigned char *mock_hw_alloc(void){hw_allocs++;return hw_pool_free?hw_buf:0;}
static void mock_hw_free(void){hw_frees++;}
static void mock_hw_quarter(unsigned char *frame,unsigned char *q){assert(q==hw_buf&&effect_order==1);hw_frame=(unsigned long)frame;hw_shrinks++;}
void film_bloom_use_quarter(const unsigned char *u,unsigned s,unsigned w,unsigned h){quarter_ptr=u;quarter_stride=s;quarter_w=w;quarter_h=h;}
int shooting_in_progress(void){return shooting;}
int main(void) {
 assert(sizeof(stage_record)==16960);
 film_stage_init(1);assert(enabled&&records&&!fade_installed&&!fade_table_loaded);
 /* FADETAB.TXT: the table is built once, at start-up, and kept. */
 assert(fast_set==0&&!fast_bloom);                                  /* exact Bloom without FASTBLOOM.TXT */
 fastbloom_marker=1;film_stage_init(1);assert(fast_set==1&&fast_bloom);fastbloom_marker=0;film_stage_init(1);assert(fast_set==0);
 assert(halo_fast_set==0&&!fast_halo);                              /* exact halation without FASTHALO.TXT */
 fasthalo_marker=1;film_stage_init(1);assert(halo_fast_set==1&&fast_halo);fasthalo_marker=0;film_stage_init(1);assert(halo_fast_set==0);
 fadetab_marker=1;film_stage_init(1);assert(fade_installed&&fade_table_loaded);{unsigned char *t=fade_installed;film_stage_init(1);assert(fade_installed==t);}
 fadetab_marker=0;
 film_stage_arm(9);assert(armed);
 int oldopens=opens,oldwrites=writes;
 film_stage_observe(0);assert(!ready&&!read_count);
 op=10;film_stage_observe(1);assert(!ready);op=2;
 /* Phase 2 (before the JPEG encode) always waits for a background backup,
  * even when it observes nothing; phase 1 never does. */
 backup_waits=0;film_stage_observe(2);assert(!ready&&backup_waits==1);
 film_stage_observe(1);assert(ready==1&&armed&&records[0].raw_id==9&&backup_waits==1);
 unsigned prev=read_count;film_stage_observe(1);assert(read_count==prev+1); /* op guard only */
 film_stage_observe(2);assert(ready==2&&!armed&&backup_waits==2);
 assert(opens==oldopens&&writes==oldwrites); /* no I/O from Canon task */
 for(unsigned k=0;k<2;k++)for(unsigned i=0;i<4;i++)for(unsigned j=0;j<STAGE_CHUNK/4;j++) {
  unsigned v;memcpy(&v,records[k].data[i]+j*4,4);assert(v==((addresses[i]+j*4)^0xa5a50000));
 }
 film_stage_arm(10);assert(current_id==9); /* pending data cannot be overwritten */
 film_stage_finish();film_stage_poll();assert(ready==2);
 clock_tick+=1001;shooting=1;film_stage_poll();assert(ready==2);
 shooting=0;film_stage_poll();assert(!ready&&!armed&&writes==oldwrites+2);
 film_stage_arm(10);film_stage_observe(1);film_stage_finish();clock_tick+=1001;film_stage_poll();assert(!ready); /* partial log allowed */
 assert(film_stage_result(10)==-100&&film_stage_result(9)==-27);             /* no LUT patch requested; another shot */
 /* A half-press held from just after a saved JPEG until the next shot:
  * the stage re-arms at once, the earlier line waits in RAM (no I/O at the
  * hook), and both lines reach STAGELOG.TXT in order once the button is up. */
 { int w0=writes,o0=opens;
   film_stage_arm(70);film_stage_observe(1);film_stage_observe(2);assert(ready==2);
   film_stage_finish();shooting=1;clock_tick+=1001;film_stage_poll();assert(ready==2&&finishing);   /* log held */
   film_stage_arm(71);assert(armed&&current_id==71&&!finishing&&ready==0&&stage_pending[0]&&strstr(stage_pending,"raw_id=70 "));
   assert(writes==w0&&opens==o0);                                                       /* nothing written at the hook */
   film_stage_observe(1);assert(ready==1);film_stage_observe(2);assert(film_stage_result(71)==patch_rc);
   film_stage_finish();film_stage_poll();assert(stage_pending[0]);                     /* still shooting: held */
   shooting=0;clock_tick+=1001;film_stage_poll();assert(!stage_pending[0]&&!ready&&!finishing&&writes>w0);
   /* still in flight (JPEG not saved yet): a new shot does not take over */
   film_stage_arm(72);film_stage_observe(1);film_stage_arm(73);assert(current_id==72);
   film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();assert(!ready); }
 fake_ready=guard_ready=patch_requested=1;fake_hw_on=fake_hw_loaded=1;   /* Canon grades with the look's table */
 rowcache_requested=1;
 film_stage_arm(12);assert(patch_ready&&patch_lut==fake_lut&&!strcmp(patch_file,"PORTRA.LUT"));
 film_stage_poll();assert(lut_poll_busy); /* No swapping the latched LUT. */
 film_stage_observe(1);assert(map_calls==1&&patch_rc==0&&rowcache_used);film_stage_observe(2);
 unsigned snapshot_word;memcpy(&snapshot_word,records[1].data,4);assert(snapshot_word==(addresses[0]^0xa5a50000));
 rowcache_requested=0;
 film_stage_finish();clock_tick+=1001;film_stage_poll();assert(lut_poll_busy&&!ready);
 film_stage_poll();assert(!lut_poll_busy); /* Selection can change only after log release. */
 guard_ready=0;film_stage_arm(13);film_stage_observe(1);assert(map_calls==1&&patch_rc==-102);
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 patch_requested=guard_ready=1;
 for(fake_halo_ready=1;fake_halo_ready<=3;fake_halo_ready++) {
  film_stage_arm(15+fake_halo_ready);assert(patch_halo==fake_halo&&patch_halo_level==(unsigned)fake_halo_ready);
  film_stage_observe(1);assert(halo_calls==fake_halo_ready&&map_calls==1&&patch_rc==0);
  film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 }
 fake_halo_ready=0;
 for(fake_grain_level=1;fake_grain_level<=3;fake_grain_level++) {
  film_stage_arm(20+fake_grain_level);assert(patch_grain==fake_grain&&patch_grain_level==fake_grain_level);
  film_stage_observe(1);assert(grain_calls==fake_grain_level&&patch_rc==0);
  film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 }
 fake_grain_level=1;grain_error=-7;film_stage_arm(30);film_stage_observe(1);assert(patch_rc==-7);
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();grain_error=0;fake_grain_level=0;
 for(fake_bloom_level=1;fake_bloom_level<=3;fake_bloom_level++) {
  fake_halo_ready=2;fake_grain_level=1;
  film_stage_arm(40+fake_bloom_level);assert(patch_bloom==patch_halo&&patch_bloom_level==fake_bloom_level);
  film_stage_observe(1);assert(patch_rc==0&&bloom_calls==fake_bloom_level&&effect_order==3);
  film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 }
 fused_requested=rowcache_requested=1;
 fake_bloom_level=fake_grain_level=fake_halo_ready=1;
 film_stage_arm(45);film_stage_observe(1);
 assert(patch_rc==0&&rowcache_used&&fused_ms>0&&bloom_ms==0&&grain_ms==0&&effect_order==3);
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 fused_requested=rowcache_requested=0;
 /* HWMASK: a pool for the shot, the shrink after the colour pass and before
  * Bloom on the frame, Bloom told about the 912 x 684 copy, the pool freed with
  * the log; a busy pool or no Bloom leaves the CPU path. */
 hwmask_requested=1;fake_bloom_level=2;fake_halo_ready=2;
 film_stage_arm(50);assert(hwq==hw_buf&&hw_allocs==1);
 film_stage_observe(1);
 assert(patch_rc==0&&hw_shrinks==1&&hw_frame==0x4238a730u&&hwmask_used&&hwmask_ms>0);
 assert(quarter_ptr==hw_buf&&quarter_stride==1824&&quarter_w==912&&quarter_h==684);
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();assert(hw_frees==1&&!hwq);
 hw_pool_free=0;film_stage_arm(51);assert(!hwq&&hw_allocs==2);film_stage_observe(1);
 assert(patch_rc==0&&hw_shrinks==1&&!hwmask_used);
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();assert(hw_frees==1);
 hw_pool_free=1;fake_bloom_level=0;film_stage_arm(52);assert(!hwq&&hw_allocs==2);film_stage_observe(1);assert(hw_shrinks==1);
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 hwmask_requested=0;fake_halo_ready=0;
 /* FRAMECACHE: every pass gets the frame's cached alias; the hardware
  * shrink keeps the uncached frame (the engine reads RAM). The log says so. */
 framecache_requested=1;camera_info.cam_uncached_bit=0x40000000;expect_frame=0x0238a730u;
 hwmask_requested=1;fake_bloom_level=2;fake_halo_ready=2;fake_grain_level=1;fake_soft=1;soft_calls=0;
 film_stage_arm(53);assert(!framecache_used);film_stage_observe(1);
 assert(patch_rc==0&&framecache_used&&soft_calls==1&&hw_shrinks==2&&hw_frame==0x4238a730u&&effect_order==3);
 assert(rows_in_place_now==0); /* in place only while the passes run */
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 framecache_requested=0;expect_frame=0x4238a730u;film_stage_arm(54);film_stage_observe(1);assert(!framecache_used);
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 hwmask_requested=0;fake_bloom_level=fake_halo_ready=fake_grain_level=fake_soft=0;
 camera_info.cam_uncached_bit=0;
 /* ZrnoBoy: one render of the whole frame, through the cached alias when
  * FRAMECACHE asks; no film pass runs and Canon's table is not needed (any
  * My Colors). Its result is the shot's, and the log says so. */
 {int g=graded_calls,polls=gboy_polls;
  fake_gboy=1;fake_hw_loaded=0;fake_bloom_level=2;fake_grain_level=1;
  film_stage_arm(60);film_stage_observe(1);
  assert(gboy_calls==1&&graded_calls==g&&patch_rc==0&&patch_gboy&&gboy_seed);
  film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();assert(gboy_polls>polls);
  framecache_requested=1;camera_info.cam_uncached_bit=0x40000000;expect_frame=0x0238a730u;
  gboy_rc=-110;film_stage_arm(61);film_stage_observe(1);assert(gboy_calls==2&&patch_rc==-110&&framecache_used);
  film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
  framecache_requested=0;camera_info.cam_uncached_bit=0;expect_frame=0x4238a730u;gboy_rc=0;
  fake_gboy=0;fake_hw_loaded=1;fake_bloom_level=fake_grain_level=0;
  film_stage_arm(62);film_stage_observe(1);assert(gboy_calls==2&&!patch_gboy);
  film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();}
 /* Only the DIGIC path grades: when Canon loaded the look's table for this
  * shot, the effects run on the graded frame (halation with the look's tint
  * and mono flag). When it did not (My Colors not Off) or the table was not
  * installed, the frame is Canon's own: nothing runs, and the shot fails
  * (-104). The log says which. */
 { int maps=map_calls,halos=halo_calls,graded=graded_calls;unsigned grains;
   fake_mono=1;fake_halo_ready=2;fake_bloom_level=0;
   film_stage_arm(60);film_stage_observe(1);
   assert(patch_rc==0&&hw_used&&graded_calls==graded+1&&graded_level==2&&graded_mono==1&&graded_tint&&graded_tint->red==300&&graded_tint->green==60&&graded_tint->blue==5);
   assert(map_calls==maps&&halo_calls==halos+1);
   film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
   fake_halo_ready=0;fake_mono=0;film_stage_arm(61);film_stage_observe(1);assert(graded_calls==graded+2&&graded_level==0&&!graded_mono&&map_calls==maps+1);
   film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
   fake_grain_level=1;grains=grain_calls;
   fake_hw_loaded=0;film_stage_arm(62);film_stage_observe(1);
   assert(!hw_used&&patch_hw&&patch_rc==-104&&graded_calls==graded+2&&grain_calls==grains);
   film_stage_observe(2);assert(film_stage_result(62)==-104);film_stage_finish();clock_tick+=1001;film_stage_poll();
   fake_hw_on=0;fake_hw_loaded=1;film_stage_arm(63);film_stage_observe(1);
   assert(!patch_hw&&!hw_used&&patch_rc==-104&&graded_calls==graded+2&&grain_calls==grains);
   film_stage_observe(2);assert(film_stage_result(63)==-104);film_stage_finish();clock_tick+=1001;film_stage_poll();
   fake_hw_on=1;fake_grain_level=0;
   assert(longest_write<768-37*9); }   /* room for every number at ten digits */
 fake_bloom_level=1;bloom_error=-8;int before_grain=grain_calls;
 film_stage_arm(44);film_stage_observe(1);assert(patch_rc==-8&&grain_calls==(unsigned)before_grain);
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 bloom_error=fake_bloom_level=fake_halo_ready=fake_grain_level=0;
 int previous_map_calls=map_calls;
 patch_requested=0;film_stage_arm(14);assert(!patch_ready);film_stage_observe(1);assert(map_calls==previous_map_calls);
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 /* Vignette and light-leak levels are latched per capture and passed with a
  * per-shot seed distinct from the grain seed. */
 patch_requested=1;fake_vignette=2;fake_leaks=3;film_stage_arm(16);film_stage_observe(1);
 assert(patch_rc==0&&seen_vignette==2&&seen_leaks==3&&seen_seed==(grain_seed^0xa3b195f1u)&&seen_seed!=grain_seed);
 fake_vignette=0;film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 assert(patch_vignette==2&&patch_leaks==3); /* latched until the next capture */
 assert(seen_toy==0&&!seen_toy_work);
 fake_leaks=0;fake_toy=2;film_stage_arm(17);film_stage_observe(1);assert(seen_vignette==0&&seen_leaks==0);
 assert(seen_toy==2&&seen_toy_work==fake_toy_work&&seen_toy_size==3648u*7u);fake_toy=0;
 /* Print effects: the stage latches the settings when armed (date included)
  * and hands them to the colour pass. */
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 fake_dust=3;film_stage_arm(18);film_stage_observe(1);
 assert(seen_print_set&&seen_print.dust==3&&seen_print.stamp==1&&!strcmp(seen_print.text,"'26 11 05"));fake_dust=0;
 /* Soft focus runs on the graded frame, before Bloom and grain, at the
  * latched level in the Bloom workspace. */
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 fake_soft=2;soft_calls=0;film_stage_arm(19);film_stage_observe(1);
 assert(patch_rc==0&&soft_calls==1&&seen_soft_level==2&&seen_soft_work==fake_halo);fake_soft=0;
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 /* M1, M2 and S: phase 1 sees a size Canon shrinks and leaves the full frame
  * alone. Phase 3 (in Canon's encode-step loop, after its resize step)
  * grades the shrunk buffer (stride twice the width), borrowing phase 2's
  * record as scratch as L does at phase 1, with every pixel-sized effect
  * scaled to the frame and back to L afterwards. The wrong buffer or step,
  * or an encode without phase 3, leaves the shot ungraded (-103), which the
  * RAW side reports. */
 { int maps=map_calls;
   static const unsigned sizes[3][3]={{1,2816,2112},{2,1600,1200},{4,640,480}};
   for(unsigned i=0;i<3;i++) {
     fake_size=(unsigned short)sizes[i][0];shrunk_desc=1;film_stage_arm(80+i);film_stage_observe(1);
     assert(map_calls==maps+(int)i&&patch_rc==-101&&shrunk_pending&&ready==1);
     exp_w=sizes[i][1];exp_h=sizes[i][2];exp_stride=exp_w*2;expect_frame=0x42154f30u;exp_scale=(exp_w*65536u+1824u)/3648u;
     shrunk_desc=2;film_stage_observe(3);
     assert(map_calls==maps+(int)i+1&&patch_rc==0&&graded_shrunk&&!shrunk_pending&&ready==1&&armed&&SCALES_ARE(65536u));
     film_stage_observe(3);assert(map_calls==maps+(int)i+1);      /* once per shot */
     film_stage_observe(2);assert(ready==2&&!armed&&film_stage_result(80+i)==0);
     film_stage_finish();clock_tick+=1001;film_stage_poll();
   }
   assert(exp_scale==11498u);exp_scale=65536u;maps=map_calls;fake_size=1;
   shrunk_desc=1;film_stage_arm(83);film_stage_observe(1);shrunk_bad=1;shrunk_desc=2;film_stage_observe(3);
   assert(map_calls==maps&&patch_rc==-103&&!graded_shrunk&&!shrunk_pending);
   film_stage_observe(2);assert(film_stage_result(83)==-103);
   film_stage_finish();clock_tick+=1001;film_stage_poll();
   shrunk_bad=0;shrunk_desc=1;film_stage_arm(84);film_stage_observe(1);shrunk_step=0;shrunk_desc=2;film_stage_observe(3);
   assert(map_calls==maps&&patch_rc==-103&&!graded_shrunk);shrunk_step=1;
   film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
   fake_size=2;shrunk_desc=1;film_stage_arm(85);film_stage_observe(1);shrunk_desc=2;film_stage_observe(2);
   assert(map_calls==maps&&patch_rc==-103&&!shrunk_pending&&film_stage_result(85)==-103);
   film_stage_finish();clock_tick+=1001;film_stage_poll();
   /* M3 and Low Light: refused at phase 1; phase 3 then does nothing. */
   for(unsigned short z=3;z<=7;z+=4) {
     shrunk_desc=0;fake_size=z;film_stage_arm(86);film_stage_observe(1);assert(patch_rc==-102&&!shrunk_pending);
     film_stage_observe(3);assert(map_calls==maps&&patch_rc==-102);
     film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
   }
   fake_size=0;exp_w=3648;exp_h=2736;exp_stride=7296;expect_frame=0x4238a730u; }
 film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 /* Without a ready LUT nothing is graded and no effect levels are latched. */
 patch_requested=0;fake_vignette=3;film_stage_arm(18);assert(!patch_ready&&!patch_vignette);
 film_stage_observe(1);film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();fake_vignette=0;
 /* Progress yields are time-throttled: none within 500 ms of the last yield,
  * one as soon as 500 ms have passed, and the count is reset per capture. */
 sleeps=0;yields=0;last_yield=(unsigned)clock_tick;
 for(int i=0;i<10;i++)colour_progress(32*(i+1),2736);
 assert(sleeps==0&&yields==0);
 clock_tick+=500;colour_progress(352,2736);assert(sleeps==1&&yields==1);
 colour_progress(384,2736);assert(sleeps==1);
 clock_tick+=490;colour_progress(416,2736);assert(sleeps==1); /* the stub clock also ticks once per read */
 clock_tick+=20;colour_progress(448,2736);assert(sleeps==2&&yields==2);
 film_stage_arm(15);assert(yields==0);film_stage_observe(1);film_stage_observe(2);film_stage_finish();clock_tick+=1001;film_stage_poll();
 free(records);records=0;marker=0;film_stage_init(1);assert(!enabled);film_stage_arm(11);assert(!armed);
 puts("Synchronized observer: bounded reads, phase ordering, no callback I/O, selected-LUT latch/logging, full-frame geometry, marker/format guards, pending-data protection and delayed swap release passed");
}
