# SPDX-License-Identifier: GPL-2.0-or-later
"""DEVWRITE probe (test build only): how does Canon's develop write the YUV
frame (0x4238a730) between the RAW hook and our stage hook (phase 1)?
At the hook it notes 8 words in every 16th row (uncached alias); a task
(FilmDevWatch, priority 0x17) rereads them every tick until phase 1 and
records, per point, the first and last change (ms after the hook) and the
number of changes. SpyTask writes A/CHDK/FILM/DEVWRITE.TXT when the JPEG
is saved. Read-only: the frame is never written.
Usage: python3 patch_tree.py <integrated tree>"""
import sys
from pathlib import Path
root=Path(sys.argv[1])
def edit(p,old,new):
    f=root/p;s=f.read_text();assert s.count(old)==1,(p,old[:60]);f.write_text(s.replace(old,new))
edit('include/film_raw.h','void film_raw_poll(void);','void film_raw_poll(void);\nextern volatile int devwatch_phase1;')
probe=r'''
#define DW_ROWS 171
#define DW_COLS 8
#define DW_N (DW_ROWS*DW_COLS)
volatile int devwatch_phase1;
static volatile int dw_state;            /* 0 idle, 1 running, 2 done */
static unsigned dw_base[DW_N];
static unsigned short dw_first[DW_N],dw_last[DW_N];static unsigned char dw_count[DW_N];
static int dw_start,dw_passes,dw_end_ms,dw_pass_ms_max,dw_id;
static volatile const unsigned *dw_word(unsigned i){
    unsigned row=(i/DW_COLS)*16u+8u,col=(i%DW_COLS)*228u+114u;   /* pixel pair index, 1824 per row */
    return (volatile const unsigned *)(0x4238a730u+row*7296u+col*4u);
}
static void dw_task(void){
    unsigned i;
    while(!devwatch_phase1&&(int)(get_tick_count()-dw_start)<5000){
        int t=get_tick_count(),now=t-dw_start;
        for(i=0;i<DW_N;i++){
            unsigned v=*dw_word(i);
            if(v!=dw_base[i]){
                if(!dw_count[i])dw_first[i]=(unsigned short)now;
                dw_last[i]=(unsigned short)now;if(dw_count[i]<255)dw_count[i]++;
                dw_base[i]=v;
            }
        }
        dw_passes++;t=get_tick_count()-t;if(t>dw_pass_ms_max)dw_pass_ms_max=t;
        msleep(10);
    }
    dw_end_ms=get_tick_count()-dw_start;
    dw_state=2;
    ExitTask();
}
static void dw_begin(int raw_id){
    unsigned i;
    if(dw_state==1)return;
    for(i=0;i<DW_N;i++){dw_base[i]=*dw_word(i);dw_first[i]=dw_last[i]=0;dw_count[i]=0;}
    dw_passes=dw_pass_ms_max=0;dw_id=raw_id;devwatch_phase1=0;dw_start=get_tick_count();dw_state=1;
    CreateTask("FilmDevWatch",0x17,0x800,dw_task);
}
static void dw_write(void){
    char line[64];unsigned i;int fd;
    if(dw_state!=2)return;
    dw_state=0;
    fd=open("A/CHDK/FILM/DEVWRITE.TXT",O_WRONLY|O_CREAT|O_APPEND,0777);if(fd<0)return;
    sprintf(line,"shot %d phase1_ms %d passes %d pass_ms_max %d\n",dw_id,dw_end_ms,dw_passes,dw_pass_ms_max);write(fd,line,strlen(line));
    for(i=0;i<DW_N;i++){
        sprintf(line,"%u %u %u %u %u\n",(i/DW_COLS)*16u+8u,(i%DW_COLS)*456u+228u,dw_first[i],dw_last[i],dw_count[i]);
        write(fd,line,strlen(line));
    }
    close(fd);
}
'''
edit('core/film_raw.c','static void bg_log(void) {',probe+'static void bg_log(void) {')
edit('core/film_raw.c','''    film_ui_raw_stage(3);if(!bg_used)film_ui_draw();log_event("to_canon",0);''','''    film_ui_raw_stage(3);if(!bg_used)film_ui_draw();log_event("to_canon",0);
    dw_begin(id);''')
edit('core/film_raw.c','''        film_ui_raw_filename(jpeg_name);''','''        film_ui_raw_filename(jpeg_name);
        dw_write();''')
edit('core/film_stage.c','''    if(phase==2)film_raw_backup_wait();''','''    if(phase==1)devwatch_phase1=1;
    if(phase==2)film_raw_backup_wait();''')
print('patched',root)
