/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native workflow with camera/file APIs mocked; no real card I/O. */
#include <assert.h>
/* Canon's settings record pool, mocked: slot 0 live, slots 1-3 for captures;
 * and the ring of three shot records whose +0x18 points at a capture's one. */
static long wb_ctx[4];static unsigned char wb_pool[4][0xdb8];static long wb_shots[3][0xe8/4];
#define WB_CTX wb_ctx
#define WB_SHOTS ((volatile long *)wb_shots)
#define WB_RAM(a) ((a)!=0)
#include "../camera/film_raw.c"
static void wb_pool_reset(void){memset(wb_pool,0,sizeof(wb_pool));memset(wb_shots,0,sizeof(wb_shots));wb_ctx[1]=wb_ctx[2]=(long)wb_pool[0];wb_ctx[3]=4;}
static void wb_gallery(int slot,unsigned char seed) {               /* a record in use, no shot's */
 wb_pool[slot][4]=1;for(int i=8;i<0xdb8;i++)wb_pool[slot][i]=seed+i*13;
}
static void wb_capture(int slot,unsigned char seed) {               /* a capture's record: in use, an active shot's */
 wb_gallery(slot,seed);wb_shots[(slot-1)%3][0]=1;wb_shots[(slot-1)%3][6]=(long)wb_pool[slot];
}
static int patch_mode=1;
int film_stage_patch_requested(void){return patch_mode;}
void film_stage_init(int e){(void)e;}
static unsigned armed_id;static int stage_result_rc;
void film_stage_arm(unsigned id){armed_id=id;}
int film_stage_result(unsigned id){return id==armed_id?stage_result_rc:-27;}
void film_stage_finish(void){}
void film_stage_poll(void){}
static int marker=1,bg_marker,ticks,video,remote,write_fail,close_fail,rename_fail;
/* FilmBackup task: 0 runs at once, 1 runs at the next msleep (a wait),
 * 2 never runs by itself, 3 CreateTask fails. */
static int task_mode,tasks_created;static void (*task_entry)(void);
int CreateTask(const char *n,int p,int s,void *e){
 (void)n;(void)s;assert(p==0x18&&!task_entry);if(task_mode==3)return -1;
 tasks_created++;task_entry=(void(*)(void))e;
 if(task_mode==0){task_entry=0;((void(*)(void))e)();}
 return 1;
}
void ExitTask(void){}
static void run_task(void){void(*e)(void)=task_entry;task_entry=0;if(e)e();}
static int ui_result,ui_active,ui_stage;static unsigned free_kb=999999;
typedef struct {char name[40];unsigned char *data;unsigned size,cap;int live,closed;} mock_file;
static mock_file fs[32];
static int find(const char *s){for(int i=0;i<32;i++)if(fs[i].live&&!strcmp(fs[i].name,s))return i;return -1;}
static int fgraw_stats,native_opens;
int mock_stat(const char *s,struct mock_stat *st) {
 if(!strncmp(s,"A/FGRAW/R",9))fgraw_stats++;
 if(!strcmp(s,"A/CHDK/FILM/RAWGOLD.TXT"))return marker?0:-1;
 if(!strcmp(s,"A/CHDK/FILM/BGBACKUP.TXT"))return bg_marker?0:-1;
 if(!strcmp(s,"A/FGRAW"))return 0;
 int i=find(s);if(i<0)return -1;if(st)st->st_size=fs[i].size;return 0;
}
int mock_open(const char *s,int flags,int mode) {
 (void)flags;(void)mode;if(!strcmp(s,"A/CHDK/FILM/NATIVE.TXT"))native_opens++;
 int i=find(s);if(i>=0)return i;
 for(i=0;i<32&&fs[i].live;i++){}
 assert(i<32);
 strcpy(fs[i].name,s);fs[i].live=1;fs[i].size=0;fs[i].closed=0;return i;
}
int mock_write(int fd,const void *p,unsigned n) {
 assert(fd>=0&&fd<32&&fs[fd].live);mock_file *f=&fs[fd];
 if(write_fail&&strstr(f->name,".TMP"))return n-1;
 if(f->size+n>f->cap){f->cap=f->size+n;f->data=realloc(f->data,f->cap);assert(f->data);}
 memcpy(f->data+f->size,p,n);f->size+=n;return n;
}
static unsigned rpos[32];static int read_fail;
int mock_read(int fd,void *p,unsigned n){assert(fd>=0&&fd<32&&fs[fd].live);if(read_fail)return -1;if(rpos[fd]+n>fs[fd].size)n=fs[fd].size-rpos[fd];memcpy(p,fs[fd].data+rpos[fd],n);rpos[fd]+=n;return n;}
int mock_close(int fd){rpos[fd]=0;fs[fd].closed=1;return close_fail&&strstr(fs[fd].name,".TMP")?-1:0;}
int mock_mkdir(const char *p){(void)p;return 0;}
int mock_rename(const char *a,const char *b){if(rename_fail)return -1;int i=find(a);assert(i>=0&&fs[i].closed&&find(b)<0);strcpy(fs[i].name,b);return 0;}
int mock_remove(const char *s){int i=find(s);if(i>=0)fs[i].live=0;return 0;}
int get_tick_count(void){return ticks++;}
void msleep(int t){ticks+=t;if(task_mode==1)run_task();}
int is_video_recording(void){return video;}
int shooting_in_progress(void){return 0;}
int remotecap_get_target(void){return remote;}
unsigned GetFreeCardSpaceKb(void){return free_kb;}
void film_ui_raw_begin(void){ui_active=1;ui_result=0;ui_stage=1;}
void film_ui_raw_stage(int s){ui_stage=s;}
void film_ui_raw_filename(const char *p){assert(strlen(p)==28);}
void film_ui_progress(int a,int b){assert(a>=0&&a<=b);}
static int ui_draws;
void film_ui_draw(void){ui_draws++;}
void film_ui_tick(void){}
void film_ui_result(int r,int detail){(void)detail;ui_result=r;ui_active=0;}
static void reset(void) {
 for(int i=0;i<32;i++){free(fs[i].data);memset(&fs[i],0,sizeof(fs[i]));}
 pending=complete=latched=0;next_id=1;jpeg_fd=-1;log_count=0;bg_wb_ok=0;
 write_fail=close_fail=rename_fail=video=remote=0;marker=1;free_kb=999999;
 bg_marker=0;task_mode=0;task_entry=0;tasks_created=0;bg_state=bg_abort=bg_used=0;bg_logged=1;
 camera_info.state.state_kbd_script_run=0;ui_result=ui_active=0;
 camera_sensor.bits_per_pixel=12;camera_sensor.black_level=127;camera_sensor.white_level=4095;
 camera_sensor.cfa_pattern=0x02010100;camera_sensor.raw_rowpix=3744;camera_sensor.raw_rows=2784;
 camera_sensor.raw_rowlen=5616;camera_sensor.raw_size=15634944;
 camera_sensor.active_area.x1=52;camera_sensor.active_area.x2=3736;camera_sensor.active_area.y1=12;camera_sensor.active_area.y2=2772;
 assert(film_raw_init());
}
int main(void) {
 const unsigned size=15634944;char *raw=malloc(size),*original=malloc(size);assert(raw&&original);
 for(unsigned i=0;i<size;i++)original[i]=(i*17+(i>>12))&255;
 for(int failure=0;failure<5;failure++) {
  reset();memcpy(raw,original,size);
  if(failure==0)write_fail=1;if(failure==1)close_fail=1;if(failure==2)rename_fail=1;
  if(failure==3)free_kb=1;if(failure==4)camera_sensor.raw_rowlen--;
  film_raw_latch(1);film_raw_process(raw);
  assert(!memcmp(raw,original,size));assert(!pending&&ui_result==FILM_UI_FAILED);
 }
 reset();marker=0;assert(!film_raw_init());film_raw_latch(1);film_raw_process(raw);assert(!ui_active);
 reset();film_raw_latch(0);film_raw_process(raw);assert(!ui_active);
 camera_info.state.state_kbd_script_run=1;film_raw_latch(1);film_raw_process(raw);assert(!ui_active);
 for(int gate=0;gate<2;gate++){reset();if(gate)remote=1;else video=1;film_raw_latch(1);film_raw_process(raw);assert(!ui_active);}
 for(int special=0;special<4;special++) {
  reset();memcpy(raw,original,size);
  /* Both finished and partial backups reserve their numbers. */
  mock_open("A/FGRAW/R0000001.RAW",0,0);mock_open("A/FGRAW/R0000002.TMP",0,0);
  film_raw_latch(1);film_raw_process(raw);
  int i=find("A/FGRAW/R0000003.RAW");assert(i>=0&&fs[i].size==size&&!memcmp(fs[i].data,original,size));
  /* The sensor buffer is only backed up, never edited. */
  assert(!memcmp(raw,original,size));assert(pending&&ui_active&&ui_stage==3);
  film_raw_latch(1);assert(!latched); /* Do not overlap a previous JPEG. */
  film_raw_opened("A/DCIM/117___01/IMG_0512.CR2",8);assert(jpeg_fd==-1);
  film_raw_opened("A/DCIM/117___01/IMG_0512.JPG",9);
  film_raw_closed(7,0);assert(!complete);
  film_raw_written(9,100,special==2?99:100);
  film_raw_closed(special==1?-2:9,special==3?-1:0);assert(complete);film_raw_poll();
  assert(!pending&&!ui_active&&ui_result==(special>=2?FILM_UI_FAILED:FILM_UI_SAVED));
  assert(!memcmp(fs[i].data,original,size));
 }
 reset();memcpy(raw,original,size);film_raw_latch(1);film_raw_process(raw);ticks+=180001;film_raw_poll();
 assert(!pending&&!ui_active&&ui_result==FILM_UI_FAILED);
 /* Without the LUT stage nothing happens: no backup, no UI, no pending JPEG. */
 reset();patch_mode=0;memcpy(raw,original,size);film_raw_latch(1);film_raw_process(raw);
 assert(!memcmp(raw,original,size)&&!pending&&!ui_active&&find("A/FGRAW/R0000001.RAW")<0);
 patch_mode=1;
 /* Re-develop: the next capture is graded without a new backup, logged
  * under its source; the source applies once, graded or not. */
 reset();int src=mock_open("A/FGRAW/R0000042.RAW",0,0);for(unsigned o=0;o<size;o+=4096){unsigned char b[4096];for(int k=0;k<4096;k++)b[k]=(o+k)*7+3;mock_write(src,b,4096);}mock_close(src);
 memcpy(raw,original,size);film_raw_redevelop("A/FGRAW/R0000042.RAW");film_raw_latch(1);film_raw_process(raw);
 assert(pending&&ui_active&&ui_stage==3&&find("A/FGRAW/R0000001.RAW")<0&&find("A/FGRAW/R0000001.TMP")<0&&!strcmp(backup_name,"A/FGRAW/R0000042.RAW"));
 assert(!memcmp(raw,fs[find("A/FGRAW/R0000042.RAW")].data,size));                                 /* the frame is now the backup */
 reset();memcpy(raw,original,size);film_raw_redevelop("A/FGRAW/R0000099.RAW");film_raw_latch(1);film_raw_process(raw);
 assert(!pending&&ui_result==FILM_UI_FAILED&&!memcmp(raw,original,size));                         /* missing backup: failed, frame untouched */
 reset();src=mock_open("A/FGRAW/R0000042.RAW",0,0);mock_write(src,original,size);mock_close(src);read_fail=1;
 film_raw_redevelop("A/FGRAW/R0000042.RAW");film_raw_latch(1);film_raw_process(raw);assert(!pending&&ui_result==FILM_UI_FAILED);read_fail=0;
 reset();film_raw_latch(1);film_raw_process(raw);assert(find("A/FGRAW/R0000001.RAW")>=0);     /* the next shot backs up again */
 reset();src=mock_open("A/FGRAW/R0000042.RAW",0,0);mock_write(src,original,size);mock_close(src);memset(raw,0,size);
 film_raw_redevelop("A/FGRAW/R0000042.RAW");film_raw_latch(0);film_raw_process(raw);assert(!ui_active&&!memcmp(raw,original,size));   /* ungraded, still the backup */
 film_raw_latch(1);film_raw_process(raw);assert(find("A/FGRAW/R0000001.RAW")>=0);              /* not carried past an ungraded capture */
 reset();film_raw_redevelop("A/FGRAW/a-name-far-too-long-to-fit.RAW");film_raw_latch(1);film_raw_process(raw);assert(find("A/FGRAW/R0000001.RAW")>=0);
 /* White balance: a shot saves its record beside the backup; a re-develop
  * copies only the saved WB (+0x5a9, +0x78d..+0x7b7) into its own record. */
 reset();wb_pool_reset();wb_capture(1,5);memcpy(raw,original,size);film_raw_latch(1);film_raw_process(raw);
 {int set=find("A/FGRAW/R0000001.SET");assert(set>=0&&fs[set].size==0xdb8&&!memcmp(fs[set].data,wb_pool[1],0xdb8));}
 film_raw_opened("A/DCIM/117___01/IMG_0512.JPG",9);film_raw_closed(9,0);film_raw_poll();
 {unsigned char saved[0xdb8];memcpy(saved,wb_pool[1],sizeof(saved));
  memset(wb_pool[1],0,0xdb8);wb_capture(2,99);unsigned char before[0xdb8];memcpy(before,wb_pool[2],sizeof(before));
  film_raw_redevelop("A/FGRAW/R0000001.RAW");film_raw_latch(1);film_raw_process(raw);assert(pending);
  for(int o=0;o<0xdb8;o++)assert(wb_pool[2][o]==((o==0x5a9||(o>=0x78d&&o<0x7b8))?saved[o]:before[o]));
  assert(find("A/FGRAW/R0000002.SET")<0);                                                         /* no second record either */
  /* An ungraded re-develop restores it too. */
  memcpy(wb_pool[2],before,sizeof(before));film_raw_redevelop("A/FGRAW/R0000001.RAW");film_raw_latch(0);pending=0;film_raw_process(raw);
  assert(wb_pool[2][0x78d]==saved[0x78d]&&wb_pool[2][0x5a9]==saved[0x5a9]&&wb_pool[2][0x78c]==before[0x78c]);
  /* The gallery's record in use beside the capture's: the shot's
   * own is taken; a finished shot's stale pointer at the gallery's is not. */
  wb_pool_reset();wb_gallery(1,40);wb_shots[2][6]=(long)wb_pool[1];wb_capture(2,99);memcpy(wb_pool[2],before,sizeof(before));
  {unsigned char gal[0xdb8];memcpy(gal,wb_pool[1],sizeof(gal));
   assert(wb_set("A/FGRAW/R0000001.RAW",raw)==0&&!memcmp(wb_pool[1],gal,sizeof(gal))&&wb_pool[2][0x78d]==saved[0x78d]);
   wb_shots[2][0]=1;assert(wb_set("A/FGRAW/R0000001.RAW",raw)==-32);wb_shots[2][0]=0;
   wb_shots[1][0]=0;assert(wb_set("A/FGRAW/R0000001.RAW",raw)==-30);wb_shots[1][0]=1;
   wb_shots[1][6]=(long)wb_pool[2]+4;assert(wb_set("A/FGRAW/R0000001.RAW",raw)==-30);       /* not a slot start */
   wb_shots[1][6]=(long)wb_pool[0];assert(wb_set("A/FGRAW/R0000001.RAW",raw)==-30);         /* the live record */
   wb_shots[1][6]=(long)wb_pool[2];}
  /* Two captures in use, or none: nothing is touched or saved. */
  memcpy(wb_pool[2],before,sizeof(before));wb_capture(3,7);assert(wb_set("A/FGRAW/R0000001.RAW",raw)==-32&&!memcmp(wb_pool[2],before,sizeof(before)));
  memset(wb_pool,0,sizeof(wb_pool));assert(wb_set("A/FGRAW/R0000001.RAW",0)==-30);
  /* A short .SET is refused; a shot without the pool still saves its backup. */
  wb_pool_reset();wb_capture(1,1);fs[find("A/FGRAW/R0000001.SET")].size=100;assert(wb_set("A/FGRAW/R0000001.RAW",raw)==-41&&wb_pool[1][0x78d]==(unsigned char)(1+0x78d*13));
  wb_ctx[1]=0;assert(wb_set("A/FGRAW/R0000001.RAW",0)==-39);}
 reset();wb_ctx[1]=0;film_raw_latch(1);film_raw_process(raw);assert(pending&&find("A/FGRAW/R0000001.RAW")>=0&&find("A/FGRAW/R0000001.SET")<0);
 /* Background backup (BGBACKUP.TXT): Canon resumes with the .TMP open; the
  * backup lands when the task runs, and the develop's wait (stage phase 2)
  * or Canon's next file open runs it to the end. */
 #define BG(mode) do{reset();bg_marker=1;assert(film_raw_init());task_mode=mode;memcpy(raw,original,size);film_raw_latch(1);}while(0)
 #define JPEG_DONE() do{film_raw_opened("A/DCIM/117___01/IMG_0512.JPG",9);film_raw_closed(9,0);film_raw_poll();}while(0)
 BG(1);film_raw_process(raw);
 assert(pending&&ui_stage==3&&tasks_created==1&&bg_state==1&&find("A/FGRAW/R0000001.TMP")>=0&&find("A/FGRAW/R0000001.RAW")<0);
 film_raw_poll();assert(bg_state==1&&pending);                                                    /* poll never waits */
 assert(film_raw_backup_wait()==0&&bg_state==2&&find("A/FGRAW/R0000001.TMP")<0);
 {int i=find("A/FGRAW/R0000001.RAW");assert(i>=0&&fs[i].size==size&&!memcmp(fs[i].data,original,size));}
 assert(!memcmp(raw,original,size));JPEG_DONE();assert(!pending&&ui_result==FILM_UI_SAVED);
 BG(0);film_raw_process(raw);assert(bg_state==2&&find("A/FGRAW/R0000001.RAW")>=0);JPEG_DONE();assert(ui_result==FILM_UI_SAVED);
 /* Canon's file open is the backstop when phase 2 never waited. */
 BG(1);film_raw_process(raw);film_raw_opened("A/DCIM/117___01/IMG_0512.JPG",9);
 assert(bg_state==2&&find("A/FGRAW/R0000001.RAW")>=0&&jpeg_fd==9);film_raw_closed(9,0);film_raw_poll();assert(ui_result==FILM_UI_SAVED);
 /* A failed write: no backup left, the graded JPEG is reported failed. */
 BG(1);write_fail=1;film_raw_process(raw);assert(pending);
 assert(film_raw_backup_wait()==-14&&find("A/FGRAW/R0000001.RAW")<0&&find("A/FGRAW/R0000001.TMP")<0);
 JPEG_DONE();assert(!pending&&ui_result==FILM_UI_FAILED);
 BG(1);rename_fail=1;film_raw_process(raw);assert(film_raw_backup_wait()==-17);JPEG_DONE();assert(ui_result==FILM_UI_FAILED);
 /* Storage checks still happen before Canon resumes. */
 BG(1);free_kb=1;film_raw_process(raw);assert(!pending&&!tasks_created&&ui_result==FILM_UI_FAILED);
 /* A task that never finishes: the wait gives up, aborts it, and later
  * waits return at once; the aborted backup leaves nothing behind. */
 BG(2);film_raw_process(raw);{int t0=ticks;assert(film_raw_backup_wait()==-25&&ticks-t0>BG_WAIT_MS&&bg_abort);}
 {int t0=ticks;assert(film_raw_backup_wait()==-25&&ticks-t0<5);}
 run_task();assert(bg_state==2&&bg_rc==-24&&find("A/FGRAW/R0000001.RAW")<0&&find("A/FGRAW/R0000001.TMP")<0);
 JPEG_DONE();assert(ui_result==FILM_UI_FAILED);
 /* No task: the backup is written before Canon resumes, as without the marker. */
 BG(3);film_raw_process(raw);assert(!tasks_created&&bg_state==0&&find("A/FGRAW/R0000001.RAW")>=0);JPEG_DONE();assert(ui_result==FILM_UI_SAVED);
 /* The next capture waits for a backup still being written. */
 BG(1);film_raw_process(raw);pending=0;film_raw_latch(1);film_raw_process(raw);
 assert(find("A/FGRAW/R0000001.RAW")>=0&&tasks_created==2&&bg_state==1&&bg_used);
 assert(film_raw_backup_wait()==0&&find("A/FGRAW/R0000002.RAW")>=0);JPEG_DONE();assert(ui_result==FILM_UI_SAVED);
 /* At the hook: no NATIVE.TXT I/O and no redraw; the next poll writes
  * the queued lines. The .SET holds the record as it was at the hook, is
  * written by the task after the backup, and is skipped when it fails. */
 BG(1);wb_pool_reset();wb_capture(1,5);{unsigned char snap[0xdb8];memcpy(snap,wb_pool[1],sizeof(snap));
  native_opens=ui_draws=0;film_raw_process(raw);
  assert(!native_opens&&!ui_draws&&pending&&find("A/FGRAW/R0000001.SET")<0&&log_count>=3);
  memset(wb_pool[1]+8,0x55,0x100);                                   /* Canon moves on */
  assert(film_raw_backup_wait()==0);int set=find("A/FGRAW/R0000001.SET");
  assert(set>=0&&fs[set].size==0xdb8&&!memcmp(fs[set].data,snap,0xdb8));
  film_raw_poll();int n=find("A/CHDK/FILM/NATIVE.TXT");assert(native_opens==1&&!log_count&&n>=0);
  char text[2048];unsigned len=fs[n].size<sizeof(text)-1?fs[n].size:sizeof(text)-1;memcpy(text,fs[n].data,len);text[len]=0;
  assert(strstr(text,"stage=begin")&&strstr(text,"stage=backup_started")&&strstr(text,"stage=to_canon")&&
         strstr(text,"stage=backup_saved")&&strstr(text,"stage=wb_saved rc=0"));
  assert(strstr(text,"stage=to_canon")<strstr(text,"stage=backup_saved"));
  JPEG_DONE();assert(ui_result==FILM_UI_SAVED);}
 BG(1);wb_pool_reset();wb_capture(1,5);write_fail=1;film_raw_process(raw);assert(film_raw_backup_wait()==-14&&find("A/FGRAW/R0000001.SET")<0);
 JPEG_DONE();wb_ctx[1]=0;
 /* Without the marker the hook still draws and writes the .SET itself. */
 reset();wb_pool_reset();wb_capture(1,5);ui_draws=0;film_raw_latch(1);film_raw_process(raw);
 assert(ui_draws>0&&find("A/FGRAW/R0000001.SET")>=0);JPEG_DONE();wb_ctx[1]=0;
 /* The first shot after boot finds the end of the numbers in a few stats;
  * it never takes a name in use and still fills from 1 when 1 is free. */
 reset();for(int k=1;k<=20;k++){char nm[24];sprintf(nm,"A/FGRAW/R%07d.RAW",k);mock_open(nm,0,0);}
 fgraw_stats=0;film_raw_latch(1);film_raw_process(raw);assert(find("A/FGRAW/R0000021.RAW")>=0&&fgraw_stats<=20);
 film_raw_opened("A/DCIM/117___01/IMG_0512.JPG",9);film_raw_closed(9,0);film_raw_poll();
 fgraw_stats=0;film_raw_latch(1);film_raw_process(raw);assert(find("A/FGRAW/R0000022.RAW")>=0&&fgraw_stats<=6);  /* later shots: from the last */
 reset();for(int k=1;k<=9;k++)if(k<6||k==9){char nm[24];sprintf(nm,"A/FGRAW/R%07d.%s",k,k==3?"TMP":"RAW");mock_open(nm,0,0);}
 film_raw_latch(1);film_raw_process(raw);assert(find("A/FGRAW/R0000006.RAW")>=0);
 reset();mock_open("A/FGRAW/R0000002.RAW",0,0);film_raw_latch(1);film_raw_process(raw);assert(find("A/FGRAW/R0000001.RAW")>=0);
 reset();film_raw_latch(1);film_raw_process(raw);assert(find("A/FGRAW/R0000001.RAW")>=0);
 /* A later shot without the background backup does not inherit its error. */
 BG(1);write_fail=1;film_raw_process(raw);film_raw_backup_wait();JPEG_DONE();write_fail=0;bg_requested=0;
 film_raw_latch(1);film_raw_process(raw);assert(pending&&!bg_used);JPEG_DONE();assert(ui_result==FILM_UI_SAVED);
 /* A shot the stage did not grade (busy, not observed, patch error) is
  * reported as failed, with the reason in NATIVE.TXT. */
 for(int k=0;k<3;k++) {
  reset();memcpy(raw,original,size);stage_result_rc=k==0?-27:k==1?-28:-102;film_raw_latch(1);film_raw_process(raw);
  film_raw_opened("A/DCIM/117___01/IMG_0512.JPG",9);film_raw_closed(9,0);film_raw_poll();film_raw_poll();
  int n=find("A/CHDK/FILM/NATIVE.TXT");assert(!pending&&ui_result==FILM_UI_FAILED&&n>=0);
  char text[4096];unsigned len=fs[n].size<sizeof(text)-1?fs[n].size:sizeof(text)-1;memcpy(text,fs[n].data,len);text[len]=0;
  char want[40];sprintf(want,"stage=not_graded rc=%d",stage_result_rc);assert(strstr(text,want));
 }
 stage_result_rc=0;
 reset();free(raw);free(original);
 puts("RAW native: sensor backup before grading with no sensor edits, storage/geometry failures, LUT-stage/script/video/remote gates, collision handling, normal/alternate close, short writes, timeout and re-develop (backup loaded over the frame, no second backup, failures reported, one capture only), white balance (record saved per shot, only the WB restored into one capture's record), and the background backup (written by its own task, waited for before Canon's files, failures, abort on timeout, no task, next capture) passed");
}
