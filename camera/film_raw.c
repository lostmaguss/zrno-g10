/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Runs in SpyTask while Canon waits at CHDK's RAW hook.
 * A full sensor backup must close successfully before the stage hook may
 * grade Canon's developed image. The sensor buffer itself is never changed,
 * except that a re-develop replaces it with the chosen backup.
 * BGBACKUP.TXT: the backup is written by a task of its own (FilmBackup,
 * priority 0x18, above the develop task's 0x19) while Canon develops and
 * Zrno grades. Canon leaves the sensor buffer alone until the JPEG is
 * saved and reuses one of its buffers within seconds after that (found
 * with a probe on the camera), so the develop task waits for the backup before
 * the JPEG encode (stage phase 2), and FileWriteTask waits before opening
 * any file while it is still running. A backup that fails then leaves a
 * graded JPEG without its backup, reported as failed.
 * The FileWriteTask callbacks only publish state; poll performs log/UI work. */
#include "task.h"
#include "camera_info.h"
#include "clock.h"
#include "shooting.h"
#include "sd_card.h"
#include "remotecap_core.h"
#include "film_raw.h"
#include "film_ui.h"
#include "film_stage.h"
static int enabled,latched,next_id=1;
static volatile int pending,complete,jpeg_fd=-1,jpeg_ok;
static int id,started,backup_ms,completed_tick;
static char backup_name[24],jpeg_name[32];
static int bg_requested,bg_used,bg_fd,bg_start,bg_logged;
static volatile int bg_state,bg_rc,bg_abort; /* 0 idle, 1 writing, 2 done */
static char bg_tmp[24];static const char *bg_raw;static unsigned bg_size;
/* The capture's white-balance record, copied at the hook and saved as the
 * .SET by the task once the backup is on the card (bg_wb_rc: its result). */
static unsigned char bg_wb[0xdb8];static int bg_wb_ok,bg_wb_rc;
static int wb_file(const char *raw_name,unsigned char *r,char *buf);
#define BG_WAIT_MS 20000
/* Re-develop (redev.flt): the next capture's sensor data is replaced with a
 * saved backup, loaded here the way backups are written (256 KB pieces with
 * pauses), then graded without a second backup and logged under the
 * source's name. CHDK's own RAW develop reads the whole 15.6 MB in one call
 * inside the capture hook, which crashed the G12 on repeated runs. */
static char redevelop_src[24];
void film_raw_redevelop(const char *src){if(src&&strlen(src)<sizeof(redevelop_src))strcpy(redevelop_src,src);else redevelop_src[0]=0;}
static void barrier(void){asm volatile("" ::: "memory");}
/* 1 for A/FGRAW/R0000001.RAW: the digits after the last "/R", 0 if none. */
static int backup_number(const char *name) {
    const char *p=name,*r=0;int n=0;
    for(;*p;p++)if(p[0]=='/'&&p[1]=='R')r=p+2;
    if(!r)return 0;
    for(;*r>='0'&&*r<='9';r++)n=n*10+(*r-'0');
    return n;
}
/* NATIVE.TXT lines are queued and written by the next poll, so the RAW
 * hook (Canon waiting) does no log I/O of its own. Only SpyTask logs. */
#define LOG_LINES 12
static char log_queue[LOG_LINES][224];static int log_count;
static void log_flush(void) {
    int fd,i;
    if(!log_count)return;
    fd=open("A/CHDK/FILM/NATIVE.TXT",O_WRONLY|O_CREAT|O_APPEND,0777);
    if(fd>=0){for(i=0;i<log_count;i++)write(fd,log_queue[i],strlen(log_queue[i]));close(fd);}
    log_count=0;
}
static void log_event(const char *stage,int rc) {
    if(log_count==LOG_LINES)log_flush();
    sprintf(log_queue[log_count++],"id=%d stage=%s rc=%d raw=%s jpg=%s backup_ms=%d total_ms=%d\n",
        id,stage,rc,backup_name,jpeg_name,backup_ms,
        (complete?completed_tick:get_tick_count())-started);
}
int film_raw_init(void) {
    enabled=!stat("A/CHDK/FILM/RAWGOLD.TXT",0);
    bg_requested=!stat("A/CHDK/FILM/BGBACKUP.TXT",0);
    film_stage_init(enabled);
    return enabled;
}
void film_raw_latch(int eligible) {
    latched=enabled&&eligible&&!pending&&!camera_info.state.state_kbd_script_run;
}
static int id_taken(int n) {
    char name[24];
    sprintf(name,"A/FGRAW/R%07d.RAW",n);if(!stat(name,0))return 1;
    sprintf(name,"A/FGRAW/R%07d.TMP",n);return !stat(name,0);
}
/* Opens the next backup's .TMP: fd, or a negative code. The first shot
 * after boot finds where the numbers end by doubling and halving (about 30
 * stats for 200 backups instead of one pair per backup); the scan below
 * then checks the name is free. */
static int backup_open(unsigned size,char *tmp) {
    int fd;
    if(GetFreeCardSpaceKb()<size/1024+32768)return -10;
    if(stat("A/FGRAW",0)&&mkdir("A/FGRAW"))return -11;
    if(next_id==1&&id_taken(1)) {
        int lo=1,hi=2,mid;
        while(hi<=9999&&id_taken(hi)){lo=hi;hi*=2;}
        if(hi>10000)hi=10000;
        while(hi-lo>1){mid=(lo+hi)/2;if(mid<=9999&&id_taken(mid))lo=mid;else hi=mid;}
        next_id=lo+1;
    }
    for(;next_id<=9999;next_id++) {
        sprintf(backup_name,"A/FGRAW/R%07d.RAW",next_id);
        sprintf(tmp,"A/FGRAW/R%07d.TMP",next_id);
        if(stat(backup_name,0)&&stat(tmp,0))break;
    }
    if(next_id>9999)return -12;
    id=next_id++;
    fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0777);
    return fd<0?-13:fd;
}
/* Writes, closes, checks and renames; a failed backup leaves no file. ui:
 * progress and redraws (SpyTask); the background task leaves the UI to the
 * stage and stops at bg_abort. */
static int backup_finish(int fd,const char *tmp,const char *raw,unsigned size,int ui) {
    int rc=0;unsigned off;struct stat st;
    for(off=0;off<size;) {
        unsigned n=size-off;if(n>262144)n=262144;
        if(!ui&&bg_abort){rc=-24;break;}
        if(write(fd,raw+off,n)!=(int)n){rc=-14;break;}
        off+=n;
        if(ui){film_ui_progress(off,size);if(off%1048576==0){film_ui_draw();msleep(10);}}
    }
    if(close(fd)&&!rc)rc=-15;
    if(!rc&&(stat(tmp,&st)||st.st_size!=size))rc=-16;
    if(!rc&&!ui&&bg_abort)rc=-24;
    if(!rc&&rename(tmp,backup_name))rc=-17;
    if(rc)remove(tmp);
    return rc;
}
static int sensor_backup(char *raw,unsigned size) {
    char tmp[24];int fd=backup_open(size,tmp);
    return fd<0?fd:backup_finish(fd,tmp,raw,size,1);
}
static void bg_task(void) {
    int rc=backup_finish(bg_fd,bg_tmp,bg_raw,bg_size,0);
    backup_ms=get_tick_count()-bg_start;
    if(!rc&&bg_wb_ok)bg_wb_rc=wb_file(backup_name,bg_wb,0);
    bg_rc=rc;
    barrier();bg_state=2;
    ExitTask();
}
/* Blocks until the background backup is done (or BG_WAIT_MS, which aborts
 * it): 0 or its error. Immediate when none is running. */
int film_raw_backup_wait(void) {
    int t;
    if(bg_state!=1)return bg_state==2?bg_rc:0;
    if(bg_abort)return -25;
    t=get_tick_count();
    while(bg_state==1) {
        if(get_tick_count()-t>BG_WAIT_MS){bg_abort=1;return -25;}
        msleep(10);
    }
    barrier();return bg_rc;
}
/* White balance for re-develops (G12 1.00G). Each capture
 * gets a copy of Canon's settings record in a pool slot (allocator
 * 0xff895680, context 0x31f8: [1] pool, [2] the live record, [3] slots of
 * 0xdb8 bytes, word 1 of a slot = in use); the develop takes the white
 * balance from it: mode +0x5a9, levels and results +0x78d..+0x7b7. A shot's
 * record is saved beside its backup as Rnnnnnnn.SET (buf 0). A re-develop
 * reads it through buf (the frame, which the backup replaces next) and copies
 * that white balance into its own capture's record while Canon waits here,
 * before the develop. The capture's record is the one an active shot record
 * points at (a ring of three 0xe8-byte entries at 0x3b780, word 0 the state,
 * +0x18 the settings record; set at the full press, before this hook): the
 * gallery keeps a record of its own in use, and finished shots keep stale
 * pointers. Returns 0, -30-n when not exactly one active shot points at a
 * record in use (n of them), -39 without the pool, -40/-41 for file errors. */
#ifndef WB_CTX
#define WB_CTX ((volatile long *)0x31f8)
#define WB_SHOTS ((volatile long *)0x3b780)
#define WB_RAM(a) ((a)>=0x1000&&(a)<0x800000)
#endif
#define WB_REC 0xdb8
/* The capture's record, or 0 with the error in *rc. */
static unsigned char *wb_record(int *rc) {
    long pool=WB_CTX[1],h;unsigned char *r=0;
    unsigned i,n=WB_CTX[3],hits=0;
    if(!WB_RAM(pool)||n>8){*rc=-39;return 0;}
    for(i=0;i<3;i++){
        volatile long *s=WB_SHOTS+i*(0xe8/4);
        h=s[6];
        if(s[0]&&h!=WB_CTX[2]&&h>=pool&&h<pool+(long)(n*WB_REC)&&(h-pool)%WB_REC==0&&((volatile unsigned *)h)[1]==1){r=(unsigned char *)h;hits++;}
    }
    if(hits!=1){*rc=-30-(int)hits;return 0;}
    return r;
}
/* Saves r beside raw_name (buf 0), or restores the white balance from it. */
static int wb_file(const char *raw_name,unsigned char *r,char *buf) {
    char name[24];int fd,rc=-41;
    strcpy(name,raw_name);strcpy(name+16,".SET");
    fd=open(name,buf?O_RDONLY:O_WRONLY|O_CREAT|O_TRUNC,0777);if(fd<0)return -40;
    if(!buf){if(write(fd,r,WB_REC)==WB_REC)rc=0;}
    else if(read(fd,buf,WB_REC)==WB_REC){r[0x5a9]=buf[0x5a9];memcpy(r+0x78d,buf+0x78d,0x2b);rc=0;}
    close(fd);
    return rc;
}
static int wb_set(const char *raw_name,char *buf) {
    int rc;unsigned char *r=wb_record(&rc);
    return r?wb_file(raw_name,r,buf):rc;
}
static int load_backup(char *raw,const char *src,unsigned size) {
    int fd=open(src,O_RDONLY,0777),rc=0;unsigned off;
    if(fd<0)return -21;
    for(off=0;off<size;) {
        unsigned n=size-off;if(n>262144)n=262144;
        if(read(fd,raw+off,n)!=(int)n){rc=-22;break;}
        off+=n;
        film_ui_progress(off,size);
        if(off%1048576==0){film_ui_draw();msleep(10);}
    }
    close(fd);
    return rc;
}
static void bg_log(void) {
    bg_logged=1;
    log_event(bg_rc?"backup_failed":"backup_saved",bg_rc);
    if(!bg_rc)log_event("wb_saved",bg_wb_rc);
}
void film_raw_process(char *raw) {
    int rc,t;char src[24];
    /* Never let a new capture start over a backup still being written. */
    film_raw_backup_wait();
    if(bg_state==2){if(!bg_logged)bg_log();bg_state=0;}
    bg_used=0;
    /* A re-develop source applies to this capture only, graded or not. */
    strcpy(src,redevelop_src);redevelop_src[0]=0;
    /* A re-develop the film route won't grade (say, a scene mode) still
     * develops the backup rather than whatever the lens saw. */
    if(src[0]&&!latched&&raw&&camera_sensor.raw_size==15634944){wb_set(src,raw);load_backup(raw,src,camera_sensor.raw_size);return;}
    if(!latched)return;
    latched=0;
    /* Without the full-frame LUT stage there is nothing to do: Canon's JPEG
     * is left untouched and no backup is written. */
    if(!film_stage_patch_requested()||remotecap_get_target()||is_video_recording())return;
    started=get_tick_count();backup_ms=0;id=0;
    backup_name[0]=jpeg_name[0]=0;complete=0;jpeg_fd=-1;
    /* In the background mode Canon resumes at once and SpyTask's loop
     * draws the panel; drawing here only keeps Canon waiting. */
    film_ui_raw_begin();film_ui_tick();if(!bg_requested||src[0])film_ui_draw();
    /* Exact platform geometry guards packed pixel and CFA assumptions. */
    if(!raw||camera_sensor.bits_per_pixel!=12||camera_sensor.black_level!=127||
       camera_sensor.white_level!=4095||camera_sensor.cfa_pattern!=0x02010100||
       camera_sensor.raw_rowpix!=3744||camera_sensor.raw_rows!=2784||
       camera_sensor.raw_rowlen!=5616||camera_sensor.raw_size!=15634944||
       camera_sensor.active_area.x1!=52||camera_sensor.active_area.y1!=12||
       camera_sensor.active_area.x2!=3736||camera_sensor.active_area.y2!=2772) {
        log_event("geometry_rejected",-18);film_ui_result(FILM_UI_FAILED,-18);return;
    }
    if(src[0]) {
        strcpy(backup_name,src);
        /* Logged under the source's number, so STAGELOG's raw_id names the
         * backup whose look this JPEG now has (the browser tool reads it). */
        id=backup_number(src);
        log_event("redevelop",0);
        log_event("wb_restored",wb_set(src,raw));
        t=get_tick_count();rc=load_backup(raw,src,camera_sensor.raw_size);backup_ms=get_tick_count()-t;
        if(rc){log_event("redevelop_failed",rc);film_ui_result(FILM_UI_FAILED,rc);return;}
        log_event("redevelop_loaded",0);
    } else if(bg_requested) {
        int fd;
        log_event("begin",0);
        bg_start=get_tick_count();fd=backup_open(camera_sensor.raw_size,bg_tmp);
        if(fd<0){log_event("backup_failed",fd);film_ui_result(FILM_UI_FAILED,fd);return;}
        {unsigned char *r;bg_wb_rc=0;r=wb_record(&bg_wb_rc);bg_wb_ok=r!=0;if(r)memcpy(bg_wb,r,sizeof(bg_wb));}
        bg_fd=fd;bg_raw=raw;bg_size=camera_sensor.raw_size;bg_rc=0;bg_abort=0;bg_logged=0;
        barrier();bg_state=1;bg_used=1;
        if(CreateTask("FilmBackup",0x18,0x1000,bg_task)<0) {
            bg_state=0;bg_used=0;rc=backup_finish(fd,bg_tmp,raw,bg_size,1);backup_ms=get_tick_count()-bg_start;
            if(rc){log_event("backup_failed",rc);film_ui_result(FILM_UI_FAILED,rc);return;}
            log_event("backup_saved",0);
            log_event("wb_saved",bg_wb_ok?wb_file(backup_name,bg_wb,0):bg_wb_rc);
        } else log_event("backup_started",0);
    } else {
        log_event("begin",0);
        t=get_tick_count();rc=sensor_backup(raw,camera_sensor.raw_size);backup_ms=get_tick_count()-t;
        if(rc){log_event("backup_failed",rc);film_ui_result(FILM_UI_FAILED,rc);return;}
        log_event("backup_saved",0);
        log_event("wb_saved",wb_set(backup_name,0));
    }
    film_ui_raw_stage(3);if(!bg_used)film_ui_draw();log_event("to_canon",0);
    film_stage_arm(id);
    barrier();pending=1;
}
void film_raw_opened(const char *name,int fd) {
    int n;
    /* Backstop for a develop that never reached stage phase 2: Canon's
     * files wait until the sensor buffer is on the card. */
    film_raw_backup_wait();
    if(!pending||complete||fd<0)return;
    n=strlen(name);
    if(n!=28||strncmp(name,"A/DCIM/",7)||strncmp(name+16,"IMG_",4)||strcmp(name+24,".JPG"))return;
    strcpy(jpeg_name,name);jpeg_fd=fd;jpeg_ok=1;
}
void film_raw_written(int fd,int expected,int actual) {
    if(pending&&fd==jpeg_fd&&expected!=actual)jpeg_ok=0;
}
void film_raw_closed(int fd,int result) {
    if(fd==-2)fd=jpeg_fd; /* G12 alternate close branch. */
    if(!pending||jpeg_fd<0||fd!=jpeg_fd||complete)return;
    completed_tick=get_tick_count();jpeg_fd=-1;
    if(result)jpeg_ok=0;
    barrier();complete=1;
}
void film_raw_poll(void) {
    int ok;
    film_stage_poll();
    if(bg_state==2&&!bg_logged){barrier();bg_log();}
    log_flush();
    if(!pending)return;
    if(complete) {
        /* The JPEG waited for the backup; a failed one fails the shot. */
        int bgerr=!bg_used?0:bg_abort?-25:bg_state==2?bg_rc:0;
        /* A JPEG Canon saved without the look is not a Zrno shot. */
        int graded=film_stage_result(id);
        barrier();ok=jpeg_ok&&!bgerr&&!graded;
        log_event(jpeg_ok?"jpeg_saved":"jpeg_failed",jpeg_ok?0:-19);
        if(jpeg_ok&&graded)log_event("not_graded",graded);
        film_ui_raw_filename(jpeg_name);
        film_ui_result(ok?FILM_UI_SAVED:FILM_UI_FAILED,!jpeg_ok?-19:bgerr?bgerr:graded);
        pending=0;
        film_stage_finish();
    } else if((unsigned)(get_tick_count()-started)>180000u) {
        film_stage_finish();
        log_event("jpeg_timeout",-20);pending=0;jpeg_fd=-1;
        film_ui_result(FILM_UI_FAILED,-20);
    }
}
