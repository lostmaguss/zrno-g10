/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual LUT manager with temporary host filesystem and fault injection. */
#include "camera_info.h"
#include "../camera/film_colour.h"
#include "../camera/film_grain.h"
#include "../camera/film_bloom.h"
#include <assert.h>
static int writes,reads,fail_write,busy,shooting,restores,fail_bloom_alloc,fail_halo_alloc,fail_grain_alloc,now,change_during_read,change_during_write;
static void inject_change(void);
static void *fault_malloc(size_t n){if(fail_bloom_alloc&&n==film_bloom_work_size(3648))return 0;if(fail_grain_alloc&&n==film_grain_work_size(3648))return 0;if(fail_halo_alloc&&n==film_halo_work_size(3648))return 0;return malloc(n);}
static ssize_t fault_write(int fd,const void *p,size_t n) {
    writes++;
    if(change_during_write){change_during_write=0;inject_change();}
    if(fail_write)return write(fd,p,n/2); /* Interrupted inactive record. */
    return write(fd,p,n);
}
static ssize_t count_read(int fd,void *p,size_t n){reads++;int result=read(fd,p,n);if(change_during_read){change_during_read=0;inject_change();}return result;}
#define write fault_write
#define read count_read
#define malloc fault_malloc
#include "film_clock.h"   /* the stub date, before film_lut.c picks the camera clock */
/* Canon's preset pointers, identity table and loaded table, mocked. */
static unsigned canon_off[1000],canon_loaded[1000];
static const unsigned *canon_presets[14];
#define FILM_HW_PRESETS ((const unsigned *volatile *)canon_presets)
#define FILM_HW_CANON_OFF ((const unsigned *)canon_off)
#define FILM_HW_LOADED ((const volatile unsigned *)canon_loaded)
static unsigned fcreate_code[3]={0xe5970008u,0xe3500000u,0xa58402b4u},fcreate_pool=0x5b38u;
static int canon_sharpen=-1;
static unsigned short canon_angel[504] __attribute__((aligned(4)));
static const unsigned short *canon_angel_ptr=canon_angel;
#define FILM_HW_SHARPEN_WORD ((volatile int *)&canon_sharpen)
#define FILM_HW_FCREATE ((const unsigned *)fcreate_code)
#define FILM_HW_FCREATE_POOL ((const unsigned *)&fcreate_pool)
#define FILM_HW_ANGEL ((const unsigned short *const volatile *)&canon_angel_ptr)
static unsigned reload_code[16]={0xe92d4010u,0xe51f4bbcu,0,0,0,0,0,0,0,0,0,0,0,0,0xeb02ffa5u,0xeb03abbdu};
static int reloads;
#define FILM_HW_RELOAD ((const unsigned *)reload_code)
#define FILM_HW_RELOAD_CALL() (reloads++)
static int preview_ok;
int film_preview_reloadable(void){return preview_ok;}
#include "../camera/film_hwcolour.c"
/* ZrnoBoy engine switch (camera/film_gbui.c). */
static int gboy_engine;                 /* 0 Film, 1 ZrnoBoy, 2 Print, 3 Grade */
int film_gboy_engine(void){return gboy_engine==1;}
int film_grade_engine(void){return gboy_engine==3;}
int film_engine_alt(void){return gboy_engine!=0;}
/* Grade's tables (camera/film_grui.c): two buffers, never the one
 * Canon has; each build marks its table with the grade's serial. */
static unsigned grade_serial=1,grade_tables[2][1000];
static int grade_builds;
unsigned film_grui_serial(void){return grade_serial;}
const unsigned *film_grui_build(const unsigned *avoid){
 unsigned *t=avoid==grade_tables[0]?grade_tables[1]:grade_tables[0];int i;
 assert(avoid!=t);grade_builds++;
 for(i=0;i<1000;i++)t[i]=(grade_serial*7u+(unsigned)i)&0xfffffu;
 return t;
}
/* What looks read and set in the other engines (film_gbui.c, film_rsui.c,
 * film_grui.c): each engine's settings, the engine switch counted. */
#include "../camera/film_gbui.h"
#include "../camera/film_rsui.h"
#include "../camera/film_grui.h"
static film_gboy_settings stub_gboy;
static film_riso_settings stub_riso;
static film_grade_settings stub_grades[FILM_GRUI_SLOTS];
static int stub_slot,engine_switches;
int film_gbui_engine(void){return gboy_engine;}
void film_gbui_set_engine(int e){if(e>=0&&e<FILM_ENGINES&&e!=gboy_engine){gboy_engine=e;engine_switches++;}}
void film_gbui_get(film_gboy_settings *g){*g=stub_gboy;}
void film_gbui_set(const film_gboy_settings *g){stub_gboy=*g;}
void film_rsui_get(film_riso_settings *r){*r=stub_riso;}
void film_rsui_set(const film_riso_settings *r){stub_riso=*r;}
void film_grui_get(int *slot,film_grade_settings *g){*slot=stub_slot;*g=stub_grades[stub_slot];}
void film_grui_set(int slot,const film_grade_settings *g){assert(slot>=0&&slot<FILM_GRUI_SLOTS&&film_grade_valid(g));stub_slot=slot;stub_grades[slot]=*g;grade_serial++;}
#include "../camera/film_lut.c"
#undef write
#undef read
#undef malloc
static char shown[32];
int shooting_in_progress(void){return shooting;}
int film_ui_busy(void){return busy;}
void film_ui_set_look(const char *s){strcpy(shown,s);}
static char preset_shown[32];
void film_ui_set_preset(const char *s){strcpy(preset_shown,s?s:"");}
void film_ui_set_halation(int enabled){(void)enabled;}
void film_ui_set_grain(int level){(void)level;}
void film_ui_set_bloom(int level){(void)level;}
static int ui_processing=-1,ui_indicator=-1;
void film_ui_set_processing(int on){ui_processing=on;}
void film_ui_set_indicator(int where){ui_indicator=where;}
static int ui_vignette=-1,ui_leaks=-1;
void film_ui_set_vignette(int l){ui_vignette=l;}
void film_ui_set_leaks(int l){ui_leaks=l;}
static int ui_leds=-1;
void film_ui_set_leds(int on){ui_leds=on;}
static int ui_toy=-1;
void film_ui_set_toy(int l){ui_toy=l;}
static int ui_dust=-1,ui_stamp=-1;
void film_ui_set_print(int dust,int stamp){ui_dust=dust;ui_stamp=stamp;}
static int ui_soft=-1;
void film_ui_set_soft(int level){ui_soft=level;}
void gui_set_need_restore(void){restores++;}
static int menu_up,redraws;
int film_menu_showing(void){return menu_up;}
void gui_set_need_redraw(void){redraws++;}
int get_tick_count(void){return now;}
static void inject_change(void){film_lut_menu_grain(1,0);}
static void reset(void) {
    free(resident);hw_installed=0;free(fx_work);fx_bytes=0;free(grain_work);grain_work=0;fx_work=0;resident=0;count=choice=initialized=legacy=0;
    bloom_choice=bloom_active=requested_bloom=fail_bloom_alloc=0;on_choice=on_active=requested_on=0;osd_choice=osd_active=requested_osd=FILM_UI_INDICATOR_BOTTOM;vig_choice=vig_active=requested_vig=leak_choice=leak_active=requested_leak=0;leds_choice=leds_active=requested_leds=1;sbtn_choice=sbtn_active=requested_sbtn=1;toy_choice=toy_active=requested_toy=0;ui_toy=-1;
    dust_choice=dust_active=requested_dust=stamp_choice=stamp_active=requested_stamp=smode_choice=smode_active=requested_smode=0;
    soft_choice=soft_active=requested_soft=0;ui_soft=-1;hcol_choice=hcol_active=requested_hcol=0;
    stext_choice[0]=stext_active[0]=requested_text[0]=0;ui_dust=ui_stamp=-1;stub_tbox_select=0;ui_processing=ui_indicator=-1;
    halo_choice=halo_active=requested_halo=fail_halo_alloc=grain_choice=grain_active=requested_grain=fail_grain_alloc=0;
    active=current_slot=-1;generation=0;request=-1;loading=0;request_version=handled_version=request_tick=last_failed=0;change_during_read=change_during_write=0;now=0;
    status_text="Starting";busy=shooting=fail_write=0;camera_info.state.state_kbd_script_run=0;
}
static void put(const char *path,const void *p,size_t n) {
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(p,1,n,f)==n);assert(!fclose(f));
}
static void copy(const char *from,const char *to) {
    FILE *f=fopen(from,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);
    void *p=malloc(n);assert(p);assert(fread(p,1,n,f)==(size_t)n);fclose(f);put(to,p,n);free(p);
}
static void settled(void){now+=250;film_lut_poll(0);}
/* The card's real tables for every look, from the source card directory. */
static void restore_tables(const char *card,const char *const *files,unsigned n) {
 char path[1024],dest[64];
 for(unsigned i=0;i<n;i++) {
  snprintf(path,sizeof(path),"%s/LUTS/%s",card,files[i]);strcpy(strrchr(path,'.'),".HWT");
  snprintf(dest,sizeof(dest),"A/CHDK/FILM/LUTS/%s",files[i]);strcpy(strrchr(dest,'.'),".HWT");copy(path,dest);
 }
}
int main(int argc,char **argv) {
 assert(argc==3);char path[1024];assert(!chdir(argv[1]));
 assert(!mkdir("A",0700));assert(!mkdir("A/CHDK",0700));assert(!mkdir("A/CHDK/FILM",0700));assert(!mkdir("A/CHDK/FILM/LUTS",0700));
 const char *files[]={"GOLD17.LUT","PORTRA.LUT","VELVIA.LUT","KCHROME.LUT","HP5.LUT","EKTAR.LUT","P800.LUT","F400H.LUT","PROVIA.LUT","ASTIA.LUT","SUPERIA.LUT","POLA669.LUT","TRIX.LUT","ACROS.LUT","BLEACH.LUT","TEALORG.LUT"};
 /* Each look is its .HWT; the .LUT beside it is only the catalogue's name. */
 for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);i++){snprintf(path,sizeof(path),"%s/LUTS/%s",argv[2],files[i]);char dest[64];snprintf(dest,sizeof(dest),"A/CHDK/FILM/LUTS/%s",files[i]);copy(path,dest);}
 restore_tables(argv[2],files,sizeof(files)/sizeof(files[0]));
 snprintf(path,sizeof(path),"%s/LUTS.TXT",argv[2]);copy(path,"A/CHDK/FILM/LUTS.TXT");
 for(int k=0;k<10;k++)canon_off[111*k]=(64u*k)<<20;
 canon_presets[0]=canon_off;
 film_lut_init();assert(active==0&&count==16&&resident&&!film_lut_hw_table()&&canon_presets[0]==canon_off);
 int oldreads=reads,oldwrites=writes;void *old=resident;
 film_lut_menu_choice(1,0);assert(request==1&&resident==old&&reads==oldreads&&writes==oldwrites);
 now=249;film_lut_poll(0);assert(active==0);now=250;film_lut_poll(1);assert(active==0);
 busy=1;film_lut_poll(0);assert(active==0&&!strcmp(film_lut_menu_status(0,0),"After capture"));busy=0;
 shooting=1;film_lut_poll(0);assert(active==0);shooting=0;
 camera_info.state.state_kbd_script_run=1;film_lut_poll(0);assert(active==0);camera_info.state.state_kbd_script_run=0;
 film_lut_poll(0);assert(active==1&&generation==1&&handled_version==request_version);
 reset();film_lut_init();assert(active==1);
 /* Rapid edits coalesce; return to the already active tuple writes nothing. */
 oldwrites=writes;film_lut_menu_halo(1,0);now+=100;film_lut_menu_grain(2,0);now+=100;film_lut_menu_halo(-1,0);film_lut_menu_grain(-2,0);
 settled();assert(writes==oldwrites&&halo_active==0&&grain_active==0);
 oldreads=reads;old=resident;film_lut_menu_halo(3,0);film_lut_menu_grain(2,0);settled();
 assert(active==1&&halo_active==3&&grain_active==2&&resident==old&&reads==oldreads+1); /* only save readback */
 reset();film_lut_init();assert(active==1&&halo_active==3&&grain_active==2);
 /* No side effects when menu renderer queries the current labels. */
 oldreads=reads;oldwrites=writes;unsigned revision=request_version;
 film_lut_menu_choice(0,0);film_lut_menu_halo(0,0);film_lut_menu_grain(0,0);film_lut_menu_status(0,0);
 assert(reads==oldreads&&writes==oldwrites&&request_version==revision);
 /* Failed save displays actual settings; the next edit starts there. */
 old=resident;void *kept_halo=fx_work,*kept_grain=grain_work;fail_write=1;
 film_lut_menu_grain(-1,0);settled();assert(resident==old&&fx_work==kept_halo&&grain_work==kept_grain&&grain_active==2);
 assert(!strcmp(film_lut_menu_grain(0,0),"Mid")&&!strcmp(film_lut_menu_status(0,0),"Save failed"));
 fail_write=0;film_lut_menu_grain(1,0);settled();assert(grain_active==3);
 /* A key event during LUT loading survives; stale LUT is never saved. */
 film_lut_menu_choice(1,0);oldwrites=writes;change_during_read=1;settled();
 assert(active==1&&writes==oldwrites&&handled_version!=request_version);
 settled();assert(active==2&&grain_active==0);
 /* A key event during selection write also survives completion of old request. */
 film_lut_menu_grain(1,0);change_during_write=1;settled();assert(grain_active==1&&handled_version!=request_version);
 settled();assert(grain_active==2&&handled_version==request_version);
 reset();film_lut_init();assert(active==2&&grain_active==2&&halo_active==3);
 /* A corrupt/missing table cannot leave the menu claiming an inactive look. */
 put("A/CHDK/FILM/LUTS/KCHROME.HWT","bad",3);film_lut_menu_choice(1,0);settled();
 assert(active==2&&!strcmp(film_lut_menu_choice(0,0),"Velvia 50")&&!strcmp(film_lut_menu_status(0,0),"LUT load failed"));
 film_lut_menu_choice(2,0);settled();assert(active==4);
 /* Allocation error reverts only to the committed tuple. */
 film_lut_menu_halo(1,0);film_lut_menu_grain(2,0);settled();assert(!fx_work&&!grain_work);
 old=resident;fail_halo_alloc=1;film_lut_menu_halo(1,0);settled();assert(resident==old&&!halo_active&&!fx_work&&!strcmp(film_lut_menu_halo(0,0),"Off"));fail_halo_alloc=0;
 fail_grain_alloc=1;film_lut_menu_grain(1,0);settled();assert(!grain_active&&!grain_work&&!strcmp(film_lut_menu_grain(0,0),"Off"));fail_grain_alloc=0;
 /* No endless retry after failure. */
 oldwrites=writes;oldreads=reads;settled();assert(writes==oldwrites&&reads==oldreads);
 /* Latest choices requested during a capture apply once the stage is idle. */
 busy=1;film_lut_menu_halo(2,0);film_lut_menu_grain(3,0);settled();assert(!halo_active&&!grain_active);busy=0;
 settled();assert(halo_active==2&&grain_active==3);reset();film_lut_init();assert(active==4&&halo_active==2&&grain_active==3);
 /* All 16 slots load, including last-to-first and first-to-last wrapping. */
 for(int index=5;index<16;index++) {
  film_lut_menu_choice(index-active,0);settled();assert(active==index&&resident);
  assert(!strcmp(film_lut_menu_choice(0,0),looks[index].label));
 }
 reset();film_lut_init();assert(active==15&&count==16&&halo_active==2&&grain_active==3);
 film_lut_menu_choice(1,0);settled();assert(active==0);
 film_lut_menu_choice(-1,0);settled();assert(active==15);
 /* Bloom shares the halo allocation; changes retain all four tuple fields. */
 void *shared=fx_work;oldreads=reads;
 film_lut_menu_bloom(2,0);settled();assert(bloom_active==2&&halo_active==2&&grain_active==3&&fx_work==shared&&film_lut_bloom_work()==film_lut_halo_work());
 assert(reads==oldreads+1);reset();film_lut_init();assert(active==15&&bloom_active==2&&halo_active==2&&grain_active==3);
 film_lut_menu_halo(2,0);film_lut_menu_grain(1,0);settled();assert(!halo_active&&!grain_active&&bloom_active==2&&fx_work);
 film_lut_menu_bloom(2,0);settled();assert(!bloom_active&&!fx_work&&!fx_bytes);
 fail_bloom_alloc=1;film_lut_menu_bloom(1,0);settled();assert(!bloom_active&&!fx_work&&!strcmp(film_lut_menu_bloom(0,0),"Off"));fail_bloom_alloc=0;
 film_lut_menu_bloom(2,0);settled();assert(bloom_active==2&&fx_bytes==film_bloom_work_size(3648));
 shared=fx_work;fail_halo_alloc=1;film_lut_menu_halo(1,0);settled();assert(!halo_active&&bloom_active==2&&fx_work==shared&&fx_bytes==film_bloom_work_size(3648));fail_halo_alloc=0;
 film_lut_menu_halo(1,0);settled();assert(halo_active==1&&bloom_active==2&&fx_bytes==film_halo_work_size(3648));
 shared=fx_work;fail_write=1;film_lut_menu_bloom(1,0);settled();assert(bloom_active==2&&fx_work==shared&&!strcmp(film_lut_menu_bloom(0,0),"Mid"));fail_write=0;
 film_lut_menu_bloom(1,0);change_during_write=1;settled();assert(bloom_active==3&&handled_version!=request_version);settled();assert(bloom_active==3&&grain_active==1);
 reset();film_lut_init();assert(bloom_active==3&&grain_active==1&&halo_active==1);
 /* Migrate the newest old tuple without modifying either rollback record. */
 reset();unlink("A/CHDK/FILM/FXSEL0.CFG");unlink("A/CHDK/FILM/FXSEL1.CFG");
 unsigned char old_record[32],check_record[32];memset(old_record,0,32);memcpy(old_record,"G12FX01",8);put32(old_record+8,90);strcpy((char*)old_record+12,"HP5.LUT");old_record[25]=3;old_record[26]=2;old_record[27]=1;put32(old_record+28,checksum(old_record,28));
 put("A/CHDK/FILM/FILMSEL0.CFG",old_record,32);film_lut_init();assert(active==4&&halo_active==3&&grain_active==2&&!bloom_active&&current_slot==-1);
 assert(access("A/CHDK/FILM/FXSEL0.CFG",F_OK));film_lut_menu_bloom(1,0);settled();assert(bloom_active==1);
 assert(read_film_selection(0,check_record,1)==4&&!memcmp(check_record,old_record,32));
 reset();film_lut_init();assert(bloom_active==1&&halo_active==3&&grain_active==2);
 /* A checksum-valid but invalid Bloom value is rejected before migration. */
 unsigned char invalid[32];assert(read_selection(0,invalid)==4);invalid[27]=4;put32(invalid+28,checksum(invalid,28));put("A/CHDK/FILM/FXSEL0.CFG",invalid,32);
 reset();film_lut_init();assert(active==4&&halo_active==3&&grain_active==2&&!bloom_active);
 reset();unlink("A/CHDK/FILM/FILMSEL0.CFG");unlink("A/CHDK/FILM/FXSEL0.CFG");
 memcpy(old_record,"G12SEL1",8);old_record[25]=1;old_record[26]=3;old_record[27]=1;put32(old_record+28,checksum(old_record,28));put("A/CHDK/FILM/LUTSEL0.CFG",old_record,32);
 film_lut_init();assert(active==4&&halo_active==3&&!grain_active&&!bloom_active);
 reset();puts("Bloom settings: independent levels, shared memory/reuse/failed upgrade, failed-save rollback, concurrent edits, persistence, old FILMSEL/LUTSEL migration and invalid Bloom rejection passed");
 /* Film processing switch: defaults Off, persists as a marker file, restores on boot,
  * coalesces edits, waits for capture and reports a failed write without
  * changing the route. */
 reset();unlink(PROCESSING_MARKER);film_lut_init();
 assert(!film_lut_processing()&&!strcmp(film_lut_menu_processing(0,0),"Off"));
 oldreads=reads;oldwrites=writes;revision=request_version;film_lut_menu_processing(0,0);
 assert(reads==oldreads&&writes==oldwrites&&request_version==revision);
 assert(!strcmp(film_lut_menu_processing(1,0),"On")&&!film_lut_processing());
 settled();assert(film_lut_processing()&&processing_stored()&&!strcmp(film_lut_menu_status(0,0),"Ready"));
 reset();film_lut_init();assert(film_lut_processing()&&!strcmp(film_lut_menu_processing(0,0),"On"));
 oldwrites=writes;film_lut_menu_processing(1,0);now+=100;film_lut_menu_processing(1,0);settled();
 assert(film_lut_processing()&&processing_stored()&&writes==oldwrites);
 film_lut_menu_processing(-1,0);settled();assert(!film_lut_processing()&&!processing_stored());
 fail_write=1;film_lut_menu_processing(1,0);settled();
 assert(!film_lut_processing()&&!processing_stored()&&!strcmp(film_lut_menu_status(0,0),"Save failed"));
 assert(!strcmp(film_lut_menu_processing(0,0),"Off"));
 fail_write=0;film_lut_menu_processing(1,0);settled();assert(film_lut_processing()&&processing_stored());
 film_lut_menu_processing(1,0);shooting=1;now+=250;film_lut_poll(0);assert(film_lut_processing());
 shooting=0;settled();assert(!film_lut_processing()&&!processing_stored());
 reset();film_lut_init();assert(!film_lut_processing());
 assert(ui_processing==0); /* the UI always mirrors the stored switch */
 film_lut_menu_processing(1,0);settled();assert(ui_processing==1);film_lut_menu_processing(1,0);settled();assert(ui_processing==0);
 /* Indicator: Bottom by default with no file, cycles both ways, persists,
  * and a failed write keeps the stored position. */
 reset();unlink(INDICATOR_FILE);film_lut_init();
 assert(ui_indicator==FILM_UI_INDICATOR_BOTTOM&&!strcmp(film_lut_menu_indicator(0,0),"Bottom"));
 assert(!strcmp(film_lut_menu_indicator(1,0),"Top"));settled();
 assert(ui_indicator==FILM_UI_INDICATOR_TOP&&indicator_stored()==FILM_UI_INDICATOR_TOP);
 reset();film_lut_init();assert(ui_indicator==FILM_UI_INDICATOR_TOP&&!strcmp(film_lut_menu_indicator(0,0),"Top"));
 assert(!strcmp(film_lut_menu_indicator(1,0),"Off"));settled();assert(ui_indicator==FILM_UI_INDICATOR_OFF);
 assert(!strcmp(film_lut_menu_indicator(-1,0),"Top"));assert(!strcmp(film_lut_menu_indicator(-1,0),"Bottom"));
 settled();assert(ui_indicator==FILM_UI_INDICATOR_BOTTOM);struct stat ist;assert(stat(INDICATOR_FILE,&ist)); /* default leaves no file */
 fail_write=1;film_lut_menu_indicator(1,0);settled();
 assert(ui_indicator==FILM_UI_INDICATOR_BOTTOM&&!strcmp(film_lut_menu_status(0,0),"Save failed")&&!strcmp(film_lut_menu_indicator(0,0),"Bottom"));
 fail_write=0;film_lut_menu_indicator(1,0);settled();assert(ui_indicator==FILM_UI_INDICATOR_TOP);
 /* A failed write from Top must keep Top, not fall back to the default. */
 fail_write=1;film_lut_menu_indicator(1,0);settled();
 assert(ui_indicator==FILM_UI_INDICATOR_TOP&&indicator_stored()==FILM_UI_INDICATOR_TOP&&!strcmp(film_lut_menu_indicator(0,0),"Top"));
 { struct stat tst;assert(stat("A/CHDK/FILM/FILMOSD.TMP",&tst)); } /* no temporary file left behind */
 fail_write=0;film_lut_menu_indicator(-1,0);settled();assert(ui_indicator==FILM_UI_INDICATOR_BOTTOM);
 fail_write=0;reset();film_lut_init();assert(ui_indicator==FILM_UI_INDICATOR_BOTTOM);
 puts("Indicator: default Bottom without a file, both-way cycling, persistence, UI publication and failed-write rollback passed");
 /* Vignette and light leaks: Off without a file, independent Off/Low/Mid/High
  * levels with wraparound, persistence, an invalid record treated as Off and
  * failed writes keeping the stored levels. */
 reset();unlink(RETRO_FILE);film_lut_init();
 assert(!film_lut_vignette_level()&&!film_lut_leak_level()&&ui_vignette==0&&ui_leaks==0);
 assert(!strcmp(film_lut_menu_vignette(2,0),"Mid")&&!strcmp(film_lut_menu_leaks(-1,0),"High"));settled();
 assert(film_lut_vignette_level()==2&&film_lut_leak_level()==3&&ui_vignette==2&&ui_leaks==3);
 reset();film_lut_init();assert(film_lut_vignette_level()==2&&film_lut_leak_level()==3);
 film_lut_menu_leaks(1,0);settled();assert(film_lut_leak_level()==0&&film_lut_vignette_level()==2);
 fail_write=1;film_lut_menu_vignette(1,0);settled();
 assert(film_lut_vignette_level()==2&&!strcmp(film_lut_menu_status(0,0),"Save failed")&&!strcmp(film_lut_menu_vignette(0,0),"Mid"));
 { struct stat tst;assert(stat("A/CHDK/FILM/RETRO.TMP",&tst)); }
 reset();film_lut_init();assert(film_lut_vignette_level()==2); /* the stored record survived the failed write */
 fail_write=0;
 { unsigned char bad[16];memset(bad,0,16);memcpy(bad,"G12RT01",8);bad[8]=7;put32(bad+12,checksum(bad,12));put(RETRO_FILE,bad,16); }
 reset();film_lut_init();assert(!film_lut_vignette_level()&&!film_lut_leak_level());
 { unsigned char bad[16];memset(bad,0,16);memcpy(bad,"G12RT01",8);bad[8]=1;put32(bad+12,checksum(bad,12)^1);put(RETRO_FILE,bad,16); }
 reset();film_lut_init();assert(!film_lut_vignette_level());
 reset();unlink(RETRO_FILE);
 puts("Vignette/leaks settings: default Off, independent levels and wraparound, persistence, invalid/corrupt records as Off and failed-write rollback passed");
 /* Busy LEDs: On without a marker, Off creates it, On removes it, persists,
  * and a failed write keeps them On. */
 reset();unlink(LEDS_OFF_MARKER);film_lut_init();
 assert(film_lut_leds()&&ui_leds==1&&!strcmp(film_lut_menu_leds(0,0),"On"));
 fail_write=1;film_lut_menu_leds(1,0);settled();
 assert(film_lut_leds()&&ui_leds==1&&!strcmp(film_lut_menu_status(0,0),"Save failed")&&!strcmp(film_lut_menu_leds(0,0),"On"));
 fail_write=0;film_lut_menu_leds(1,0);settled();assert(!film_lut_leds()&&ui_leds==0&&!leds_stored());
 reset();film_lut_init();assert(!film_lut_leds()&&ui_leds==0);
 film_lut_menu_leds(1,0);settled();assert(film_lut_leds()&&leds_stored());
 { struct stat lst;assert(stat(LEDS_OFF_MARKER,&lst)&&stat("A/CHDK/FILM/LEDOFF.TMP",&lst)); }
 /* S button: opens the Film simulation menu by default (no marker); CHDK
  * writes SCHDK.TXT and survives a reload; back to Film menu removes it; a
  * failed write keeps what is stored. */
 reset();unlink(SBUTTON_CHDK_MARKER);film_lut_init();
 assert(film_lut_s_opens_film_menu()&&!strcmp(film_lut_menu_sbutton(0,0),"Zrno menu"));
 assert(!strcmp(film_lut_menu_sbutton(1,0),"CHDK"));settled();
 { struct stat st;assert(!film_lut_s_opens_film_menu()&&!stat(SBUTTON_CHDK_MARKER,&st)&&stat("A/CHDK/FILM/SCHDK.TMP",&st)); }
 reset();film_lut_init();assert(!film_lut_s_opens_film_menu());
 film_lut_menu_sbutton(1,0);settled();
 { struct stat st;assert(film_lut_s_opens_film_menu()&&stat(SBUTTON_CHDK_MARKER,&st)); }
 fail_write=1;film_lut_menu_sbutton(1,0);settled();
 { struct stat st;assert(film_lut_s_opens_film_menu()&&stat(SBUTTON_CHDK_MARKER,&st)&&!strcmp(film_lut_menu_status(0,0),"Save failed")); }
 fail_write=0;
 puts("S button: default Film menu, CHDK marker persistence and reload, removal and failed-write rollback passed");
 puts("Busy LEDs: default On without a marker, Off/On persistence, UI publication and failed-write rollback passed");
 /* Toy lens: Off without a record, levels persist in RETRO.CFG byte 10 beside
  * vignette and leaks, a record from before (byte 10 = 0) reads as Off, and
  * its workspace follows halation in the pool, which grows only while on. */
 reset();unlink(RETRO_FILE);film_lut_init();
 assert(!film_lut_toy_level()&&!film_lut_toy_work()&&ui_toy==0);
 film_lut_menu_vignette(1,0);settled();assert(film_lut_vignette_level()==1&&!film_lut_toy_level());
 size_t plain=fx_bytes;
 assert(!strcmp(film_lut_menu_toy(2,0),"Mid"));settled();
 assert(film_lut_toy_level()==2&&ui_toy==2&&film_lut_vignette_level()==1);
 assert(fx_bytes>=plain+film_toy_work_size(3648)&&film_lut_toy_work());
 assert((unsigned char*)film_lut_toy_work()+film_toy_work_size(3648)<=(unsigned char*)fx_work+fx_bytes);
 if(!halo_active){film_lut_menu_halo(1,0);settled();}
 assert(halo_active>0&&(unsigned char*)film_lut_toy_work()>=(unsigned char*)fx_work+film_halo_work_size(3648));
 assert((unsigned char*)film_lut_toy_work()+film_toy_work_size(3648)<=(unsigned char*)fx_work+fx_bytes);
 reset();film_lut_init();assert(film_lut_toy_level()==2&&film_lut_vignette_level()==1&&film_lut_toy_work());
 film_lut_menu_toy(-2,0);settled();assert(!film_lut_toy_level()&&!film_lut_toy_work()&&film_lut_vignette_level()==1);
 fail_write=1;film_lut_menu_toy(3,0);settled();
 assert(!film_lut_toy_level()&&!strcmp(film_lut_menu_status(0,0),"Save failed")&&!strcmp(film_lut_menu_toy(0,0),"Off"));
 fail_write=0;
 /* A record written by the vignette/leaks firmware (byte 10 zero) keeps its levels. */
 { unsigned char old[16];memset(old,0,16);memcpy(old,"G12RT01",8);old[8]=2;old[9]=1;put32(old+12,checksum(old,12));put(RETRO_FILE,old,16); }
 reset();film_lut_init();assert(film_lut_vignette_level()==2&&film_lut_leak_level()==1&&!film_lut_toy_level());
 { unsigned char bad[16];memset(bad,0,16);memcpy(bad,"G12RT01",8);bad[10]=5;put32(bad+12,checksum(bad,12));put(RETRO_FILE,bad,16); }
 reset();film_lut_init();assert(!film_lut_toy_level()&&!film_lut_vignette_level());
 /* Soft focus, dust and sharpening share RETRO.CFG byte 11 (bits 2-3, 4-5,
  * 6-7). Bits 0-1 held toning, which is gone: a record that has them set
  * still reads. */
 { unsigned char rec[16];memset(rec,0,16);memcpy(rec,"G12RT01",8);rec[8]=1;rec[11]=0x45;put32(rec+12,checksum(rec,12));put(RETRO_FILE,rec,16); }
 reset();film_lut_init();assert(film_lut_vignette_level()==1&&soft_active==1&&!dust_active&&sharp_active==1);
 reset();unlink(RETRO_FILE);
 puts("Toy lens settings: default Off, RETRO.CFG byte 10 persistence beside vignette/leaks, old records as Off, invalid levels rejected, pool layout after halation and failed-write rollback passed");
 /* Dust and the date stamp: Off by default; dust persists in RETRO.CFG
  * byte 11 beside the other levels; the stamp corner, content and
  * text persist in STAMP.CFG; the text box takes any printable ASCII. */
 reset();unlink(RETRO_FILE);unlink(STAMP_FILE);film_lut_init();
 { film_print_settings ps;film_lut_print_settings(&ps);assert(!ps.dust&&!ps.stamp&&!ps.text[0]&&ui_stamp==0); }
 assert(!strcmp(film_lut_menu_dust(-1,0),"High"));
 film_lut_menu_vignette(2,0);settled();
 assert(dust_active==3&&film_lut_vignette_level()==2&&ui_dust==3);
 { unsigned char rec[16];FILE *f=fopen(RETRO_FILE,"rb");assert(f&&fread(rec,1,16,f)==16);fclose(f);assert(rec[8]==2&&rec[11]==0x30); }
 assert(!strcmp(film_lut_menu_stamp(1,0),"Bottom right"));settled();
 { film_print_settings ps;film_lut_print_settings(&ps);assert(ps.stamp==1&&!strcmp(ps.text,"'26 11 05")&&ps.dust==3); }
 film_lut_menu_stamp_mode(1,0);settled();
 { film_print_settings ps;stub_year=2031;stub_month=2;stub_day=28;film_lut_print_settings(&ps);assert(!strcmp(ps.text,"28 02 '31")); }
 /* Text entry: the box opens with the current text and a 24-character limit;
  * every printable ASCII character is kept, others become '?', and entering
  * text switches the stamp to show it. Cancel changes nothing. */
 film_lut_menu_stamp_text(1,0);assert(stub_tbox_select&&stub_tbox_max==FILM_PRINT_TEXT&&!stub_tbox_default[0]);
 stub_tbox_select(0);settled();assert(smode_active==1&&!stext_active[0]);
 film_lut_menu_stamp_text(1,0);stub_tbox_select("Hi *\\|~{}\x80!");
 assert(!strcmp(film_lut_menu_stamp_text(0,0),"Hi *\\|~{}?!"));settled();
 assert(smode_active==2&&!strcmp(stext_active,"Hi *\\|~{}?!"));
 { film_print_settings ps;film_lut_print_settings(&ps);assert(!strcmp(ps.text,"Hi *\\|~{}?!")&&ps.stamp==1); }
 { char all[96];int i;for(i=0;i<FILM_PRINT_TEXT;i++)all[i]=(char)(32+i*4);all[i]=0;film_lut_menu_stamp_text(1,0);stub_tbox_select(all);settled();
   assert(!strncmp(stext_active,all,FILM_PRINT_TEXT)&&strlen(stext_active)==FILM_PRINT_TEXT); }
 film_lut_menu_stamp_text(1,0);stub_tbox_select("A long text that is far over the limit");settled();assert(strlen(stext_active)==FILM_PRINT_TEXT);
 assert(!strcmp(film_lut_menu_stamp_text(0,0),"A long tex.."));
 reset();film_lut_init();assert(stamp_active==1&&smode_active==2&&strlen(stext_active)==FILM_PRINT_TEXT&&dust_active==3);
 /* A failed write keeps what is stored; a damaged record reads as Off. */
 fail_write=1;film_lut_menu_stamp(1,0);settled();assert(stamp_active==1&&!strcmp(film_lut_menu_status(0,0),"Save failed"));fail_write=0;
 { unsigned char rec[40];FILE *f=fopen(STAMP_FILE,"rb");assert(f&&fread(rec,1,40,f)==40);fclose(f);rec[12]=7;put(STAMP_FILE,rec,40); }
 reset();film_lut_init();assert(!stamp_active&&!stext_active[0]);
 /* Dust shows dark on slide and instant looks, light on negative looks. */
 { int i;film_print_settings ps;for(i=0;i<count;i++){active=i;film_lut_print_settings(&ps);
   assert(ps.dust_dark==(!strcmp(looks[i].file,"VELVIA.LUT")||!strcmp(looks[i].file,"KCHROME.LUT")||!strcmp(looks[i].file,"PROVIA.LUT")||!strcmp(looks[i].file,"ASTIA.LUT")||!strcmp(looks[i].file,"POLA669.LUT")));} }
 reset();unlink(RETRO_FILE);unlink(STAMP_FILE);
 /* Soft focus: Off by default; persists in byte 11 bits 2-3 beside dust
  * and sharpening; needs the Bloom workspace in the pool even with Bloom off, and frees
  * it again when turned off; a failed write keeps what is stored. */
 reset();film_lut_init();assert(!film_lut_soft_level()&&!film_lut_soft_work()&&ui_soft==0&&!bloom_active);
 if(halo_active){film_lut_menu_halo(4-halo_active,0);settled();}
 assert(!halo_active&&!bloom_active&&!toy_active&&fx_bytes<film_bloom_work_size(3648));
 { size_t plain=fx_bytes;
   assert(!strcmp(film_lut_menu_soft(2,0),"Mid"));settled();
   assert(film_lut_soft_level()==2&&film_lut_soft_work()==fx_work&&ui_soft==2&&fx_bytes>=film_bloom_work_size(3648)&&fx_bytes>plain);
   unsigned char rec[16];FILE *f=fopen(RETRO_FILE,"rb");assert(f&&fread(rec,1,16,f)==16);fclose(f);assert(rec[11]==(2<<2));
   reset();film_lut_init();assert(film_lut_soft_level()==2&&film_lut_soft_work());
   fail_write=1;film_lut_menu_soft(1,0);settled();assert(film_lut_soft_level()==2&&!strcmp(film_lut_menu_status(0,0),"Save failed"));fail_write=0;
   film_lut_menu_soft(-2,0);settled();assert(!film_lut_soft_level()&&!film_lut_soft_work()); }
 reset();unlink(RETRO_FILE);unlink(STAMP_FILE);
 puts("Soft focus settings: default Off, RETRO.CFG byte 11 bits 2-3, Bloom workspace in the pool with Bloom off, reload and failed-write rollback passed");
 puts("Print settings: default Off, dust in RETRO.CFG byte 11 (old toning bits ignored), stamp corner/content/text in STAMP.CFG, date formats from the camera clock, any printable ASCII via the text box (others as '?', 24-character limit, cancel ignored), failed-write rollback, damaged records as Off and dust polarity per look passed");
 puts("Film processing switch: default Off, marker persistence and reload, coalescing, failed-save reporting and capture exclusion passed");
 /* Hardware tables. Each look is its .HWT; with Film processing on, Canon's
  * My Colors Off pointer is the look's table, otherwise Canon's, and an
  * unexpected pointer refuses the install. A missing, short or malformed
  * table cannot be loaded and the active look stays. Monochrome looks are
  * the tables whose every node has zero chroma. */
 reset();unlink(PROCESSING_MARKER);
 { unsigned words[1068];int k;unsigned char *tr=(unsigned char*)(words+1000);static unsigned other[1000];
   for(k=0;k<1000;k++)words[k]=(unsigned)k*4099u&0x3fffffffu;
   memcpy(tr,"HWTINT1\0",8);tr[8]=0x65;tr[9]=1;tr[10]=47;tr[11]=0;tr[12]=3;tr[13]=0;tr[14]=tr[15]=0;   /* 357, 47, 3 */
   for(k=0;k<256;k++)tr[16+k]=(unsigned char)(k/2);
   put("A/CHDK/FILM/LUTS/GOLD17.HWT",words,sizeof(words));
   for(k=0;k<1000;k++)words[k]^=5u;
   put("A/CHDK/FILM/LUTS/PORTRA.HWT",words,sizeof(words));
   put("A/CHDK/FILM/LUTS/EKTAR.HWT",words,4000);                                   /* no trailer */
   words[3]|=1u<<31;put("A/CHDK/FILM/LUTS/VELVIA.HWT",words,sizeof(words));          /* malformed */
   unlink("A/CHDK/FILM/FXSEL0.CFG");unlink("A/CHDK/FILM/FXSEL1.CFG");unlink("A/CHDK/FILM/FILMSEL0.CFG");unlink("A/CHDK/FILM/FILMSEL1.CFG");
   unlink("A/CHDK/FILM/LUTSEL0.CFG");unlink("A/CHDK/FILM/LUTSEL1.CFG");
   film_lut_init();
   assert(active==0&&resident&&!film_lut_hw_table()&&canon_presets[0]==canon_off&&!film_lut_mono());   /* processing off */
   assert(canon_sharpen==-1);
   film_lut_menu_processing(1,0);settled();assert(film_lut_processing()&&film_lut_hw_table()==resident&&canon_presets[0]==resident&&canon_sharpen==-1);   /* Sharpening Original: no override */
   /* ZrnoBoy: Canon's own table while it is the engine (Canon applies the
    * table again after ZrnoBoy writes the frame), the look's again after. */
   gboy_engine=1;film_lut_poll(0);assert(!film_lut_hw_table()&&canon_presets[0]==canon_off);
   gboy_engine=0;film_lut_poll(0);assert(film_lut_hw_table()==resident&&canon_presets[0]==resident);
   gboy_engine=2;film_lut_poll(0);assert(!film_lut_hw_table()&&canon_presets[0]==canon_off);   /* Print: Canon's table too */
   gboy_engine=0;film_lut_poll(0);assert(film_lut_hw_table()==resident&&canon_presets[0]==resident);
   /* Grade: its own table (not the look's: no halation tint for the
    * stage), rebuilt into the other buffer on an edit, at most every 150 ms;
    * the look's table again after. */
   { int b=grade_builds;const unsigned *first;
     gboy_engine=3;film_lut_poll(0);first=canon_presets[0];
     assert(!film_lut_hw_table()&&(first==grade_tables[0]||first==grade_tables[1])&&first[0]==grade_serial*7u&&grade_builds==b+1);
     grade_serial++;film_lut_poll(0);assert(canon_presets[0]==first&&grade_builds==b+1);          /* within 150 ms */
     now+=150;film_lut_poll(0);assert(canon_presets[0]!=first&&canon_presets[0][0]==grade_serial*7u&&grade_builds==b+2);
     now+=150;film_lut_poll(0);assert(grade_builds==b+2);                                            /* no edit: no rebuild */
     shooting=1;grade_serial++;now+=150;film_lut_poll(0);assert(grade_builds==b+2);shooting=0;        /* never during a shot */
     film_lut_poll(0);assert(grade_builds==b+3);
     gboy_engine=0;film_lut_poll(0);assert(film_lut_hw_table()==resident&&canon_presets[0]==resident); }
   film_lut_menu_sharpen(1,0);settled();assert(sharp_active==1&&canon_sharpen==32);
   assert(film_lut_hw_tint()&&film_lut_hw_tint()->red==357&&film_lut_hw_tint()->green==47&&film_lut_hw_tint()->blue==3&&film_lut_hw_tint()->highlight[200]==100);
   /* Halation colour: Red by default; Amber is stored as HALOAMB.TXT and
    * takes the look's .HWA tint, or without one an estimate from the Red
    * tint (green up 5/16 of red; a grey look's glow 29/16 as bright). The
    * highlight weights stay the .HWT's. */
   assert(!hcol_active&&!strcmp(film_lut_menu_halo_colour(0,0),"Red")&&access(HALO_AMBER_MARKER,F_OK));
   { const unsigned *gold=resident;
     fail_write=1;film_lut_menu_halo_colour(1,0);settled();
     assert(!hcol_active&&access(HALO_AMBER_MARKER,F_OK)&&!strcmp(film_lut_menu_status(0,0),"Save failed")&&!strcmp(film_lut_menu_halo_colour(0,0),"Red"));fail_write=0;
     film_lut_menu_halo_colour(1,0);settled();
     assert(hcol_active==1&&!access(HALO_AMBER_MARKER,F_OK)&&!strcmp(film_lut_menu_halo_colour(0,0),"Amber")&&resident==gold&&canon_presets[0]==resident);
     assert(film_lut_hw_tint()->red==357&&film_lut_hw_tint()->green==47+111&&film_lut_hw_tint()->blue==3&&film_lut_hw_tint()->highlight[200]==100);
     reset();film_lut_init();assert(hcol_active==1&&hcol_choice==1&&film_lut_hw_tint()->green==158);   /* the marker survives a restart */
     { unsigned char a[16]={'H','W','A','M','B','R','1',0,0x2c,1,150,0,9,0,0,0};          /* 300, 150, 9 */
       put("A/CHDK/FILM/LUTS/PORTRA.HWA",a,16);a[0]='X';put("A/CHDK/FILM/LUTS/HP5.HWA",a,16); }   /* HP5's is damaged */
     gold=resident;
     film_lut_menu_choice(1,0);settled();assert(active==1&&resident&&resident!=gold&&canon_presets[0]==resident&&resident[0]==5u);
     assert(film_lut_hw_tint()->red==300&&film_lut_hw_tint()->green==150&&film_lut_hw_tint()->blue==9&&film_lut_hw_tint()->highlight[200]==100); }
   memcpy(canon_loaded,resident,4000);assert(film_hw_used(film_lut_hw_table()));
   film_lut_menu_choice(1,0);settled();                                             /* VELVIA: malformed */
   assert(active==1&&canon_presets[0]==resident&&!strcmp(film_lut_menu_status(0,0),"LUT load failed")&&!strcmp(film_lut_menu_choice(0,0),looks[1].label));
   film_lut_menu_choice(4,0);settled();assert(active==1&&!strcmp(film_lut_menu_status(0,0),"LUT load failed"));   /* EKTAR: no trailer */
   film_lut_menu_choice(3,0);settled();assert(active==4&&film_lut_mono()&&canon_presets[0]==resident&&canon_sharpen==32);   /* HP5: mono; sharpening is not per look */
   { film_halo_tint red;read_tint(resident,&red,0);                                  /* a grey look's estimate: all three 29/16 */
     assert(red.red==red.green&&red.green==red.blue&&film_lut_hw_tint()->red==(red.red*29u>>4)&&film_lut_hw_tint()->green==film_lut_hw_tint()->red&&film_lut_hw_tint()->blue==film_lut_hw_tint()->red);
     film_lut_menu_halo_colour(1,0);settled();assert(!hcol_active&&access(HALO_AMBER_MARKER,F_OK)&&film_lut_hw_tint()->red==red.red); }
   film_lut_menu_choice(-3,0);settled();assert(active==1&&!film_lut_mono());
   film_lut_menu_processing(1,0);settled();assert(!film_lut_processing()&&!film_lut_hw_table()&&canon_presets[0]==canon_off&&canon_sharpen==-1);
   canon_presets[0]=other;film_lut_menu_processing(1,0);settled();                  /* not Canon's Off table: refused */
   assert(film_lut_processing()&&!film_lut_hw_table()&&!film_lut_hw_tint()&&canon_presets[0]==other);
   canon_presets[0]=canon_off;film_lut_menu_processing(1,0);settled();assert(!film_lut_processing()); }
 reset();unlink(PROCESSING_MARKER);canon_presets[0]=canon_off;restore_tables(argv[2],files,sizeof(files)/sizeof(files[0]));
 puts("Hardware tables: each look is its .HWT (Amber halation tint from its .HWA, or estimated), HALOAMB.TXT marker with restart and failed-save rollback, Canon's pointer ours only with processing on, an unexpected pointer refuses the install, missing/short/malformed tables keep the active look, mono from zero chroma");
 /* Sharpening: Original (Canon's own) by default (no override); Soft, Off and Crisp set
  * Canon's gain (32, 0, 112) while processing is on and persist in RETRO.CFG
  * byte 11 bits 6-7 beside soft focus and dust; processing off gives
  * Canon's own back; a failed write keeps what is stored. */
 reset();unlink(RETRO_FILE);unlink(PROCESSING_MARKER);canon_sharpen=-1;film_lut_init();
 assert(!sharp_active&&!strcmp(film_lut_menu_sharpen(0,0),"Original")&&canon_sharpen==-1);
 assert(!strcmp(film_lut_menu_sharpen(1,0),"Soft")&&!strcmp(film_lut_menu_sharpen(1,0),"Off")&&!strcmp(film_lut_menu_sharpen(1,0),"Crisp")&&!strcmp(film_lut_menu_sharpen(1,0),"Original"));
 assert(!strcmp(film_lut_menu_sharpen(-1,0),"Crisp"));settled();
 assert(sharp_active==3&&canon_sharpen==-1);                 /* processing off */
 { unsigned char rec[16];FILE *f=fopen(RETRO_FILE,"rb");assert(f&&fread(rec,1,16,f)==16);fclose(f);assert(rec[11]==(3<<6)); }
 film_lut_menu_processing(1,0);settled();assert(film_lut_processing()&&canon_sharpen==112);
 reset();canon_sharpen=-1;film_lut_init();assert(sharp_active==3&&canon_sharpen==112);
 film_lut_menu_sharpen(-1,0);settled();assert(sharp_active==2&&canon_sharpen==0);
 fail_write=1;film_lut_menu_sharpen(1,0);settled();assert(sharp_active==2&&canon_sharpen==0&&!strcmp(film_lut_menu_status(0,0),"Save failed")&&!strcmp(film_lut_menu_sharpen(0,0),"Off"));fail_write=0;
 film_lut_menu_sharpen(-2,0);settled();assert(!sharp_active&&canon_sharpen==-1);
 film_lut_menu_sharpen(1,0);settled();assert(canon_sharpen==32);
 film_lut_menu_processing(1,0);settled();assert(!film_lut_processing()&&canon_sharpen==-1);
 reset();unlink(RETRO_FILE);unlink(PROCESSING_MARKER);canon_sharpen=-1;
 puts("Sharpening: default Original (no override), Soft/Off/Crisp gains with processing on, Canon's own with it off, RETRO.CFG byte 11 bits 6-7 persistence and failed-write rollback passed");
 /* The live view's colour table is reloaded once whenever the table in
  * effect changes (a new look, Processing on or off), only in shooting mode
  * with My Colors Off, and never at start-up (Canon loads it then). */
 reset();unlink(RETRO_FILE);unlink(PROCESSING_MARKER);canon_presets[0]=canon_off;previewed_known=0;preview_ok=1;reloads=0;
 film_lut_init();assert(!reloads);
 film_lut_menu_processing(1,0);settled();assert(film_lut_hw_table()&&reloads==1);          /* look on */
 film_lut_menu_grain(1,0);settled();assert(reloads==1);                                     /* same table */
 film_lut_menu_choice(1,0);settled();assert(reloads==2&&canon_presets[0]==resident);       /* new look */
 preview_ok=0;film_lut_menu_choice(1,0);settled();assert(reloads==2);                       /* My Colors on, or playback */
 preview_ok=1;film_lut_menu_grain(-1,0);settled();assert(reloads==2);                       /* that table is already in place */
 film_lut_menu_processing(1,0);settled();assert(!film_lut_hw_table()&&reloads==3);         /* Canon's own again */
 fail_write=1;film_lut_menu_choice(1,0);settled();fail_write=0;assert(reloads==3);          /* a failed save keeps what is shown */
 /* Grade: each rebuilt grade shows in the live view; Processing Off
  * gives Canon's own table back. */
 film_lut_menu_processing(1,0);settled();assert(reloads==4);
 gboy_engine=3;film_lut_poll(0);assert(reloads==5&&!film_lut_hw_table());
 grade_serial++;now+=150;film_lut_poll(0);assert(reloads==6);
 film_lut_menu_processing(1,0);settled();assert(canon_presets[0]==canon_off&&reloads==7);
 gboy_engine=0;
 reset();unlink(RETRO_FILE);unlink(PROCESSING_MARKER);canon_presets[0]=canon_off;preview_ok=0;
 puts("Preview: the live view's table reloaded once per change of table (look, Processing), only in shooting mode with My Colors Off, not at start-up or for effect-only edits");
 /* Applying a change while the Zrno menu is up redraws it without erasing
  * the screen (an erase shows the live view for a moment); elsewhere the
  * screen is restored as before. */
 reset();film_lut_init();
 { int r=restores,d=redraws;menu_up=1;
   film_lut_menu_grain(1,0);settled();assert(grain_active==1&&restores==r&&redraws==d+1);
   film_lut_menu_grain(1,0);film_lut_menu_grain(-1,0);settled();assert(restores==r&&redraws==d+2);   /* back to what is applied */
   menu_up=0;film_lut_menu_grain(-1,0);settled();assert(!grain_active&&restores==r+1&&redraws==d+2); }
 reset();unlink(RETRO_FILE);
 puts("Screen refresh: a change applied under the Zrno menu redraws it without an erase; elsewhere it restores");
 /* Looks: none saved at first; saving names the look after its LUT and
  * writes LOOKS.TXT from SpyTask; the Look row recalls every setting in one
  * step, marks edits with '*', and the indicator shows the look's name. */
 reset();unlink(RETRO_FILE);unlink(STAMP_FILE);unlink(LOOKS_FILE);unlink(PROCESSING_MARKER);
 preset_count=0;preset_base=-1;presets_version=presets_saved=0;
 film_lut_init();
 { int en;
   assert(!strcmp(film_lut_menu_look(0,0),"None saved")&&preset_count==0);
   film_lut_menu_look(1,0);assert(request_version==handled_version);                   /* nothing to recall */
   film_lut_look_label(FILM_LOOK_UPDATE,&en);assert(!en);film_lut_look_label(FILM_LOOK_DELETE,&en);assert(!en);
   assert(!strcmp(film_lut_look_label(FILM_LOOK_SAVE,&en),"Save as new look")&&en);
   while(choice!=1){film_lut_menu_choice(1,0);}
   film_lut_menu_halo(1,0);film_lut_menu_grain(2,0);film_lut_menu_vignette(1,0);film_lut_menu_sharpen(2,0);settled();
   assert(!strcmp(shown,"Portra 400")&&!preset_shown[0]);
   int oldwrites=writes;
   film_lut_look_action(FILM_LOOK_SAVE);assert(preset_count==1&&!strcmp(presets[0].name,"Look 1")&&!strcmp(film_lut_menu_look(0,0),"Look 1"));
   assert(writes==oldwrites);settled();assert(writes>oldwrites&&presets_saved==presets_version);  /* written by the poll */
   { FILE *f=fopen(LOOKS_FILE,"r");char line[512];int found=0;assert(f);
     while(fgets(line,sizeof line,f))if(!strcmp(line,"Look 1|PORTRA.LUT|halation=Low bloom=Off soft=Off sharpening=Off grain=Mid vignette=Low leaks=Off toy=Off dust=Off stamp=Off shows=YYMMDD halationcolour=Red|\n"))found=1;
     fclose(f);assert(found); }
   /* A second look from the same LUT is numbered; an edit shows '*'. */
   film_lut_menu_toy(1,0);film_lut_menu_stamp(1,0);film_lut_menu_stamp_mode(2,0);stub_tbox_select=0;film_lut_menu_stamp_text(1,0);stub_tbox_select("Roll 7");
   assert(!strcmp(film_lut_menu_look(0,0),"Look 1*"));
   assert(!strcmp(film_lut_look_label(FILM_LOOK_UPDATE,&en),"Update Look 1")&&en);
   film_lut_look_action(FILM_LOOK_SAVE);settled();assert(preset_count==2&&!strcmp(presets[1].name,"Look 2")&&!strcmp(presets[1].text,"Roll 7"));
   assert(toy_active==1&&!strcmp(shown,"Portra 400")&&!strcmp(preset_shown,"Look 2"));
   /* Recall applies everything: back to the first look, stamp off, the
    * typed text kept for the next look that shows it. */
   film_lut_menu_look(-1,0);assert(!strcmp(film_lut_menu_look(0,0),"Look 1"));settled();
   assert(active==1&&halo_active==1&&grain_active==2&&vig_active==1&&sharp_active==2&&!toy_active&&!stamp_active&&!strcmp(stext_active,"Roll 7")&&!strcmp(shown,"Portra 400")&&!strcmp(preset_shown,"Look 1"));
   film_lut_menu_look(1,0);settled();assert(toy_active==1&&stamp_active==1&&smode_active==2&&!strcmp(stext_active,"Roll 7"));
   /* After the last look comes Custom: the settings stay, no look is
    * current (not even the one they equal), Update, Rename and Delete have
    * nothing to act on, and edits show no '*'. Then round to the first. */
   film_lut_menu_look(1,0);settled();assert(!strcmp(film_lut_menu_look(0,0),"Custom")&&toy_active==1&&stamp_active==1&&!preset_shown[0]);
   film_lut_look_label(FILM_LOOK_UPDATE,&en);assert(!en);film_lut_look_label(FILM_LOOK_RENAME,&en);assert(!en);film_lut_look_label(FILM_LOOK_DELETE,&en);assert(!en);
   film_lut_look_action(FILM_LOOK_DELETE);film_lut_look_action(FILM_LOOK_DELETE);assert(preset_count==2);
   film_lut_menu_vignette(1,0);settled();assert(!strcmp(film_lut_menu_look(0,0),"Custom")&&vig_active==2);
   film_lut_menu_vignette(-1,0);settled();assert(!strcmp(film_lut_menu_look(0,0),"Custom"));   /* equal to Look 2 again: still Custom */
   film_lut_menu_look(1,0);assert(!strcmp(film_lut_menu_look(0,0),"Look 1"));settled();   /* wraps */
   film_lut_menu_look(-1,0);assert(!strcmp(film_lut_menu_look(0,0),"Custom"));film_lut_menu_look(1,0);settled();
   assert(!strcmp(film_lut_menu_look(0,0),"Look 1")&&!strcmp(preset_shown,"Look 1"));
   /* Halation colour is part of a look: Amber marks Look 1 edited, and a
    * look without halation leaves the colour as it is. */
   film_lut_menu_halo_colour(1,0);settled();assert(hcol_active==1&&!strcmp(film_lut_menu_look(0,0),"Look 1*"));
   film_lut_menu_halo_colour(1,0);settled();assert(!hcol_active&&!strcmp(film_lut_menu_look(0,0),"Look 1"));
   film_lut_menu_halo(-1,0);film_lut_menu_halo_colour(1,0);settled();assert(!halo_active&&hcol_active==1&&!strcmp(film_lut_menu_look(0,0),"Look 1*"));
   film_lut_menu_halo(1,0);film_lut_menu_halo_colour(1,0);settled();assert(halo_active==1&&!hcol_active&&!strcmp(film_lut_menu_look(0,0),"Look 1"));
   /* Update overwrites the look it came from and keeps its name. */
   film_lut_menu_choice(1,0);settled();assert(!strcmp(film_lut_menu_look(0,0),"Look 1*")&&!strcmp(shown,looks[2].label)&&!preset_shown[0]);
   film_lut_look_action(FILM_LOOK_UPDATE);settled();assert(!strcmp(presets[0].file,"VELVIA.LUT")&&!strcmp(presets[0].name,"Look 1")&&!strcmp(film_lut_menu_look(0,0),"Look 1"));
   /* Rename through the text box; empty or cancelled names change nothing. */
   film_lut_look_action(FILM_LOOK_RENAME);assert(!strcmp(stub_tbox_default,"Look 1")&&stub_tbox_max==20);
   stub_tbox_select("  Velvia sun  ");settled();assert(!strcmp(presets[0].name,"Velvia sun")&&!strcmp(shown,looks[2].label)&&!strcmp(preset_shown,"Velvia sun"));
   film_lut_look_action(FILM_LOOK_RENAME);stub_tbox_select("   ");stub_tbox_select(0);assert(!strcmp(presets[0].name,"Velvia sun"));
   /* Settings, names and text survive a restart, read from the file. */
   reset();preset_count=0;film_lut_init();
   assert(preset_count==2&&!strcmp(presets[0].name,"Velvia sun")&&!strcmp(presets[1].name,"Look 2")&&!strcmp(presets[1].text,"Roll 7")&&presets[1].v[PV_TOY]==1);
   assert(!strcmp(film_lut_menu_look(0,0),"Velvia sun")&&!strcmp(shown,looks[2].label)&&!strcmp(preset_shown,"Velvia sun"));
   /* Delete takes SET twice; any other action disarms it. */
   film_lut_look_action(FILM_LOOK_DELETE);assert(preset_count==2&&!strcmp(film_lut_look_label(FILM_LOOK_DELETE,&en),"SET again to delete"));
   film_lut_look_disarm();film_lut_look_action(FILM_LOOK_DELETE);film_lut_look_action(FILM_LOOK_DELETE);settled();
   assert(preset_count==1&&!strcmp(presets[0].name,"Look 2")&&!strcmp(film_lut_menu_look(0,0),"Custom")&&!strcmp(shown,looks[2].label)&&!preset_shown[0]);
   /* New looks are named by their place in the list; a name already taken
    * (Look 2 moved up to first place) takes the next free number. */
   film_lut_look_action(FILM_LOOK_SAVE);assert(preset_count==2&&!strcmp(presets[1].name,"Look 3"));
   film_lut_menu_grain(1,0);film_lut_look_action(FILM_LOOK_SAVE);assert(preset_count==3&&!strcmp(presets[2].name,"Look 4"));
   film_lut_look_action(FILM_LOOK_DELETE);film_lut_look_action(FILM_LOOK_DELETE);assert(preset_count==2);
   film_lut_menu_grain(1,0);film_lut_look_action(FILM_LOOK_SAVE);settled();assert(preset_count==3&&!strcmp(presets[2].name,"Look 4"));
   /* A file edited on a computer: names trimmed, words in any case, digits,
    * unknown keys ignored (toning= too, from before it was removed),
    * left-out settings Off, lower-case LUT names, comments and broken lines
    * skipped, at most 16 looks. */
   { FILE *f=fopen(LOOKS_FILE,"w");assert(f);
     fputs("# hand edited\r\n  Beach day |ektar.lut| HALATION=high bloom=2 sparkle=9 stamp=TopLeft shows=ddmmyy HalationColour=amber|ignored text\r\nbroken line\nTri-X|TRIX.LUT|toning=Selenium grain=High sharpening=Crisp sharpening=Canon\n|PORTRA.LUT|grain=Low|\n",f);
     for(int k=0;k<20;k++)fprintf(f,"Extra %d|PORTRA.LUT||\n",k);
     fclose(f); }
   reset();preset_count=0;film_lut_init();
   assert(preset_count==16&&!strcmp(presets[0].name,"Beach day")&&!strcmp(presets[0].file,"EKTAR.LUT")&&presets[0].v[PV_HALO]==3&&presets[0].v[PV_BLOOM]==2);
   assert(presets[0].v[PV_STAMP]==4&&presets[0].v[PV_SMODE]==1&&!presets[0].text[0]&&!presets[0].v[PV_GRAIN]&&presets[0].v[PV_HCOL]==1);
   assert(!strcmp(presets[1].name,"Tri-X")&&presets[1].v[PV_GRAIN]==3&&presets[1].v[PV_SHARP]==0);   /* "Canon": Original's earlier name */
   assert(!strcmp(film_lut_look_label(FILM_LOOK_SAVE,&en),"Looks full (16)")&&!en);
   film_lut_look_action(FILM_LOOK_SAVE);assert(preset_count==16);
   film_lut_menu_look(1,0);settled();assert(active==5&&halo_active==3&&hcol_active==1&&bloom_active==2&&stamp_active==4&&smode_active==1&&!strcmp(shown,looks[5].label)&&!strcmp(preset_shown,"Beach day"));
   /* A failed write is reported, and a look whose LUT is missing applies the rest. */
   fail_write=1;film_lut_look_action(FILM_LOOK_DELETE);film_lut_look_action(FILM_LOOK_DELETE);settled();
   assert(!strcmp(film_lut_menu_status(0,0),"Look save failed")&&preset_count==15);fail_write=0;
   strcpy(presets[0].file,"NOPE.LUT");assert(!strcmp(film_lut_menu_look(0,0),"Custom"));
   film_lut_menu_look(-1,0);film_lut_menu_look(1,0);assert(!strcmp(film_lut_menu_look(0,0),"Custom"));film_lut_menu_look(1,0);settled();   /* the last look, Custom, the first */
   assert(active==1&&grain_active==3&&!halo_active&&hcol_active==1&&!strcmp(film_lut_menu_look(0,0),"Tri-X*"));   /* the extras use Portra; no halation: Amber kept */
   film_lut_menu_halo_colour(1,0);settled();assert(!hcol_active); }
 reset();unlink(LOOKS_FILE);unlink(RETRO_FILE);unlink(STAMP_FILE);unlink(HALO_AMBER_MARKER);preset_count=0;preset_base=-1;
 puts("Looks: saves named Look N by position, written by SpyTask, one-step recall of LUT, effects, halation colour and stamp (typed text and colour without halation kept), '*' on edits, update/rename/delete (SET twice), restart, hand-edited LOOKS.TXT, 16-look limit, failed write and missing LUT passed");
 /* Looks in every engine: each keeps its engine and that engine's
  * settings; recall switches the engine and sets only that engine's. */
 preset_count=0;preset_base=-1;presets_version=presets_saved=0;gboy_engine=0;engine_switches=0;stub_slot=0;
 for(int k=0;k<FILM_GRUI_SLOTS;k++)film_grade_neutral(&stub_grades[k]);
 memset(&stub_gboy,0,sizeof stub_gboy);memset(&stub_riso,0,sizeof stub_riso);
 film_lut_init();
 { int en;
   while(choice!=1){film_lut_menu_choice(1,0);}
   while(grain_choice!=1){film_lut_menu_grain(1,0);}
   settled();
   film_lut_look_action(FILM_LOOK_SAVE);settled();assert(preset_count==1&&presets[0].engine==ENGINE_FILM&&!strcmp(preset_shown,"Look 1"));
   /* ZrnoBoy: the Engine row switches; its settings make a look of their own. */
   gboy_engine=1;stub_gboy.palette=2;stub_gboy.frame=5;stub_gboy.contrast=9;stub_gboy.brightness=4;stub_gboy.edges=1;stub_gboy.resolution=1;
   settled();assert(!strcmp(film_lut_menu_look(0,0),"Look 1*")&&!preset_shown[0]);     /* a Film look: not ZrnoBoy's settings */
   film_lut_look_action(FILM_LOOK_SAVE);assert(preset_count==2&&presets[1].engine==ENGINE_GBOY&&!strcmp(film_lut_menu_look(0,0),"Look 2"));
   settled();assert(!strcmp(preset_shown,"Look 2"));
   stub_gboy.palette=3;assert(!strcmp(film_lut_menu_look(0,0),"Look 2*"));
   assert(!strcmp(film_lut_look_label(FILM_LOOK_UPDATE,&en),"Update Look 2")&&en);
   settled();assert(!preset_shown[0]);                                                /* the tab follows the engine's own rows */
   stub_gboy.palette=2;settled();assert(!strcmp(preset_shown,"Look 2")&&!strcmp(film_lut_menu_look(0,0),"Look 2"));
   /* Print and Grade the same. Grade keeps which grade, its values and
    * Canon's sharpening (its row in the Grade menu). */
   gboy_engine=2;stub_riso.style=FILM_RISO_STYLE_WOODCUT;stub_riso.variant=1;stub_riso.inks=4;stub_riso.noise=3;stub_riso.reg=2;stub_riso.flaws=1;stub_riso.paper=2;
   film_lut_look_action(FILM_LOOK_SAVE);assert(preset_count==3&&presets[2].engine==ENGINE_PRINT);
   gboy_engine=3;stub_slot=1;stub_grades[1].v[FILM_GRADE_CONTRAST]=3;stub_grades[1].v[FILM_GRADE_HSL_HUE+5]=-4;stub_grades[1].v[FILM_GRADE_MONO]=4;
   film_lut_menu_sharpen(1,0);settled();
   film_lut_look_action(FILM_LOOK_SAVE);assert(preset_count==4&&presets[3].engine==ENGINE_GRADE&&presets[3].slot==1&&presets[3].v[PV_SHARP]==1);
   settled();
   { FILE *f=fopen(LOOKS_FILE,"r");char line[1024];int found=0;assert(f);
     while(fgets(line,sizeof line,f)){
       if(!strcmp(line,"Look 2|ZrnoBoy|palette=2 frame=5 resolution=1 contrast=9 brightness=4 edges=1 dither=0\n"))found|=1;
       if(!strcmp(line,"Look 3|Print|style=3 palette=1 inks=4 separation=0 screen=0 noise=3 register=2 flaws=1 paper=2\n"))found|=2;
       if(!strcmp(line,"Look 4|Grade|grade=2 sharpening=Soft contrast=+3 bluehue=-4 blackwhite=4\n"))found|=4;
       if(!strncmp(line,"Look 1|PORTRA.LUT|halation=Off bloom=Off soft=Off sharpening=Original grain=Low ",strlen("Look 1|PORTRA.LUT|halation=Off bloom=Off soft=Off sharpening=Original grain=Low ")))found|=8;
     }
     fclose(f);assert(found==15); }
   /* Recall from any engine: the look's engine and settings, the others'
    * left as they are. */
   stub_grades[1].v[FILM_GRADE_CONTRAST]=-2;stub_slot=0;stub_gboy.palette=7;
   assert(!strcmp(film_lut_menu_look(0,0),"Look 4*"));                               /* Grade 1 now: an edit of Look 4 */
   film_lut_menu_look(-1,0);assert(gboy_engine==2&&stub_riso.style==FILM_RISO_STYLE_WOODCUT&&!strcmp(film_lut_menu_look(0,0),"Look 3"));
   stub_riso.noise=0;film_lut_menu_look(-1,0);
   assert(gboy_engine==1&&stub_gboy.palette==2&&stub_riso.noise==0&&!strcmp(film_lut_menu_look(0,0),"Look 2"));
   settled();assert(!strcmp(preset_shown,"Look 2"));
   film_lut_menu_look(-1,0);settled();assert(gboy_engine==0&&active==1&&grain_active==1&&sharp_active==0&&!strcmp(preset_shown,"Look 1"));
   film_lut_menu_look(-1,0);settled();assert(gboy_engine==0&&!strcmp(film_lut_menu_look(0,0),"Custom")&&!preset_shown[0]);   /* Custom: nothing changes */
   film_lut_menu_look(-1,0);settled();                                               /* wraps to Look 4 */
   assert(gboy_engine==3&&stub_slot==1&&stub_grades[1].v[FILM_GRADE_CONTRAST]==3&&stub_grades[1].v[FILM_GRADE_MONO]==4&&sharp_active==1&&active==1);
   assert(!strcmp(film_lut_menu_look(0,0),"Look 4")&&!strcmp(preset_shown,"Look 4")&&stub_gboy.palette==2);
   /* Update keeps the look's place and name, from its own engine. */
   stub_grades[1].v[FILM_GRADE_FADE]=2;film_lut_look_action(FILM_LOOK_UPDATE);assert(presets[3].grade.v[FILM_GRADE_FADE]==2&&!strcmp(film_lut_menu_look(0,0),"Look 4"));
   int switches=engine_switches;
   film_lut_menu_look(1,0);assert(gboy_engine==3&&engine_switches==switches&&!strcmp(film_lut_menu_look(0,0),"Custom"));
   film_lut_menu_look(1,0);assert(gboy_engine==0&&engine_switches==switches+1);
   film_lut_menu_look(1,0);assert(gboy_engine==1&&engine_switches==switches+2);
   /* Restart: every look read back with its engine. */
   settled();reset();preset_count=0;film_lut_init();
   assert(preset_count==4&&presets[0].engine==ENGINE_FILM&&!strcmp(presets[0].file,"PORTRA.LUT")&&presets[0].v[PV_GRAIN]==1);
   assert(presets[1].engine==ENGINE_GBOY&&presets[1].gboy.palette==2&&presets[1].gboy.frame==5&&presets[1].gboy.contrast==9&&presets[1].gboy.brightness==4&&presets[1].gboy.edges==1&&presets[1].gboy.resolution==1&&!presets[1].file[0]);
   assert(presets[2].engine==ENGINE_PRINT&&presets[2].riso.style==FILM_RISO_STYLE_WOODCUT&&presets[2].riso.variant==1&&presets[2].riso.inks==4&&presets[2].riso.noise==3&&presets[2].riso.reg==2&&presets[2].riso.flaws==1&&presets[2].riso.paper==2);
   assert(presets[3].engine==ENGINE_GRADE&&presets[3].slot==1&&presets[3].v[PV_SHARP]==1&&presets[3].grade.v[FILM_GRADE_CONTRAST]==3&&presets[3].grade.v[FILM_GRADE_HSL_HUE+5]==-4&&presets[3].grade.v[FILM_GRADE_FADE]==2&&presets[3].grade.v[FILM_GRADE_MONO]==4);
   { film_grade_settings g=presets[3].grade;g.v[FILM_GRADE_CONTRAST]=g.v[FILM_GRADE_HSL_HUE+5]=g.v[FILM_GRADE_FADE]=g.v[FILM_GRADE_MONO]=0;film_grade_settings z;film_grade_neutral(&z);assert(!memcmp(&g,&z,sizeof z)); }
   assert(!strcmp(film_lut_menu_look(0,0),"Look 2")&&!strcmp(preset_shown,"Look 2"));  /* ZrnoBoy, as left */
   /* Hand-edited: engine names in any case, values out of range ignored,
    * a grade's left-out values neutral. */
   { FILE *f=fopen(LOOKS_FILE,"w");assert(f);
     fputs("Boy|zrnoboy|palette=3 contrast=99 dither=1 bogus=2|stamp text ignored\nGr|GRADE|grade=9 contrast=+500 midtones=6 redhue=-5 shadowamount=+4 sharpening=crisp\nInk|Print|style=6 palette=2 paper=7\n",f);fclose(f); }
   reset();preset_count=0;film_lut_init();
   assert(preset_count==3&&presets[0].engine==ENGINE_GBOY&&presets[0].gboy.palette==3&&!presets[0].gboy.contrast&&presets[0].gboy.dither==1&&!presets[0].text[0]);
   assert(presets[1].engine==ENGINE_GRADE&&!presets[1].slot&&!presets[1].grade.v[FILM_GRADE_CONTRAST]&&presets[1].grade.v[FILM_GRADE_HSL_HUE]==-5&&!presets[1].grade.v[FILM_GRADE_MIDTONES]&&presets[1].grade.v[FILM_GRADE_HIGHLIGHT_AMOUNT]==3&&presets[1].grade.v[FILM_GRADE_SHADOW_AMOUNT]==4&&presets[1].v[PV_SHARP]==3);
   assert(presets[2].engine==ENGINE_PRINT&&presets[2].riso.style==6&&presets[2].riso.variant==2&&!presets[2].riso.paper);
   /* Sixteen Grade looks with every value at its widest fit the file. */
   { char *buf=malloc(LOOKS_BYTES);int k,i;assert(buf);
     for(i=0;i<MAX_PRESETS;i++){memset(&presets[i],0,sizeof presets[i]);sprintf(presets[i].name,"A twenty-char look %d",i%10);presets[i].engine=ENGINE_GRADE;presets[i].slot=3;
       for(k=0;k<FILM_GRADE_FIELDS;k++)presets[i].grade.v[k]=(signed char)(film_grade_min(k)<0?film_grade_min(k):film_grade_max(k));}
     preset_count=MAX_PRESETS;i=presets_format(buf);assert(i<LOOKS_BYTES);free(buf); } }
 /* The browser tool rewrites LOOKS.TXT over USB and leaves LOOKSRLD.TXT:
  * the looks are read again within a second; an unsaved edit of the
  * camera's own wins and the marker waits. */
 { FILE *f;struct stat st;
   reset();preset_count=0;presets_version=presets_saved=0;
   f=fopen(LOOKS_FILE,"w");assert(f);fputs("Old|PORTRA.LUT|grain=Low|\n",f);fclose(f);
   film_lut_init();assert(preset_count==1);
   f=fopen(LOOKS_FILE,"w");assert(f);fputs("# from the browser\nNew one|VELVIA.LUT|halation=High|\nPocket|ZrnoBoy|palette=1\n",f);fclose(f);
   f=fopen(LOOKS_RELOAD,"w");assert(f);fclose(f);
   presets_begin();now+=1000;film_lut_poll(0);assert(preset_count==1&&!stat(LOOKS_RELOAD,&st));   /* an edit in progress: not yet */
   presets_end();presets_saved=presets_version;
   now+=500;film_lut_poll(0);assert(preset_count==1&&!stat(LOOKS_RELOAD,&st));                    /* at most once a second */
   now+=1000;film_lut_poll(0);assert(preset_count==2&&!strcmp(presets[0].name,"New one")&&presets[1].engine==ENGINE_GBOY);
   assert(stat(LOOKS_RELOAD,&st));                                                               /* the marker is taken */
   now+=1000;film_lut_poll(0);assert(preset_count==2);
   puts("Looks reload: LOOKSRLD.TXT makes the camera read LOOKS.TXT again within a second, after its own pending edit, and is removed passed"); }
 reset();unlink(LOOKS_FILE);unlink(RETRO_FILE);unlink(STAMP_FILE);preset_count=0;preset_base=-1;gboy_engine=0;
 puts("Looks in every engine: ZrnoBoy, Print and Grade looks saved with their engine and settings (Grade: which grade, its values, sharpening), recalled from any engine with the engine switched and the others' settings kept, '*' and the indicator's tab following the engines' own rows, update, restart, hand-edited lines and 16 full Grade looks in the file passed");
 reset();puts("16-preset menu: each added LUT loads; last-slot persistence and bidirectional wrapping passed");
 puts("Auto settings: no Apply call; 250ms coalescing/no-op; no KBD I/O; capture exclusion; persistence; failed-load/save/allocation displayed rollback; effect-only LUT reuse; edits during load/save retained; renderer queries inert");
}
