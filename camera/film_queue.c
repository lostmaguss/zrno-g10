/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. G12 native capture/FileWrite bridge.
 * No allocation, file I/O or heavy processing in capture/write callbacks. */
#include "camera_info.h"
#include "properties.h"
#include "clock.h"
#include "film_queue.h"
#include "film_raw.h"
#include "film_lut.h"
#include "film_gbui.h"
#include "film_reloc.h"
/* G12 1.00G property values: shooting mode (49), drive mode (102), JPEG size
 * (222, 0 = L), aspect ratio (300, 0 = 4:3) and My Colors (188, 0 = Off). With film processing on, the
 * standard photo modes P, Tv, Av, M and Auto take the film route on any dial
 * position, including C1/C2 when they store one of those modes. Scene, filter,
 * low-light and movie modes are excluded because Canon processes them
 * differently. Single shot and 4:3 are required. L (0) is graded on the full
 * 3648x2736 frame; M1 (1), M2 (2) and S (4) on Canon's shrunk frame, with the
 * effects scaled to it. The stage guard re-checks Canon's buffers before
 * writing. My Colors must be Off: the look replaces that preset's colour
 * table in Canon's develop (film_hwcolour.c), and any other preset would
 * leave the shot in Canon's colours. The ZrnoBoy engine uses only the
 * frame's luma, so it takes any My Colors preset.
 * The Grade engine is only the colour table, which Canon's develop
 * applies to every shot: it needs My Colors Off and a photo mode, takes any
 * drive, aspect and size, and never takes the film route (no backup, no
 * pixel work, Canon's own speed).
 * With processing off, nothing is latched: no backup and no pixel changes. */
static int photo_mode(unsigned short mode) {
    return mode==32768||mode==32769||mode==32770||mode==32771||mode==32772;
}
int film_route_reason(void) {
    unsigned short mode=0,drive=1,size=1,aspect=1,colors=1;
    if(!film_lut_processing())return FILM_ROUTE_OFF;
    if(film_reloc_status<0)return FILM_ROUTE_FIRMWARE;
    if(!camera_info.state.mode_rec)return FILM_ROUTE_NOT_RECORDING;
    get_property_case(49,&mode,sizeof(mode));
    get_property_case(102,&drive,sizeof(drive));
    get_property_case(222,&size,sizeof(size));
    get_property_case(300,&aspect,sizeof(aspect));
    get_property_case(188,&colors,sizeof(colors));
    if(!photo_mode(mode))return FILM_ROUTE_MODE;
    if(film_grade_engine())return colors!=0?FILM_ROUTE_COLORS:FILM_ROUTE_READY;
    if(drive!=0)return FILM_ROUTE_DRIVE;
    if(aspect!=0)return FILM_ROUTE_ASPECT;
    /* L, or M1, M2 and S graded on Canon's shrunk frame (film_stage.c). */
    if(size!=0&&size!=1&&size!=2&&size!=4)return FILM_ROUTE_SIZE;
    if(colors!=0&&!film_gboy_engine())return FILM_ROUTE_COLORS;   /* ZrnoBoy uses only the luma */
    return FILM_ROUTE_READY;
}
int film_preview_reloadable(void) {
    unsigned short colors=1;
    if(!camera_info.state.mode_rec)return 0;
    get_property_case(188,&colors,sizeof(colors));
    return colors==0;
}
void film_capture_latch(void) {
    film_raw_latch(film_route_reason()==FILM_ROUTE_READY&&!film_grade_engine());
}
void film_opened(const char *name,int fd){film_raw_opened(name,fd);}
void film_write_result(int fd,int expected,int actual){film_raw_written(fd,expected,actual);}
int film_closed(int fd,int result){film_raw_closed(fd,result);return result;}
int film_special_closed(int result){film_raw_closed(-2,result);return result;}
void film_boot_poll(void) {
    static int attempted;
    film_raw_poll();
    if(!attempted&&get_tick_count()>5000){attempted=1;film_raw_init();}
}
