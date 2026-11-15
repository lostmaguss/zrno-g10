/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native capture bridge: actual callbacks with mocked camera interfaces. */
#include <assert.h>
#include <stdio.h>
#include "../camera/film_queue.c"
static unsigned short drive,shooting_mode=32770,aspect,size,colors;
static int processing=1;
int film_reloc_status=1;
int film_lut_processing(void){return processing;}
static int gboy;
int film_gboy_engine(void){return gboy;}
static int grade;
int film_grade_engine(void){return grade;}
static int ticks,init_calls,polls,latched,close_fd,close_rc,closes;
long get_property_case(long id,void *p,long n){
 assert(n==2&&(id==49||id==102||id==222||id==300||id==188));
 *(unsigned short*)p=id==49?shooting_mode:id==102?drive:id==222?size:id==188?colors:aspect;return 0;
}
int get_tick_count(void){return ticks;}
int film_raw_init(void){init_calls++;return 1;}
void film_raw_latch(int v){latched=v;}
void film_raw_opened(const char *n,int fd){assert(!strcmp(n,"A/DCIM/117___01/IMG_0512.JPG")&&fd==4);}
void film_raw_written(int fd,int a,int b){assert(fd==4&&a==100&&b==99);}
void film_raw_closed(int fd,int rc){close_fd=fd;close_rc=rc;closes++;}
void film_raw_poll(void){polls++;}
int main(void){
 camera_info.state.mode_rec=1;
 /* Processing on: P, Tv, Av, M and Auto take the route, whatever the dial. */
 unsigned short photo[]={32768,32769,32770,32771,32772},other[]={33315,32801,16908,16940,16942,16939,16399,16903,2614};
 for(unsigned i=0;i<sizeof(photo)/sizeof(photo[0]);i++){shooting_mode=photo[i];film_capture_latch();assert(latched);}
 /* Scene, filter, low-light, stitch and movie modes never do. */
 for(unsigned i=0;i<sizeof(other)/sizeof(other[0]);i++){shooting_mode=other[i];film_capture_latch();assert(!latched);}
 shooting_mode=32772;
 drive=1;film_capture_latch();assert(!latched);drive=0;
 /* 4:3 only (aspect 0); 16:9, 3:2, 1:1 and 4:5 are rejected. */
 for(aspect=1;aspect<=4;aspect++){film_capture_latch();assert(!latched);}aspect=0;
 /* L (222 = 0), and M1, M2 and S (1, 2, 4: graded on Canon's shrunk frame);
  * M3 (3) and the Low Light and other sizes are rejected. */
 for(size=1;size<=7;size++){film_capture_latch();assert(latched==(size==1||size==2||size==4));}size=0;
 camera_info.state.mode_rec=0;film_capture_latch();assert(!latched);camera_info.state.mode_rec=1;
 /* The shared reason drives both the capture decision and the indicator. */
 assert(film_route_reason()==FILM_ROUTE_READY);
 shooting_mode=16940;assert(film_route_reason()==FILM_ROUTE_MODE);shooting_mode=32772;
 drive=1;assert(film_route_reason()==FILM_ROUTE_DRIVE);drive=0;
 aspect=1;assert(film_route_reason()==FILM_ROUTE_ASPECT);aspect=0;
 film_reloc_status=-5;assert(film_route_reason()==FILM_ROUTE_FIRMWARE);film_capture_latch();assert(!latched);   /* not G12 1.00G */
 processing=0;assert(film_route_reason()==FILM_ROUTE_OFF);processing=1;film_reloc_status=1;
 size=4;assert(film_route_reason()!=FILM_ROUTE_SIZE);size=7;assert(film_route_reason()==FILM_ROUTE_SIZE);size=0;
 /* My Colors must be Off (188 = 0): the look replaces that preset's table. */
 for(colors=1;colors<=11;colors++){assert(film_route_reason()==FILM_ROUTE_COLORS);film_capture_latch();assert(!latched);}
 /* ZrnoBoy uses only the luma: any My Colors preset takes the route. */
 gboy=1;for(colors=1;colors<=11;colors++){assert(film_route_reason()==FILM_ROUTE_READY);film_capture_latch();assert(latched);}gboy=0;colors=11;
 colors=0;assert(film_route_reason()==FILM_ROUTE_READY);
 /* Grade is the colour table alone: ready in any drive, aspect and
  * size, My Colors Off and a photo mode still needed, never latched. */
 grade=1;
 drive=1;aspect=2;size=3;assert(film_route_reason()==FILM_ROUTE_READY);film_capture_latch();assert(!latched);
 colors=3;assert(film_route_reason()==FILM_ROUTE_COLORS);colors=0;
 shooting_mode=16940;assert(film_route_reason()==FILM_ROUTE_MODE);shooting_mode=32772;
 drive=0;aspect=0;size=0;film_capture_latch();assert(!latched);grade=0;
 camera_info.state.mode_rec=0;assert(film_route_reason()==FILM_ROUTE_NOT_RECORDING);camera_info.state.mode_rec=1;
 processing=0;assert(film_route_reason()==FILM_ROUTE_OFF);processing=1;
 /* Processing off: no shot takes the route in any mode. */
 processing=0;
 for(unsigned i=0;i<sizeof(photo)/sizeof(photo[0]);i++){shooting_mode=photo[i];film_capture_latch();assert(!latched);}
 processing=1;shooting_mode=32770;film_capture_latch();assert(latched);
 film_opened("A/DCIM/117___01/IMG_0512.JPG",4);film_write_result(4,100,99);
 assert(film_closed(4,-1)==-1&&close_fd==4&&close_rc==-1&&closes==1);
 assert(film_special_closed(0)==0&&close_fd==-2&&close_rc==0&&closes==2);
 ticks=5000;film_boot_poll();assert(!init_calls&&polls==1);
 ticks=5001;film_boot_poll();assert(init_calls==1&&polls==2);
 ticks=9000;film_boot_poll();assert(init_calls==1&&polls==3);
 puts("Native bridge: processing switch, P/Tv/Av/M/Auto route with scene/filter/movie exclusion, single-shot/record/4:3/L gates and route reasons, Grade without the route, write/close forwarding, single special-close callback and delayed one-time init passed");
}
